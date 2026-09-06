#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""SIGCHLD during interactive shell must not voluntary-logout (REPROMPT).

Regression: innocuous SIGCHLD (ps, ls) aborted lineedit with errno==0 → ash
exitshell → CONSOLE_SESSION_END+REPROMPT while the user still sees a shell.
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
FATAL = relogin.FATAL

MARKER = "SIGCHLDNOLOGOUT42"


def main() -> int:
    iso = Path(os.environ.get("ISO", str(ROOT / "kernel-x64-userspace.iso")))
    src = Path(os.environ.get("DISK", str(ROOT / "disk.img")))
    log = Path("/tmp/ir0-sigchld-no-false-logout.log")
    port = int(os.environ.get("PORT", "46745"))
    if not iso.is_file() or not src.is_file():
        print("✗ missing iso/disk", file=sys.stderr)
        return 1

    user, password = "labuser", "testpass"
    hashed = crypt.crypt(password, crypt.METHOD_SHA512)
    seed_body = (
        f"username={user}\nhostname=unix\npassword_hash={hashed}\n"
        "wheel=1\nlock_root=1\nrecovery=1\n"
    )

    disk = Path(tempfile.mktemp(prefix="ir0-sigchld-nolog.", suffix=".img"))
    seed = Path(tempfile.mktemp(prefix="ir0-sigchld-nolog-seed.", suffix=".txt"))
    proc = None
    try:
        subprocess.run(["cp", "-f", str(src), str(disk)], check=True)
        seed.write_text(seed_body, encoding="utf-8")
        subprocess.run(
            [sys.executable, str(ROOT / "scripts" / "inject_init_minix.py"),
             str(disk), str(seed), "etc/firstboot.seed"], check=True)

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
            print("✗ boot tags missing", file=sys.stderr)
            return 1
        wait_tags(log, ["RUNSV_LOGGER_START"], proc, 15)
        time.sleep(3.0)

        if not login(port, log, proc, user, password, 0, 0, 0):
            print("✗ login failed", file=sys.stderr)
            return 1

        text = read_log(log)
        reprompt_base = text.count("CONSOLE_SESSION_REPROMPT")
        end_base = text.count("CONSOLE_SESSION_END")
        mark = len(text)

        for cmd in ("ps", "ls /sys/kernel", "true", "ps"):
            type_str(port, cmd, delay=0.04)
            mon(port, "sendkey ret", 0.5)
            time.sleep(0.4)

        # Idle at prompt after cd/ls (user report: SIGCHLD during read → false logout)
        type_str(port, "cd /tmp", delay=0.04)
        mon(port, "sendkey ret", 0.5)
        time.sleep(0.3)
        type_str(port, "ls", delay=0.04)
        mon(port, "sendkey ret", 0.5)
        time.sleep(2.5)

        type_str(port, f"echo {MARKER}", delay=0.05)
        mon(port, "sendkey ret", 0.8)
        time.sleep(0.6)

        text = read_log(log)
        for tag in FATAL:
            if tag in text:
                print(f"✗ fatal: {tag}", file=sys.stderr)
                return 1
        if text.count("CONSOLE_SESSION_REPROMPT") > reprompt_base:
            print("✗ false REPROMPT after SIGCHLD-heavy cmds", file=sys.stderr)
            print(text[mark:][-3000:], file=sys.stderr)
            return 1
        if text.count("CONSOLE_SESSION_END") > end_base:
            print("✗ false SESSION_END after SIGCHLD-heavy cmds", file=sys.stderr)
            return 1
        if MARKER not in text[mark:]:
            print("✗ shell lost after SIGCHLD cmds", file=sys.stderr)
            return 1
        if "USER_RESUME_KSTACK_GPR_LEAK" in text[mark:]:
            print("✗ GPR leak", file=sys.stderr)
            return 1

        print("✓ smoke-sigchld-no-false-logout PASS")
        return 0
    finally:
        if proc is not None:
            kill_qemu(proc)
        disk.unlink(missing_ok=True)
        seed.unlink(missing_ok=True)


if __name__ == "__main__":
    sys.exit(main())
