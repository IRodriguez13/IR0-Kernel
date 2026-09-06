#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""SIGCHLD during TTY-blocked read must not corrupt the shell line editor.

Regression: foreground ls → SIGCHLD while ash blocked in read() →
USER_RESUME_KSTACK_GPR_LEAK and '[' garbage on the console.

Flow: firstboot.seed → login → ls /sys/kernel (SIGCHLD) → echo marker.
PASS: marker echoed, no panic, no KSTACK_GPR_LEAK, no '[[[' corruption.
"""
from __future__ import annotations

import crypt
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

import importlib.util

_spec = importlib.util.spec_from_file_location(
    "relogin", str(ROOT / "scripts" / "smoke_desktop_relogin.py"))
relogin = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(relogin)

read_log = relogin.read_log
kill_qemu = relogin.kill_qemu
mon = relogin.mon
type_str = relogin.type_str
wait_tags = relogin.wait_tags
login = relogin.login
NEED_BOOT = relogin.NEED_BOOT

PANIC = (
    "KERNEL PANIC",
    "invalid RIP for ring3 iretq",
    "invalid CS/SS for ring3 iretq",
)
MARKER = "SIGCHLDTTY7788"

_guards_spec = importlib.util.spec_from_file_location(
    "guards", str(ROOT / "scripts" / "smoke_tty_guards.py"))
guards = importlib.util.module_from_spec(_guards_spec)
_guards_spec.loader.exec_module(guards)


def shell_ready(port: int, log: Path, proc: subprocess.Popen[bytes]) -> bool:
    type_str(port, "true", delay=0.05)
    mon(port, "sendkey ret", 0.35)
    deadline = time.time() + 8.0
    while time.time() < deadline:
        if proc.poll() is not None:
            return False
        if any(p in read_log(log) for p in PANIC):
            return False
        time.sleep(0.2)
    return proc.poll() is None


def attempt(iso: Path, src: Path, port: int) -> int:
    user, password = "labuser", "testpass"
    hashed = crypt.crypt(password, crypt.METHOD_SHA512)
    seed_body = (
        f"username={user}\nhostname=unix\npassword_hash={hashed}\n"
        "wheel=1\nlock_root=1\nrecovery=1\n"
    )

    disk = Path(tempfile.mktemp(prefix="ir0-sigchld-tty.", suffix=".img"))
    seed = Path(tempfile.mktemp(prefix="ir0-sigchld-tty-seed.", suffix=".txt"))
    log = Path("/tmp/ir0-sigchld-tty.log")
    proc = None
    try:
        subprocess.run(["cp", "-f", str(src), str(disk)], check=True)
        seed.write_text(seed_body, encoding="utf-8")
        subprocess.run(
            [sys.executable, str(ROOT / "scripts" / "inject_init_minix.py"),
             str(disk), str(seed), "etc/firstboot.seed"],
            check=True)

        log.unlink(missing_ok=True)
        proc = subprocess.Popen(
            [os.environ.get("QEMU", "qemu-system-x86_64"),
             "-cdrom", str(iso),
             "-drive", f"file={disk},format=raw,if=ide,index=0",
             "-serial", f"file:{log}",
             "-display", "none", "-m", "256M", "-no-reboot",
             "-monitor", f"tcp:127.0.0.1:{port},server,nowait"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        if not wait_tags(log, NEED_BOOT, proc, 120):
            return 2
        wait_tags(log, ["RUNSV_LOGGER_START"], proc, 15)
        time.sleep(3.0)
        if not login(port, log, proc, user, password, 0, 0, 0):
            return 2
        time.sleep(2.0)
        if not shell_ready(port, log, proc):
            return 2

        type_str(port, "ls /sys/kernel", delay=0.05)
        mon(port, "sendkey ret", 1.0)
        time.sleep(1.5)

        type_str(port, f"echo {MARKER}", delay=0.05)
        mon(port, "sendkey ret", 0.6)

        deadline = time.time() + 15.0
        ok = False
        while time.time() < deadline:
            text = read_log(log)
            if any(p in text for p in PANIC):
                print("✗ panic", file=sys.stderr)
                print(text[-4000:], file=sys.stderr)
                return 1
            if "[[" in text and "not found" in text:
                print("✗ shell corruption ([[... not found)", file=sys.stderr)
                print(text[-4000:], file=sys.stderr)
                return 1
            if text.count(MARKER) >= 2:
                ok = True
                break
            if proc.poll() is not None:
                break
            time.sleep(0.25)

        text = read_log(log)
        errs = guards.check_typing_garbage(text, mark=0)
        if text.count("USER_RESUME_KSTACK_GPR_LEAK") > 0:
            errs.append(
                f"USER_RESUME_KSTACK_GPR_LEAK x{text.count('USER_RESUME_KSTACK_GPR_LEAK')}"
            )
        if not ok:
            errs.append("marker not echoed")
        if errs:
            return guards.report_guard_failures(errs, text[-4000:])
        print(f"✓ smoke-sigchld-tty PASS (marker OK, leaks=0)")
        return 0
    finally:
        if proc is not None:
            kill_qemu(proc)
        disk.unlink(missing_ok=True)
        seed.unlink(missing_ok=True)


def main() -> int:
    iso = Path(os.environ.get("ISO", str(ROOT / "kernel-x64-userspace.iso")))
    src = Path(os.environ.get("DISK", str(ROOT / "disk.img")))
    port = int(os.environ.get("PORT", "46940"))
    if not iso.is_file() or not src.is_file():
        print("✗ missing iso/disk", file=sys.stderr)
        return 1

    for trial in range(3):
        rc = attempt(iso, src, port + trial)
        if rc == 0:
            return 0
        if rc == 1:
            return 1
        print(f"⚠ retry {trial + 1}/3 (login/harness flake)", file=sys.stderr)
    print("✗ smoke-sigchld-tty FAILED after retries", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
