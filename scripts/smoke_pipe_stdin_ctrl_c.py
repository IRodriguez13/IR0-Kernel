#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Pipeline blocked on stdin read must respond to Ctrl+C after SIGCHLD.

Regression: ./executable → SIGCHLD → hexdump -C | grep ELF → ^C dead.
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

MARKER = "PIPESTDININTOK"

_guards_spec = importlib.util.spec_from_file_location(
    "guards", str(ROOT / "scripts" / "smoke_tty_guards.py"))
guards = importlib.util.module_from_spec(_guards_spec)
_guards_spec.loader.exec_module(guards)


def run_cmd(port: int, cmd: str, pause: float = 0.5) -> None:
    type_str(port, cmd, delay=0.06)
    mon(port, "sendkey ret", pause)
    time.sleep(pause * 0.85)


def main() -> int:
    iso = Path(os.environ.get("ISO", str(ROOT / "kernel-x64-userspace.iso")))
    src = Path(os.environ.get("DISK", str(ROOT / "disk.img")))
    log = Path("/tmp/ir0-pipe-stdin-ctrl-c.log")
    port = int(os.environ.get("PORT", "46747"))
    if not iso.is_file() or not src.is_file():
        print("✗ missing iso/disk", file=sys.stderr)
        return 1

    user, password = "labuser", "testpass"
    hashed = crypt.crypt(password, crypt.METHOD_SHA512)
    seed_body = (
        f"username={user}\nhostname=unix\npassword_hash={hashed}\n"
        "wheel=1\nlock_root=1\nrecovery=1\n"
    )

    disk = Path(tempfile.mktemp(prefix="ir0-pipe-intr.", suffix=".img"))
    seed = Path(tempfile.mktemp(prefix="ir0-pipe-intr-seed.", suffix=".txt"))
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

        # Short-lived fg job → SIGCHLD at prompt (user's ./executable pattern).
        run_cmd(port, "/bin/true", pause=0.35)
        time.sleep(0.4)

        mark = len(read_log(log))
        type_str(port, "hexdump -C | grep ELF", delay=0.08)
        mon(port, "sendkey ret", 0.3)
        time.sleep(1.5)
        mon(port, "sendkey ctrl-c", 0.15)
        time.sleep(1.0)
        type_str(port, f"echo {MARKER}", delay=0.06)
        mon(port, "sendkey ret", 0.4)
        time.sleep(0.8)

        text = read_log(log)
        errs = guards.check_typing_garbage(text, mark=mark)
        if MARKER not in text[mark:]:
            errs.append("shell did not recover after ^C on hexdump|grep")
        if errs:
            return guards.report_guard_failures(errs, text[mark:])
        print("✓ smoke-pipe-stdin-ctrl-c PASS")
        return 0
    finally:
        if proc is not None:
            kill_qemu(proc)
        disk.unlink(missing_ok=True)
        seed.unlink(missing_ok=True)


if __name__ == "__main__":
    sys.exit(main())
