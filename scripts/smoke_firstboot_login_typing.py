#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Interactive firstboot → login → type ASCII commands (no seed).

Catches the user-reported path:
  Account created → login → typed `llss` shows as `êllss` → Invalid argument
  → run crash (KERNEL_UACCESS_FAULT) → RUNSV_CONSOLE_START.

Must PASS with clean 7-bit echo on every typed line (3 trials).
"""

from __future__ import annotations

import argparse
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

import importlib.util

_guards_spec = importlib.util.spec_from_file_location(
    "guards", str(ROOT / "scripts" / "smoke_tty_guards.py"))
guards = importlib.util.module_from_spec(_guards_spec)
_guards_spec.loader.exec_module(guards)

SPECIAL = {
    " ": "spc",
    "/": "slash",
    "-": "minus",
    ".": "dot",
    "_": "shift-minus",
}

FAIL_RE = re.compile(
    r"KERNEL PANIC|#UD\b|Unhandled kernel|KERNEL_EXECUTE_BSS|DOUBLE FAULT"
)
USER_PROMPT = re.compile(r"Enter your Unix username:")
HOST_PROMPT = re.compile(r"Hostname")
LAYOUT_PROMPT = re.compile(r"Keyboard layout")
PASS_PROMPT = re.compile(r"Password:")
CONFIRM_PROMPT = re.compile(r"Confirm password:")
ACCOUNT_CREATED = re.compile(r"Account '.+' created")
LOGIN_PROMPT = re.compile(r"Enter your Unix username:|login:")
PROMPT_RE = re.compile(r"[a-zA-Z0-9_-]+@[a-zA-Z0-9_-]+:\S*[#$]")

MARKER = "FBTYPEOK"
COMMANDS = ("ls", f"echo {MARKER}", "pwd", "llss", "ll")
TRIALS = 3


def mon_send(port: int, cmd: str, wait: float = 0.05) -> None:
    with socket.create_connection(("127.0.0.1", port), timeout=5) as s:
        s.settimeout(0.4)
        try:
            s.recv(4096)
        except OSError:
            pass
        s.sendall((cmd + "\n").encode())
        time.sleep(wait)
        try:
            while s.recv(4096):
                pass
        except OSError:
            pass


def type_str(port: int, text: str, delay: float = 0.10, enter: bool = True) -> None:
    for ch in text:
        if ch in SPECIAL:
            mon_send(port, f"sendkey {SPECIAL[ch]}")
        elif ch.isupper():
            mon_send(port, f"sendkey shift-{ch.lower()}")
        else:
            mon_send(port, f"sendkey {ch}")
        time.sleep(delay)
    if enter:
        mon_send(port, "sendkey ret", 0.35)


def read_log(path: Path) -> str:
    return path.read_text(errors="replace") if path.is_file() else ""


def wait_re(log: Path, rx: re.Pattern[str], proc: subprocess.Popen, limit: float) -> str:
    t0 = time.time()
    while time.time() - t0 < limit:
        text = read_log(log)
        if FAIL_RE.search(text):
            return text
        if rx.search(text):
            return text
        if proc.poll() is not None:
            return text
        time.sleep(0.2)
    return read_log(log)


def wait_prompt(log: Path, proc: subprocess.Popen, limit: float) -> bool:
    t0 = time.time()
    while time.time() - t0 < limit:
        text = read_log(log)
        if FAIL_RE.search(text):
            return False
        if PROMPT_RE.search(text):
            return True
        if proc.poll() is not None:
            return False
        time.sleep(0.25)
    return False


def run_trial(args: argparse.Namespace, trial: int) -> int:
    disk = Path(tempfile.mktemp(prefix=f"ir0-fb-type-{trial}.", suffix=".img"))
    port = args.port + trial
    shutil.copy2(args.disk, disk)
    args.log.write_text("")
    subprocess.run(
        ["pkill", "-f", f"qemu-system-x86_64.*127.0.0.1:{port}"],
        check=False,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    time.sleep(0.3)

    proc = subprocess.Popen(
        [
            "qemu-system-x86_64",
            "-cdrom", str(args.iso),
            "-drive", f"file={disk},format=raw,if=ide,index=0",
            "-serial", f"file:{args.log}",
            "-display", "none", "-m", "512M", "-no-reboot", "-net", "none",
            "-monitor", f"tcp:127.0.0.1:{port},server,nowait",
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    end_base = 0
    try:
        text = wait_re(args.log, USER_PROMPT, proc, min(90.0, args.timeout))
        if FAIL_RE.search(text):
            print(f"✗ trial {trial + 1}: panic before firstboot username", file=sys.stderr)
            return 1
        if not USER_PROMPT.search(text):
            print(f"✗ trial {trial + 1}: timeout firstboot username", file=sys.stderr)
            return 1

        time.sleep(1.0)
        type_str(port, args.user)
        text = wait_re(args.log, HOST_PROMPT, proc, 40.0)
        type_str(port, "unix")
        text = wait_re(args.log, LAYOUT_PROMPT, proc, 40.0)
        type_str(port, "us")
        text = wait_re(args.log, PASS_PROMPT, proc, 40.0)
        type_str(port, args.password)
        text = wait_re(args.log, CONFIRM_PROMPT, proc, 40.0)
        type_str(port, args.password)
        text = wait_re(args.log, ACCOUNT_CREATED, proc, 60.0)
        if not ACCOUNT_CREATED.search(text):
            print(f"✗ trial {trial + 1}: firstboot wizard did not finish", file=sys.stderr)
            return 1

        end_base = text.count("CONSOLE_SESSION_END")
        mark_wizard = len(text)

        errs = guards.check_typing_garbage(text, mark=mark_wizard)
        if errs:
            print(f"✗ trial {trial + 1} post-wizard:", file=sys.stderr)
            return guards.report_guard_failures(errs, text)

        text = wait_re(args.log, LOGIN_PROMPT, proc, 45.0)
        time.sleep(0.8)
        type_str(port, args.user)
        text = wait_re(args.log, PASS_PROMPT, proc, 30.0)
        type_str(port, args.password)

        if not wait_prompt(args.log, proc, 45.0):
            print(f"✗ trial {trial + 1}: shell prompt missing after login", file=sys.stderr)
            print(read_log(args.log)[-4000:], file=sys.stderr)
            return 1

        # Foreground job → SIGCHLD (user's ./true / manual path).
        time.sleep(0.6)
        type_str(port, "/bin/true", delay=0.08)
        time.sleep(0.9)
        if not wait_prompt(args.log, proc, 20.0):
            print(f"✗ trial {trial + 1}: prompt missing after /bin/true", file=sys.stderr)
            return 1

        mark = len(read_log(args.log))
        for cmd in COMMANDS:
            type_str(port, cmd, delay=0.08)
            time.sleep(0.75)
            if cmd.startswith("echo "):
                deadline = time.time() + 12.0
                while time.time() < deadline:
                    if MARKER in read_log(args.log)[mark:]:
                        break
                    if proc.poll() is not None:
                        break
                    time.sleep(0.2)
            elif not wait_prompt(args.log, proc, 12.0):
                print(f"✗ trial {trial + 1}: prompt missing after {cmd!r}", file=sys.stderr)
                return 1

        mon_send(port, "sendkey ctrl-c", 0.12)
        time.sleep(0.35)
        type_str(port, "ls", delay=0.08)
        time.sleep(0.75)

        text = read_log(args.log)
        errs = guards.check_typing_garbage(text, mark=mark)
        ab = guards.check_abnormal_session_end(text, end_base)
        if ab:
            errs.append(ab)
        if MARKER not in text[mark:]:
            errs.append(f"marker {MARKER} missing after typing commands")
        if errs:
            print(f"✗ trial {trial + 1} session typing:", file=sys.stderr)
            return guards.report_guard_failures(errs, text[mark:])

        return 0
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except Exception:
                proc.kill()
        disk.unlink(missing_ok=True)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--iso", type=Path, default=ROOT / "kernel-x64-userspace.iso")
    ap.add_argument("--disk", type=Path, default=ROOT / "disk.img")
    ap.add_argument("--log", type=Path, default=Path("/tmp/ir0-firstboot-login-typing.log"))
    ap.add_argument("--port", type=int, default=46207)
    ap.add_argument("--timeout", type=float, default=180.0)
    ap.add_argument("--user", default="ivan")
    ap.add_argument("--password", default="ivan")
    ap.add_argument("--trials", type=int, default=TRIALS)
    args = ap.parse_args()

    if not args.iso.is_file() or not args.disk.is_file():
        print(f"✗ need iso+disk: {args.iso} {args.disk}", file=sys.stderr)
        return 2

    for trial in range(args.trials):
        rc = run_trial(args, trial)
        if rc != 0:
            print(f"✗ smoke-firstboot-login-typing FAILED trial {trial + 1}/{args.trials}",
                  file=sys.stderr)
            return rc
        if trial + 1 < args.trials:
            print(f"  trial {trial + 1}/{args.trials} OK", file=sys.stderr)

    print(f"✓ smoke-firstboot-login-typing PASS "
          f"(trials={args.trials}, commands={len(COMMANDS)}, marker OK, no TTY garbage)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
