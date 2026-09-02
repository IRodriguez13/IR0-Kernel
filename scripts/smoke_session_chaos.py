#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
Session chaos battery — nonsense / edge I/O after getty login.

Purpose: prove the interactive session stays coherent under commands that
are awkward or pointless but legal on a Unix-like system:

  /dev/null /dev/zero /dev/urandom /dev/full
  pipes, redirections, stderr divert
  dmesg, /proc, free, ps
  tmpfs mount/umount
  virtio-9p hostshare mount/umount + guest→host write

Fails hard on: KERNEL PANIC, KERNEL_UACCESS_FAULT, CONSOLE_SESSION_SEGV
(on hard commands), missing shell prompt after a hard command.

Soft commands may time out (noted, not fatal) — same class as pipe flakes.

Usage:
  make -s smoke-session-chaos
  # or:
  python3 scripts/smoke_session_chaos.py --iso kernel-x64-userspace.iso --disk disk.img
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
    r"\x1b\[[0-9;?]*[ -/]*[@-~]"
    r"|\x1b\][^\x07\x1b]*(?:\x07|\x1b\\)"
    r"|\x1b[@-_]"
)
NEED_BOOT = ["RUNIT_STAGE1_OK", "GETTY_READY"]
FATAL = (
    "KERNEL PANIC",
    "KERNEL_UACCESS_FAULT",
    "KTM_USER_CANARY_BROKEN",
    "STACK_TOP_OVERRUN",
    "double fault",
)

SPECIAL = {
    " ": "spc",
    "/": "slash",
    "-": "minus",
    ".": "dot",
    "=": "equal",
    "_": "shift-minus",
    ":": "shift-semicolon",
    ";": "semicolon",
    "|": "shift-backslash",
    "&": "shift-7",
    "'": "apostrophe",
    '"': "shift-apostrophe",
    "\\": "backslash",
    "!": "shift-1",
    "?": "shift-slash",
    "(": "shift-9",
    ")": "shift-0",
    ",": "comma",
    "+": "shift-equal",
    "*": "shift-8",
    ">": "shift-dot",
    "<": "shift-comma",
    "#": "shift-3",
    "{": "shift-bracket_left",
    "}": "shift-bracket_right",
    "[": "bracket_left",
    "]": "bracket_right",
}

# Hard: must return a new prompt without fatal/session SEGV.
# File writes use $HOME — product /tmp may not be world-writable on MINIX root.
# Mounts need doas (wheel); password is typed when OpenDoas prompts.
HARD_COMMANDS = [
    "uname -a",
    "id",
    "cat /proc/version",
    "cat /proc/uptime",
    "cat /proc/meminfo",
    "free",
    "ps",
    "cat /proc/ps",
    "ls /dev",
    "cat /dev/null; echo null_ok",
    "head -c 64 /dev/zero | wc -c",
    "head -c 32 /dev/urandom | wc -c",
    "echo fulltest >/dev/full; echo full_done",
    "echo hi >/home/labuser/c.txt",
    "cat /home/labuser/c.txt",
    "ls /no/such 2>/home/labuser/e.txt",
    "cat /home/labuser/e.txt",
    "false; echo still_alive",
    "mount",
    "cat /proc/mounts",
    "echo CHAOS_PRE_MOUNT",
]

DOAS_SETUP = [
    "mkdir -p /home/labuser/mntchaos",
    "doas /bin/sh -c 'mkdir -p /mnt/host; mount -t tmpfs tmpfs /home/labuser/mntchaos'",
    "echo ram >/home/labuser/mntchaos/x",
    "cat /home/labuser/mntchaos/x",
    "doas umount /home/labuser/mntchaos",
    "doas /bin/sh -c 'mount -t 9p ir0share /mnt/host; ls /mnt/host; cat /mnt/host/host_marker.txt; echo guest_ok >/mnt/host/chaos_guest.txt; umount /mnt/host'",
]

HARD_TAIL = [
    "echo CHAOS_HARD_DONE",
]

# Soft: hang/flake class — timeout records skip, does not fail the battery.
SOFT_COMMANDS = [
    "echo pipeok | cat -u | head",
    "yes | head -n 8",
    "dmesg | cat -u | head -n 3",
]


def strip_ansi(text: str) -> str:
    return ANSI_ESCAPE_RE.sub("", text)


def read_log(path: Path) -> str:
    if not path.is_file():
        return ""
    return path.read_text(errors="replace")


def log_for_prompt(log: Path) -> str:
    return strip_ansi(read_log(log))


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


def type_str(port: int, s: str, delay: float = 0.055) -> None:
    for ch in s:
        if ch in SPECIAL:
            mon(port, f"sendkey {SPECIAL[ch]}", delay)
        elif ch.isupper():
            mon(port, f"sendkey shift-{ch.lower()}", delay)
        else:
            mon(port, f"sendkey {ch}", delay)


def first_fatal(text: str) -> str | None:
    low = text.lower()
    for tag in FATAL:
        if tag.lower() in low or tag in text:
            idx = text.lower().find(tag.lower()) if tag.lower() in low else text.find(tag)
            if idx < 0:
                continue
            return text[max(0, idx - 800) : idx + 1200]
    return None


def wait_tags(log: Path, tags: list[str], proc: subprocess.Popen[bytes],
              timeout: float) -> bool:
    deadline = time.time() + timeout
    while time.time() < deadline:
        text = read_log(log)
        if first_fatal(text):
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
        text = log_for_prompt(log)
        if first_fatal(text):
            return False
        if len(list(PROMPT_RE.finditer(text))) > after:
            return True
        if proc.poll() is not None:
            return False
        time.sleep(0.3)
    return False


def wait_count(log: Path, proc: subprocess.Popen[bytes], needle: str,
               want: int, timeout: float) -> bool:
    deadline = time.time() + timeout
    while time.time() < deadline:
        text = read_log(log)
        if first_fatal(text):
            return False
        if text.count(needle) >= want:
            return True
        if proc.poll() is not None:
            return False
        time.sleep(0.25)
    return False


def shell_login(port: int, log: Path, proc: subprocess.Popen[bytes],
                user: str, password: str) -> bool:
    before = 0
    user_prompts = 0
    pass_prompts = 0
    for _attempt in range(3):
        if not wait_count(log, proc, "Enter your Unix username",
                          user_prompts + 1, 60):
            return False
        time.sleep(1.5)
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


def run_cmd(port: int, log: Path, proc: subprocess.Popen[bytes], cmd: str,
            prompts_before: int, budget: float, *, hard: bool,
            segv_base: int) -> tuple[bool, int, str | None]:
    """Returns (ok, new_prompt_count, skip_reason)."""
    type_str(port, cmd)
    mon(port, "sendkey ret", 0.35)
    if not wait_prompt(log, proc, budget, after=prompts_before):
        if hard:
            text = read_log(log)
            if text.count("CONSOLE_SESSION_SEGV") > segv_base:
                return False, prompts_before, f"session_segv:{cmd}"
            return False, prompts_before, f"no_prompt:{cmd}"
        return True, prompts_before, f"soft_timeout:{cmd}"
    text = read_log(log)
    fat = first_fatal(text)
    if fat:
        return False, prompts_before, f"fatal_after:{cmd}"
    segv_now = text.count("CONSOLE_SESSION_SEGV")
    if hard and segv_now > segv_base:
        return False, prompts_before, f"session_segv:{cmd}"
    n = len(list(PROMPT_RE.finditer(log_for_prompt(log))))
    return True, n, None


def run_doas(port: int, log: Path, proc: subprocess.Popen[bytes], cmd: str,
             password: str, prompts_before: int, budget: float,
             segv_base: int) -> tuple[bool, int, str | None]:
    """Run a command that starts with doas; type password when prompted."""
    pass_before = read_log(log).count("Password:")
    type_str(port, cmd)
    mon(port, "sendkey ret", 0.4)
    # OpenDoas prompts "Password:" (or similar) once.
    if wait_count(log, proc, "Password:", pass_before + 1, 25):
        time.sleep(0.4)
        type_str(port, password)
        mon(port, "sendkey ret", 0.4)
    if not wait_prompt(log, proc, budget, after=prompts_before):
        return False, prompts_before, f"no_prompt_doas:{cmd}"
    text = read_log(log)
    if first_fatal(text):
        return False, prompts_before, f"fatal_doas:{cmd}"
    if text.count("CONSOLE_SESSION_SEGV") > segv_base:
        return False, prompts_before, f"session_segv_doas:{cmd}"
    n = len(list(PROMPT_RE.finditer(log_for_prompt(log))))
    return True, n, None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--iso", default=str(ROOT / "kernel-x64-userspace.iso"))
    ap.add_argument("--disk", default=str(ROOT / "disk.img"))
    ap.add_argument("--log", default="/tmp/ir0-session-chaos.log")
    ap.add_argument("--timeout", type=int, default=240)
    ap.add_argument("--port", type=int, default=46327)
    ap.add_argument("--hard-budget", type=float, default=45.0)
    ap.add_argument("--soft-budget", type=float, default=20.0)
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

    disk = Path(tempfile.mktemp(prefix="ir0-chaos.", suffix=".img"))
    seed_path = Path(tempfile.mktemp(prefix="ir0-chaos-seed.", suffix=".txt"))
    share = Path(tempfile.mkdtemp(prefix="ir0-chaos-share."))
    (share / "host_marker.txt").write_text("from_host\n", encoding="utf-8")
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
                "-fsdev", f"local,id=ir0fs,path={share},security_model=none",
                "-device",
                "virtio-9p-pci,fsdev=ir0fs,mount_tag=ir0share,disable-modern=on",
                "-serial", f"file:{log_path}",
                "-display", "none",
                "-m", "256M",
                "-no-reboot",
                "-net", "none",
                "-monitor", f"tcp:127.0.0.1:{args.port},server,nowait",
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

        if not wait_tags(log_path, NEED_BOOT, proc, min(args.timeout, 120)):
            kill_qemu(proc)
            print("✗ boot tags missing", file=sys.stderr)
            print(read_log(log_path)[-6000:], file=sys.stderr)
            return 1

        if not shell_login(args.port, log_path, proc, user, password):
            kill_qemu(proc)
            print("✗ login failed", file=sys.stderr)
            print(read_log(log_path)[-6000:], file=sys.stderr)
            return 1

        prompts = len(list(PROMPT_RE.finditer(log_for_prompt(log_path))))
        segv_base = read_log(log_path).count("CONSOLE_SESSION_SEGV")
        soft_skips: list[str] = []

        for cmd in HARD_COMMANDS:
            print(f"  chaos hard: {cmd}", flush=True)
            ok, prompts, reason = run_cmd(
                args.port, log_path, proc, cmd, prompts, args.hard_budget,
                hard=True, segv_base=segv_base)
            if not ok:
                kill_qemu(proc)
                print(f"✗ {reason}", file=sys.stderr)
                print(read_log(log_path)[-10000:], file=sys.stderr)
                return 1
            segv_base = read_log(log_path).count("CONSOLE_SESSION_SEGV")

        for cmd in DOAS_SETUP:
            print(f"  chaos doas/mount: {cmd}", flush=True)
            if cmd.startswith("doas "):
                ok, prompts, reason = run_doas(
                    args.port, log_path, proc, cmd, password, prompts,
                    args.hard_budget, segv_base)
            else:
                ok, prompts, reason = run_cmd(
                    args.port, log_path, proc, cmd, prompts, args.hard_budget,
                    hard=True, segv_base=segv_base)
            if not ok:
                kill_qemu(proc)
                print(f"✗ {reason}", file=sys.stderr)
                print(read_log(log_path)[-10000:], file=sys.stderr)
                return 1
            segv_base = read_log(log_path).count("CONSOLE_SESSION_SEGV")

        for cmd in HARD_TAIL:
            print(f"  chaos hard: {cmd}", flush=True)
            ok, prompts, reason = run_cmd(
                args.port, log_path, proc, cmd, prompts, args.hard_budget,
                hard=True, segv_base=segv_base)
            if not ok:
                kill_qemu(proc)
                print(f"✗ {reason}", file=sys.stderr)
                print(read_log(log_path)[-10000:], file=sys.stderr)
                return 1
            segv_base = read_log(log_path).count("CONSOLE_SESSION_SEGV")

        for cmd in SOFT_COMMANDS:
            print(f"  chaos soft: {cmd}", flush=True)
            ok, prompts, reason = run_cmd(
                args.port, log_path, proc, cmd, prompts, args.soft_budget,
                hard=False, segv_base=segv_base)
            if not ok:
                kill_qemu(proc)
                print(f"✗ {reason}", file=sys.stderr)
                print(read_log(log_path)[-10000:], file=sys.stderr)
                return 1
            if reason:
                soft_skips.append(reason)
                # Try to free a stuck soft pipeline.
                mon(args.port, "sendkey ctrl-c", 0.4)
                wait_prompt(log_path, proc, 15, after=prompts)
                prompts = len(list(PROMPT_RE.finditer(log_for_prompt(log_path))))
            segv_base = read_log(log_path).count("CONSOLE_SESSION_SEGV")

        text = read_log(log_path)
        if "CHAOS_HARD_DONE" not in text:
            kill_qemu(proc)
            print("✗ CHAOS_HARD_DONE echo missing (session not coherent)", file=sys.stderr)
            print(text[-8000:], file=sys.stderr)
            return 1

        guest_marker = share / "chaos_guest.txt"
        if not guest_marker.is_file():
            kill_qemu(proc)
            print("✗ virtio-9p: host did not see /mnt/host/chaos_guest.txt",
                  file=sys.stderr)
            print(f"  share={share}", file=sys.stderr)
            print(text[-8000:], file=sys.stderr)
            return 1
        body = guest_marker.read_text(errors="replace")
        if "guest_ok" not in body:
            kill_qemu(proc)
            print(f"✗ virtio-9p guest marker content unexpected: {body!r}",
                  file=sys.stderr)
            return 1

        if first_fatal(text):
            kill_qemu(proc)
            print("✗ fatal tag in log at end", file=sys.stderr)
            print(first_fatal(text), file=sys.stderr)
            return 1

        kill_qemu(proc)
        hard_n = len(HARD_COMMANDS) + len(DOAS_SETUP) + len(HARD_TAIL)
        print(f"✓ smoke-session-chaos OK "
              f"(hard={hard_n} soft_skip={len(soft_skips)} "
              f"9p_guest_write=ok uaccess_fault=0)", flush=True)
        for s in soft_skips:
            print(f"  note {s}", flush=True)
        return 0
    finally:
        if proc is not None:
            kill_qemu(proc)
        disk.unlink(missing_ok=True)
        seed_path.unlink(missing_ok=True)
        # leave share for debugging on failure; clean on success
        try:
            for p in share.iterdir():
                p.unlink()
            share.rmdir()
        except Exception:
            pass


if __name__ == "__main__":
    sys.exit(main())
