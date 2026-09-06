#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Desktop re-login after exec ls (blocked TTY read resume).

Repro: first session → `exec ls` (logout) → type user + password again.
PASS: second LOGIN_OK / shell prompt; no USER_FAULT_FRAME / CONSOLE_SESSION_SEGV
around the password read.

Uses a firstboot.seed so the harness skips the interactive wizard.
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

FATAL = (
    "KERNEL PANIC",
    "KERNEL_UACCESS_FAULT",
    "USER_FAULT_FRAME",
    "CONSOLE_SESSION_SEGV",
    "FILES_STRUCT_BAD",
)


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
        if any(f in text for f in FATAL):
            return False
        if all(t in text for t in tags):
            return True
        if proc.poll() is not None:
            return False
        time.sleep(0.25)
    return False


def wait_count(log: Path, proc: subprocess.Popen[bytes], needle: str,
               want: int, timeout: float) -> bool:
    deadline = time.time() + timeout
    while time.time() < deadline:
        text = read_log(log)
        if any(f in text for f in FATAL):
            return False
        if text.count(needle) >= want:
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
        if any(f in text for f in FATAL):
            return False
        if len(list(PROMPT_RE.finditer(text))) > after:
            return True
        if proc.poll() is not None:
            return False
        time.sleep(0.3)
    return False


def login(port: int, log: Path, proc: subprocess.Popen[bytes], user: str,
          password: str, user_prompts: int, pass_prompts: int,
          prompts_before: int) -> bool:
    for _ in range(3):
        if not wait_count(log, proc, "Enter your Unix username",
                          user_prompts + 1, 60):
            return False
        time.sleep(1.5)
        type_str(port, user)
        mon(port, "sendkey ret", 0.35)
        if not wait_count(log, proc, "Password:", pass_prompts + 1, 25):
            text = read_log(log)
            user_prompts = text.count("Enter your Unix username")
            pass_prompts = text.count("Password:")
            continue
        type_str(port, password)
        mon(port, "sendkey ret", 0.45)
        if wait_prompt(log, proc, 60, after=prompts_before):
            return True
        text = read_log(log)
        user_prompts = text.count("Enter your Unix username")
        pass_prompts = text.count("Password:")
        prompts_before = len(list(PROMPT_RE.finditer(text)))
    return False


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--iso", default=str(ROOT / "kernel-x64-userspace.iso"))
    ap.add_argument("--disk", default=str(ROOT / "disk.img"))
    ap.add_argument("--log", default="/tmp/ir0-desktop-relogin.log")
    ap.add_argument("--port", type=int, default=46327)
    args = ap.parse_args()

    iso = Path(args.iso)
    src = Path(args.disk)
    log_path = Path(args.log)
    if not iso.is_file() or not src.is_file():
        print("✗ missing iso/disk", file=sys.stderr)
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

    disk = Path(tempfile.mktemp(prefix="ir0-relogin.", suffix=".img"))
    seed_path = Path(tempfile.mktemp(prefix="ir0-relogin-seed.", suffix=".txt"))
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
            print("✗ boot tags missing", file=sys.stderr)
            print(read_log(log_path)[-6000:], file=sys.stderr)
            return 1

        if not login(args.port, log_path, proc, user, password, 0, 0, 0):
            print("✗ first login failed", file=sys.stderr)
            print(read_log(log_path)[-6000:], file=sys.stderr)
            return 1

        text = read_log(log_path)
        prompts = len(list(PROMPT_RE.finditer(text)))
        ends = text.count("CONSOLE_SESSION_END")
        # Sample *before* exec ls so login() waits for the post-logout prompt.
        user_base = text.count("Enter your Unix username")
        pass_base = text.count("Password:")
        login_ok = text.count("LOGIN_OK")

        type_str(args.port, "exec ls")
        mon(args.port, "sendkey ret", 0.4)

        if not wait_count(log_path, proc, "CONSOLE_SESSION_END", ends + 1, 30):
            print("✗ exec ls did not end the session", file=sys.stderr)
            print(read_log(log_path)[-6000:], file=sys.stderr)
            return 1

        time.sleep(1.0)
        text = read_log(log_path)
        for tag in FATAL:
            if text.count(tag) > 0:
                print(f"✗ fatal after logout: {tag}", file=sys.stderr)
                print(text[-6000:], file=sys.stderr)
                return 1

        if not login(args.port, log_path, proc, user, password,
                     user_base, pass_base, prompts):
            print("✗ re-login after exec ls failed", file=sys.stderr)
            print(read_log(log_path)[-8000:], file=sys.stderr)
            return 1

        text = read_log(log_path)
        for tag in FATAL:
            if tag in text:
                print(f"✗ fatal after re-login: {tag}", file=sys.stderr)
                print(text[-8000:], file=sys.stderr)
                return 1

        if text.count("LOGIN_OK") < login_ok + 1:
            print("✗ missing second LOGIN_OK", file=sys.stderr)
            print(text[-6000:], file=sys.stderr)
            return 1

        leaks = text.count("USER_RESUME_KSTACK_GPR_LEAK")
        if leaks > 0:
            print(f"✗ USER_RESUME_KSTACK_GPR_LEAK x{leaks}", file=sys.stderr)
            print(text[-6000:], file=sys.stderr)
            return 1

        print("✓ smoke-desktop-relogin PASS")
        return 0
    finally:
        if proc is not None:
            kill_qemu(proc)
        disk.unlink(missing_ok=True)
        seed_path.unlink(missing_ok=True)


if __name__ == "__main__":
    sys.exit(main())
