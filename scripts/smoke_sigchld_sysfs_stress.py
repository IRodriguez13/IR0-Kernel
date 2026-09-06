#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Stress SIGCHLD + sysfs/cat-on-directory during interactive ash.

Regression (user report): cat /sys/class/net (directory) → SIGCHLD during
prompt read → ash exitshell(last_status=1) → CONSOLE_SESSION_END abnormal
→ supervisor restarts shell without login.

Replays: sysfs walks, repeated cat on directories, idle at prompt.
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

MARKER = "SYSFSSIGCHLDSTRESSOK"
ABNORMAL = "shell exited abnormally"

_guards_spec = importlib.util.spec_from_file_location(
    "guards", str(ROOT / "scripts" / "smoke_tty_guards.py"))
guards = importlib.util.module_from_spec(_guards_spec)
_guards_spec.loader.exec_module(guards)


def run_cmd(port: int, cmd: str, pause: float = 0.55) -> None:
    type_str(port, cmd, delay=0.06)
    mon(port, "sendkey ret", pause)
    time.sleep(pause * 0.85)


def main() -> int:
    iso = Path(os.environ.get("ISO", str(ROOT / "kernel-x64-userspace.iso")))
    src = Path(os.environ.get("DISK", str(ROOT / "disk.img")))
    log = Path("/tmp/ir0-sigchld-sysfs-stress.log")
    port = int(os.environ.get("PORT", "46746"))
    if not iso.is_file() or not src.is_file():
        print("✗ missing iso/disk", file=sys.stderr)
        return 1

    user, password = "labuser", "testpass"
    hashed = crypt.crypt(password, crypt.METHOD_SHA512)
    seed_body = (
        f"username={user}\nhostname=unix\npassword_hash={hashed}\n"
        "wheel=1\nlock_root=1\nrecovery=1\n"
    )

    disk = Path(tempfile.mktemp(prefix="ir0-sigchld-sysfs.", suffix=".img"))
    seed = Path(tempfile.mktemp(prefix="ir0-sigchld-sysfs-seed.", suffix=".txt"))
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
        end_base = text.count("CONSOLE_SESSION_END")
        reprompt_base = text.count("CONSOLE_SESSION_REPROMPT")
        resume_base = text.count("CONSOLE_SESSION_RESUME")
        mark = len(text)

        # User-reported sequence + sysfs stress
        stress_cmds = (
            "cat /proc/version",
            "cat /sys/kernel/version",
            "cat /sys/class/net",
            "cat /sys/class/net",
            "cat /sys/class/",
            "ls /sys/class/",
            "cat /sys/devices/",
            "true",
            "ps",
            "cat /sys/class/net",
            "ls /sys/kernel",
            "cat /sys/class/bluetooth",
        )
        for cmd in stress_cmds:
            run_cmd(port, cmd)

        time.sleep(2.5)
        run_cmd(port, f"echo {MARKER}", pause=0.5)
        time.sleep(0.8)

        text = read_log(log)
        tail = text[mark:]
        errs = guards.check_typing_garbage(text, mark=mark)
        if tail.count("USER_FAULT_FRAME\n") > 0:
            errs.append("USER_FAULT_FRAME during sysfs stress")
        if text.count("CONSOLE_SESSION_END") > end_base:
            errs.append("false SESSION_END during sysfs stress")
        if text.count("CONSOLE_SESSION_REPROMPT") > reprompt_base:
            errs.append("false REPROMPT during sysfs stress")
        if text.count("CONSOLE_SESSION_RESUME") > resume_base:
            errs.append("abnormal shell restart (SESSION_RESUME)")
        if MARKER not in tail:
            errs.append("shell lost before marker")
        if tail.count("DELIVER_CTX sig=17") > 24:
            errs.append("excessive SIGCHLD delivery during stress")
        if errs:
            return guards.report_guard_failures(errs, tail)

        print("✓ smoke-sigchld-sysfs-stress PASS")
        return 0
    finally:
        if proc is not None:
            kill_qemu(proc)
        disk.unlink(missing_ok=True)
        seed.unlink(missing_ok=True)


if __name__ == "__main__":
    sys.exit(main())
