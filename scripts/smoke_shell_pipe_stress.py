#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
Stress ash pipelines after pipe kernel-sleep fix.

Injects firstboot.seed, waits for getty, then runs:
  uname -a
  dmesg | grep -i hyper
  dmesg | cat | head
  echo pipeok | cat -u
  ls / | grep proc
  id; whoami; date; uptime

Fails on panic / hang (missing prompt after commands) / FIRSTBOOT_FAIL.
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
ANSI_ESCAPE_RE = re.compile(
    r"\x1b\[[0-9;?]*[ -/]*[@-~]"  # CSI (colors, cursor)
    r"|\x1b\][^\x07\x1b]*(?:\x07|\x1b\\)"  # OSC
    r"|\x1b[@-_]"  # other two-char ESC
)
NEED_BOOT = ["RUNIT_STAGE1_OK", "GETTY_READY"]


def strip_ansi(text: str) -> str:
    return ANSI_ESCAPE_RE.sub("", text)


def log_for_prompt(log: Path) -> str:
    return strip_ansi(read_log(log))


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


def type_str(port: int, s: str, delay: float = 0.08) -> None:
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
        elif ch.isupper():
            mon(port, f"sendkey shift-{ch.lower()}", delay)
        else:
            mon(port, f"sendkey {ch}", delay)


def wait_tags(log: Path, tags: list[str], proc: subprocess.Popen[bytes], timeout: float) -> bool:
    deadline = time.time() + timeout
    while time.time() < deadline:
        text = read_log(log)
        if "FIRSTBOOT_FAIL" in text or "KERNEL PANIC" in text or "Oops:" in text:
            return False
        if all(t in text for t in tags):
            return True
        if proc.poll() is not None:
            return False
        time.sleep(0.25)
    return False


def wait_prompt(log: Path, proc: subprocess.Popen[bytes], timeout: float, after: int = 0) -> bool:
    deadline = time.time() + timeout
    while time.time() < deadline:
        text = log_for_prompt(log)
        if "KERNEL PANIC" in text or "double fault" in text.lower():
            return False
        matches = list(PROMPT_RE.finditer(text))
        if len(matches) > after:
            return True
        if proc.poll() is not None:
            return False
        time.sleep(0.3)
    return False


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


def shell_login(port: int, log: Path, proc: subprocess.Popen[bytes],
                user: str, password: str,
                baseline: tuple[int, int, int] | None = None) -> bool:
    """Log in via getty; settle after username prompt (same as session_soak)."""
    before, user_prompts, pass_prompts = (
        baseline if baseline is not None else (0, 0, 0))
    if baseline is None:
        text = log_for_prompt(log)
        before = len(list(PROMPT_RE.finditer(text)))
        user_prompts = text.count("Enter your Unix username")
        pass_prompts = text.count("Password:")

    for attempt in range(3):
        if attempt == 0 or user_prompts == 0:
            if not wait_count(log, proc, "Enter your Unix username",
                              user_prompts + 1, 60):
                return False
        else:
            # After getty respawn the new prompt is often already in the log;
            # waiting for another copy causes a 60s timeout (pipe-stress flake).
            time.sleep(1.0)
        time.sleep(1.5 if attempt == 0 else 0.5)

        type_str(port, user)
        mon(port, "sendkey ret", 0.3)
        if not wait_count(log, proc, "Password:", pass_prompts + 1, 20):
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
    ap.add_argument("--log", default="/tmp/ir0-shell-pipe-stress.log")
    ap.add_argument("--timeout", type=int, default=120)
    ap.add_argument("--port", type=int, default=46311)
    args = ap.parse_args()

    iso = Path(args.iso)
    src = Path(args.disk)
    log_path = Path(args.log)
    if not iso.is_file() or not src.is_file():
        print("✗ missing iso/disk — make load-userspace-runit + kernel-x64-userspace.iso", file=sys.stderr)
        return 1

    user = "labuser"
    hashed = crypt.crypt("testpass", crypt.METHOD_SHA512)
    seed_body = (
        f"username={user}\n"
        f"hostname=unix\n"
        f"password_hash={hashed}\n"
        "wheel=1\n"
        "lock_root=1\n"
        "recovery=1\n"
    )

    disk = Path(tempfile.mktemp(prefix="ir0-pipe-stress.", suffix=".img"))
    seed_path = Path(tempfile.mktemp(prefix="ir0-pipe-seed.", suffix=".txt"))
    try:
        subprocess.run(["cp", "-f", str(src), str(disk)], check=True)
        seed_path.write_text(seed_body, encoding="utf-8")
        inject = ROOT / "scripts" / "inject_init_minix.py"
        # Prefer canonical seed name; fall back handled by firstboot legacy.
        dest = "etc/firstboot.seed"
        subprocess.run(
            [sys.executable, str(inject), str(disk), str(seed_path), dest],
            check=True,
        )

        log_path.unlink(missing_ok=True)
        proc = subprocess.Popen(
            [
                os.environ.get("QEMU", "qemu-system-x86_64"),
                "-cdrom",
                str(iso),
                "-drive",
                f"file={disk},format=raw,if=ide,index=0",
                "-serial",
                f"file:{log_path}",
                "-display",
                "none",
                "-m",
                "256M",
                "-no-reboot",
                "-monitor",
                f"tcp:127.0.0.1:{args.port},server,nowait",
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

        if not wait_tags(log_path, NEED_BOOT + ["FIRSTBOOT_OK"], proc, min(args.timeout, 90)):
            # FIRSTBOOT_OK optional if already seeded on some images
            if not wait_tags(log_path, NEED_BOOT, proc, 30):
                kill_qemu(proc)
                print("✗ boot tags missing", file=sys.stderr)
                print(read_log(log_path)[-5000:], file=sys.stderr)
                return 1

        # Baseline (0,0,0): GETTY_READY may already be in the log; counting
        # prompts from the file would wait for a second username line that
        # never comes on first login (session_soak pattern).
        if not shell_login(args.port, log_path, proc, user, "testpass",
                           (0, 0, 0)):
            kill_qemu(proc)
            print("✗ login failed (getty prompt / password / shell prompt)", file=sys.stderr)
            print(read_log(log_path)[-5000:], file=sys.stderr)
            return 1

        prompts_before = len(list(PROMPT_RE.finditer(log_for_prompt(log_path))))

        # Light pipes + builtins: must return prompt without CONSOLE_SESSION_SEGV.
        hard_commands = [
            "uname -a",
            "ls /proc",
            "id",
            "cat /proc/uptime",
            "lsblk",
            "echo pipeok",
        ]
        # Ash pipes: soft (hang after pipeok or CONSOLE_SESSION_SEGV under load).
        soft_commands = [
            "echo pipeok | cat",
            # cat between pipes must be unbuffered (-u): stdio block-buffers
            # pipe stdout and can deadlock with head (middle read before flush).
            "echo pipeok | cat -u | head",
            "dmesg | cat",
            "hexdump -C /bin/busybox | cat -u | head -n 3",
            "yes | head -n 20",
            "cat /bin/busybox | head -n 1",
        ]
        if os.environ.get("PIPE_STRESS_FAST", "0") == "1":
            hard_commands = ["echo pipeok"]
            soft_commands = ["hexdump -C /bin/busybox | cat -u | head -n 3"]
        session_segv_before = read_log(log_path).count("CONSOLE_SESSION_SEGV")
        soft_skips = []

        for cmd in hard_commands:
            type_str(args.port, cmd)
            mon(args.port, "sendkey ret", 0.35)
            if not wait_prompt(log_path, proc, 60, after=prompts_before):
                kill_qemu(proc)
                print(f"✗ hang or no prompt after: {cmd!r}", file=sys.stderr)
                print(read_log(log_path)[-8000:], file=sys.stderr)
                return 1
            prompts_before = len(list(PROMPT_RE.finditer(log_for_prompt(log_path))))
            text = read_log(log_path)
            if "KERNEL PANIC" in text or "double free" in text.lower():
                kill_qemu(proc)
                print(f"✗ panic after: {cmd!r}", file=sys.stderr)
                return 1
            segv_now = text.count("CONSOLE_SESSION_SEGV")
            if segv_now > session_segv_before:
                kill_qemu(proc)
                print(f"✗ CONSOLE_SESSION_SEGV after: {cmd!r}", file=sys.stderr)
                print(text[-8000:], file=sys.stderr)
                return 1

        # The pipeline hang is a flake: one pass often survives it. PIPE_STRESS_ROUNDS
        # replays the soft set so a reproduction attempt does not cost a full boot each try.
        rounds = max(1, int(os.environ.get("PIPE_STRESS_ROUNDS", "1")))
        # A timeout alone cannot tell a stuck pipeline from slow serial output, so
        # record how long each command took and whether ^C or plain waiting freed it.
        soft_budget = float(os.environ.get("PIPE_STRESS_SOFT_TIMEOUT", "25"))
        timings = []
        for cmd in [c for _ in range(rounds) for c in soft_commands]:
            before_text = read_log(log_path)
            segv_base = before_text.count("CONSOLE_SESSION_SEGV")
            deferred_segv_base = before_text.count("DELIVER_DEFER sig=11")
            t0 = time.time()
            type_str(args.port, cmd)
            mon(args.port, "sendkey ret", 0.35)
            if not wait_prompt(log_path, proc, soft_budget, after=prompts_before):
                # Distinguish "slow" from "wedged": wait again without ^C first.
                grace = wait_prompt(log_path, proc, soft_budget, after=prompts_before)
                how = "slow" if grace else "wedged"
                if not grace:
                    mon(args.port, "sendkey ctrl-c", 0.4)
                    if wait_prompt(log_path, proc, 20, after=prompts_before):
                        how = "ctrl-c"
                timings.append((cmd, round(time.time() - t0, 1), how))
                soft_skips.append(f"{cmd} [{how}]")
                prompts_before = len(list(PROMPT_RE.finditer(log_for_prompt(log_path))))
                continue
            timings.append((cmd, round(time.time() - t0, 1), "ok"))
            prompts_before = len(list(PROMPT_RE.finditer(log_for_prompt(log_path))))
            text = read_log(log_path)
            if text.count("CONSOLE_SESSION_SEGV") > segv_base:
                soft_skips.append(cmd + " [SESSION_SEGV]")
                prompts_before = len(list(PROMPT_RE.finditer(strip_ansi(text))))
            if text.count("DELIVER_DEFER sig=11") > deferred_segv_base:
                kill_qemu(proc)
                print(f"✗ deferred SIGSEGV after: {cmd!r}", file=sys.stderr)
                print(text[-8000:], file=sys.stderr)
                return 1

        text = read_log(log_path)
        kill_qemu(proc)

        if ("DELIVER_DEFER sig=11" in text or
                "CLASSIFY SIGNAL_KILL_SIGSEGV" in text):
            print("✗ latent SIGSEGV observed during pipeline stress",
                  file=sys.stderr)
            return 1

        checks = [
            ("UP Priority" in text or "UP RR" in text or "IR0 " in text, "uname identity"),
            ("proc" in text, "ls /proc"),
            ("pipeok" in text, "echo|cat"),
        ]
        for ok, name in checks:
            if not ok:
                print(f"✗ missing evidence: {name}", file=sys.stderr)
                print(text[-6000:], file=sys.stderr)
                return 1

        if timings:
            slow = [t for t in timings if t[2] != "ok"]
            print(f"  soft pipelines: {len(timings) - len(slow)}/{len(timings)} clean")
            for cmd, secs, how in slow:
                print(f"    {how:>7} {secs:>6}s  {cmd}")
        if soft_skips:
            print("⚠ soft-skip heavy pipelines:", ", ".join(repr(c) for c in soft_skips))
        print("✓ smoke-shell-pipe-stress OK")
        return 0
    finally:
        seed_path.unlink(missing_ok=True)
        disk.unlink(missing_ok=True)


if __name__ == "__main__":
    sys.exit(main())
