#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""SIGCHLD to a shell blocked in wait4 must not panic.

Regression for: switch_to_user_task "invalid RIP for ring3 iretq" panic. A
foreground command (fork + wait4) exits; SIGCHLD is delivered to ash on
schedule-in while its task.arch still holds a kernel continuation (rip in
kernel .text). Saving that as the sigcontext made rt_sigreturn iretq to ring3
with a kernel RIP -> KERNEL PANIC.

Flow: firstboot.seed -> login -> run a burst of external commands (each is
fork+execve+wait4 -> SIGCHLD with ash's handler installed). PASS if no panic
and the shell stays responsive (final marker echoes).
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

PANIC = ("KERNEL PANIC", "invalid RIP for ring3 iretq",
         "invalid CS/SS for ring3 iretq", "PARENT_IRET_FRAME_BAD")


def main() -> int:
    iso = Path(os.environ.get("ISO", str(ROOT / "kernel-x64-userspace.iso")))
    src = Path(os.environ.get("DISK", str(ROOT / "disk.img")))
    log = Path("/tmp/ir0-sigchld-wait.log")
    port = int(os.environ.get("PORT", "46939"))
    rounds = int(os.environ.get("ROUNDS", "12"))
    if not iso.is_file() or not src.is_file():
        print("✗ missing iso/disk", file=sys.stderr)
        return 1

    user, password = "labuser", "testpass"
    hashed = crypt.crypt(password, crypt.METHOD_SHA512)
    seed_body = (
        f"username={user}\nhostname=unix\npassword_hash={hashed}\n"
        "wheel=1\nlock_root=1\nrecovery=1\n"
    )

    disk = Path(tempfile.mktemp(prefix="ir0-sigchld.", suffix=".img"))
    seed = Path(tempfile.mktemp(prefix="ir0-sigchld-seed.", suffix=".txt"))
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
            print(read_log(log)[-4000:], file=sys.stderr)
            return 1
        wait_tags(log, ["RUNSV_LOGGER_START"], proc, 15)
        time.sleep(3.0)
        if not login(port, log, proc, user, password, 0, 0, 0):
            print("✗ login failed", file=sys.stderr)
            print(read_log(log)[-4000:], file=sys.stderr)
            return 1

        # Burst of foreground commands: each forks + wait4 -> SIGCHLD.
        cmds = ["groups", "id", "true", "uname", "echo hi", "pwd"]
        for i in range(rounds):
            type_str(port, cmds[i % len(cmds)], delay=0.04)
            mon(port, "sendkey ret", 0.18)
            if proc.poll() is not None:
                break
            text = read_log(log)
            if any(p in text for p in PANIC):
                print(f"✗ panic after {i} commands", file=sys.stderr)
                print(text[-4000:], file=sys.stderr)
                return 1

        # Shell responsive check: marker must echo back.
        time.sleep(0.5)
        # No '_' / punctuation: type_str/QEMU sendkey only maps the subset used
        # by the login helpers; underscores are silently dropped.
        marker = "SIGCHLDOK7788"
        type_str(port, f"echo {marker}", delay=0.04)
        mon(port, "sendkey ret", 0.5)
        deadline = time.time() + 12
        ok = False
        while time.time() < deadline:
            text = read_log(log)
            if any(p in text for p in PANIC):
                print("✗ panic during responsiveness check", file=sys.stderr)
                print(text[-4000:], file=sys.stderr)
                return 1
            # Two occurrences: the typed echo line + its output.
            if text.count(marker) >= 2:
                ok = True
                break
            if proc.poll() is not None:
                break
            time.sleep(0.25)

        text = read_log(log)
        if any(p in text for p in PANIC):
            print("✗ panic (final)", file=sys.stderr)
            print(text[-4000:], file=sys.stderr)
            return 1
        if not ok:
            print("✗ shell unresponsive after command burst", file=sys.stderr)
            print(text[-3000:], file=sys.stderr)
            return 1

        deferred = text.count("DELIVER_DEFER")
        print(f"✓ smoke-sigchld-wait PASS "
              f"({rounds} cmds, no panic, DELIVER_DEFER x{deferred})")
        return 0
    finally:
        if proc is not None:
            kill_qemu(proc)
        disk.unlink(missing_ok=True)
        seed.unlink(missing_ok=True)


if __name__ == "__main__":
    sys.exit(main())
