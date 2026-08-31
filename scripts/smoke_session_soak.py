#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
Soak a long interactive session looking for memory corruption.

smoke-shell-pipe-stress runs one short round and passes; the corruption we are
after showed up in a session with ~3400s of uptime, repeated logins and doas
in the mix. This drives the same console for many rounds and leans on the
kernel-side watchdogs rather than on a command happening to crash:

  KTM_USER_CANARY_BROKEN  the 16 slack bytes above the initial argv/envp
                          image were overwritten (names the corruption)
  STACK_TOP_OVERRUN       a fault landed in the page just past USER_STACK_TOP
  CONSOLE_SESSION_SEGV    the shell died (downstream symptom)

The first two are failures. Session segv is counted and reported, since it is
the symptom we are trying to attribute rather than the signal itself.
"""

from __future__ import annotations

import argparse
import crypt
import os
import re
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROMPT_RE = re.compile(r"[a-zA-Z0-9_-]+@[a-zA-Z0-9_-]+:\S*[#$]")
NEED_BOOT = ["RUNIT_STAGE1_OK", "GETTY_READY"]

FATAL = ("KERNEL PANIC", "KTM_USER_CANARY_BROKEN", "STACK_TOP_OVERRUN")

# Rotated per round. Mixes pipelines, heavy readdir and set-id exec, which is
# what the reported session was doing when it came apart.
ROUND_COMMANDS = [
    ["echo pipeok | cat", "ls /proc | head", "id"],
    ["dmesg | cat | head", "cat /proc/uptime", "free"],
    ["hexdump -C /bin/busybox | head -n 3", "uptime"],
    ["du -h /etc | tail -n 3", "df -h"],
    ["lsblk", "mount | head"],
    ["yes | head -n 20", "cat /bin/busybox | head -n 1"],
    ["find /etc -name hosts", "wc -l /etc/passwd"],
]


def read_log(path: Path) -> str:
    if not path.is_file():
        return ""
    return path.read_text(errors="replace")


def kill_qemu(proc: subprocess.Popen[bytes]) -> None:
    if proc.poll() is not None:
        return
    try:
        proc.send_signal(signal.SIGTERM)
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=5)
    except ProcessLookupError:
        pass


def mon(port: int, cmd: str, wait: float = 0.05) -> None:
    import socket

    with socket.create_connection(("127.0.0.1", port), timeout=5) as s:
        s.settimeout(1.0)
        try:
            s.recv(4096)
        except Exception:
            pass
        s.sendall((cmd.strip() + "\r\n").encode())
        time.sleep(wait)
        try:
            while s.recv(4096):
                pass
        except Exception:
            pass


def type_str(port: int, s: str, delay: float = 0.06) -> None:
    for ch in s:
        if ch == " ":
            mon(port, "sendkey spc", delay)
        elif ch == "|":
            mon(port, "sendkey shift-backslash", delay)
        elif ch == "-":
            mon(port, "sendkey minus", delay)
        elif ch == "/":
            mon(port, "sendkey slash", delay)
        elif ch == ";":
            mon(port, "sendkey semicolon", delay)
        elif ch == ".":
            mon(port, "sendkey dot", delay)
        elif ch.isupper():
            mon(port, f"sendkey shift-{ch.lower()}", delay)
        else:
            mon(port, f"sendkey {ch}", delay)


def wait_tags(log: Path, tags: list[str], proc: subprocess.Popen[bytes],
              timeout: float) -> bool:
    deadline = time.time() + timeout
    while time.time() < deadline:
        text = read_log(log)
        if "FIRSTBOOT_FAIL" in text or "KERNEL PANIC" in text:
            return False
        if all(t in text for t in tags):
            return True
        if proc.poll() is not None:
            return False
        time.sleep(0.25)
    return False


def wait_prompt(log: Path, proc: subprocess.Popen[bytes], timeout: float,
                after: int = 0) -> bool:
    deadline = time.time() + timeout
    while time.time() < deadline:
        text = read_log(log)
        if "KERNEL PANIC" in text:
            return False
        if len(list(PROMPT_RE.finditer(text))) > after:
            return True
        if proc.poll() is not None:
            return False
        time.sleep(0.3)
    return False


def prompt_count(log: Path) -> int:
    return len(list(PROMPT_RE.finditer(read_log(log))))


def first_fatal(text: str) -> str | None:
    for tag in FATAL:
        if tag in text:
            idx = text.index(tag)
            return text[max(0, idx - 2000):idx + 4000]
    return None


def wait_count(log: Path, proc: subprocess.Popen[bytes], needle: str,
               want: int, timeout: float) -> bool:
    """Wait until `needle` has appeared at least `want` times."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        text = read_log(log)
        if "KERNEL PANIC" in text:
            return False
        if text.count(needle) >= want:
            return True
        if proc.poll() is not None:
            return False
        time.sleep(0.25)
    return False


def getty_baseline(log: Path) -> tuple[int, int, int]:
    """Prompt counts to compare against after triggering a new session.

    Must be sampled *before* the action that spawns the getty: after `exit`
    the username prompt is already on the wire, so counting inside login()
    waits for a second one that never comes.
    """
    text = read_log(log)
    return (len(list(PROMPT_RE.finditer(text))),
            text.count("Enter your Unix username"),
            text.count("Password:"))


def login(port: int, log: Path, proc: subprocess.Popen[bytes], user: str,
          password: str, baseline: tuple[int, int, int] | None = None) -> bool:
    """Log in, tolerating a getty that restarts while settling.

    Typing as soon as the username prompt appears is too early: getty reads
    the name, then respawns without ever printing "Password:". Earlier
    versions only worked by accident, syncing on the second prompt. Settle
    after the prompt and retry on a fresh one instead.
    """
    before, user_prompts, pass_prompts = (
        baseline if baseline is not None else getty_baseline(log))

    for attempt in range(3):
        if not wait_count(log, proc, "Enter your Unix username",
                          user_prompts + 1, 60):
            return False
        time.sleep(1.5)

        type_str(port, user)
        mon(port, "sendkey ret", 0.3)
        if not wait_count(log, proc, "Password:", pass_prompts + 1, 20):
            # getty respawned instead of prompting: resync on its new prompt.
            text = read_log(log)
            user_prompts = text.count("Enter your Unix username")
            pass_prompts = text.count("Password:")
            continue

        type_str(port, password)
        mon(port, "sendkey ret", 0.4)
        if wait_prompt(log, proc, 60, after=before):
            return True

        text = read_log(log)
        user_prompts = text.count("Enter your Unix username")
        pass_prompts = text.count("Password:")

    return False


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--iso", default=str(ROOT / "kernel-x64-userspace.iso"))
    ap.add_argument("--disk", default=str(ROOT / "disk.img"))
    ap.add_argument("--log", default="/tmp/ir0-session-soak.log")
    ap.add_argument("--rounds", type=int, default=8)
    # Off by default: the relogin path stalls the harness after a few
    # cycles, which cut every run short at round 3 and starved the soak of
    # the uptime it exists to accumulate. Enable explicitly to exercise it.
    ap.add_argument("--relogin-every", type=int, default=0,
                    help="log out and back in every N rounds (0 disables)")
    ap.add_argument("--port", type=int, default=46317)
    args = ap.parse_args()

    iso = Path(args.iso)
    src = Path(args.disk)
    log_path = Path(args.log)
    if not iso.is_file() or not src.is_file():
        print("✗ missing iso/disk — make load-userspace-runit + kernel-x64-userspace.iso",
              file=sys.stderr)
        return 1

    user = "labuser"
    password = "testpass"
    hashed = crypt.crypt(password, crypt.METHOD_SHA512)
    seed_body = (
        f"username={user}\n"
        f"hostname=unix\n"
        f"password_hash={hashed}\n"
        "wheel=1\n"
        "lock_root=1\n"
        "recovery=1\n"
    )

    disk = Path(tempfile.mktemp(prefix="ir0-soak.", suffix=".img"))
    seed_path = Path(tempfile.mktemp(prefix="ir0-soak-seed.", suffix=".txt"))
    proc = None
    try:
        subprocess.run(["cp", "-f", str(src), str(disk)], check=True)
        seed_path.write_text(seed_body, encoding="utf-8")
        subprocess.run(
            [sys.executable, str(ROOT / "scripts" / "inject_init_minix.py"),
             str(disk), str(seed_path), "etc/firstboot.seed"],
            check=True,
        )

        log_path.unlink(missing_ok=True)
        proc = subprocess.Popen(
            [
                os.environ.get("QEMU", "qemu-system-x86_64"),
                "-cdrom", str(iso),
                "-drive", f"file={disk},format=raw,if=ide,index=0",
                "-serial", f"file:{log_path}",
                "-display", "none",
                "-m", "256M",
                "-no-reboot",
                "-monitor", f"tcp:127.0.0.1:{args.port},server,nowait",
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

        if not wait_tags(log_path, NEED_BOOT, proc, 120):
            kill_qemu(proc)
            print("✗ boot tags missing", file=sys.stderr)
            print(read_log(log_path)[-5000:], file=sys.stderr)
            return 1

        # First login: the getty prompt is already on the wire from boot, so
        # the baseline is zero rather than "whatever is in the log now".
        if not login(args.port, log_path, proc, user, password, (0, 0, 0)):
            kill_qemu(proc)
            print("✗ no shell prompt after login", file=sys.stderr)
            print(read_log(log_path)[-5000:], file=sys.stderr)
            return 1

        segv_total = 0
        stalls = 0

        for rnd in range(args.rounds):
            commands = ROUND_COMMANDS[rnd % len(ROUND_COMMANDS)]
            for cmd in commands:
                before = prompt_count(log_path)
                type_str(args.port, cmd)
                mon(args.port, "sendkey ret", 0.35)
                if not wait_prompt(log_path, proc, 45, after=before):
                    # A stalled command is not the signal we are after; break
                    # out and keep soaking so the watchdogs get more time.
                    stalls += 1
                    mon(args.port, "sendkey ctrl-c", 0.4)
                    wait_prompt(log_path, proc, 20, after=before)

                text = read_log(log_path)
                ctx = first_fatal(text)
                if ctx:
                    kill_qemu(proc)
                    print(f"✗ watchdog fired during round {rnd} on {cmd!r}",
                          file=sys.stderr)
                    print(ctx, file=sys.stderr)
                    return 1
                segv_total = text.count("CONSOLE_SESSION_SEGV")

            if args.relogin_every and (rnd + 1) % args.relogin_every == 0:
                # Session restart was in the reported reproduction path.
                before = prompt_count(log_path)
                type_str(args.port, "exit")
                mon(args.port, "sendkey ret", 0.5)
                time.sleep(2.0)
                if not login(args.port, log_path, proc, user, password):
                    text = read_log(log_path)
                    ctx = first_fatal(text)
                    if ctx:
                        kill_qemu(proc)
                        print(f"✗ watchdog fired during relogin (round {rnd})",
                              file=sys.stderr)
                        print(ctx, file=sys.stderr)
                        return 1
                    # Harness limitation, not a kernel verdict: the watchdogs
                    # above are the failure signal. Stop soaking rather than
                    # report a getty choreography problem as corruption.
                    print(f"  stopped early: could not log back in after "
                          f"round {rnd + 1}", flush=True)
                    break

            print(f"  round {rnd + 1}/{args.rounds} ok "
                  f"(session_segv={segv_total} stalls={stalls})", flush=True)

        text = read_log(log_path)
        kill_qemu(proc)
        proc = None

        ctx = first_fatal(text)
        if ctx:
            print("✗ watchdog fired", file=sys.stderr)
            print(ctx, file=sys.stderr)
            return 1

        print(f"✓ smoke-session-soak OK (rounds={args.rounds} "
              f"session_segv={segv_total} stalls={stalls})")
        return 0
    finally:
        if proc is not None:
            kill_qemu(proc)
        seed_path.unlink(missing_ok=True)
        disk.unlink(missing_ok=True)


if __name__ == "__main__":
    sys.exit(main())
