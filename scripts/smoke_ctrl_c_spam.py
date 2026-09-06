#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Ctrl+C spam on interactive ash must not corrupt the line editor or false-logout.

Regression: rapid ^C after session events produced garbage bytes (ê^C) and
eventually abnormal CONSOLE_SESSION_END without voluntary exit.
"""
from __future__ import annotations

import crypt
import os
import re
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

MARKER = "CTRLCSPAM9911"
SPAM_COUNT = 24


def sanitize(s: str) -> str:
    return "".join(c if (32 <= ord(c) < 127 or c in "\n\t") else "." for c in s)


def main() -> int:
    iso = Path(os.environ.get("ISO", str(ROOT / "kernel-x64-userspace.iso")))
    src = Path(os.environ.get("DISK", str(ROOT / "disk.img")))
    log = Path("/tmp/ir0-ctrl-c-spam.log")
    port = int(os.environ.get("PORT", "46744"))
    if not iso.is_file() or not src.is_file():
        print("✗ missing iso/disk", file=sys.stderr)
        return 1

    user, password = "labuser", "testpass"
    hashed = crypt.crypt(password, crypt.METHOD_SHA512)
    seed_body = (
        f"username={user}\nhostname=unix\npassword_hash={hashed}\n"
        "wheel=1\nlock_root=1\nrecovery=1\n"
    )

    disk = Path(tempfile.mktemp(prefix="ir0-ctrl-c-spam.", suffix=".img"))
    seed = Path(tempfile.mktemp(prefix="ir0-ctrl-c-spam-seed.", suffix=".txt"))
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

        reprompt_base = read_log(log).count("CONSOLE_SESSION_REPROMPT")
        mark = len(read_log(log))

        for _ in range(SPAM_COUNT):
            mon(port, "sendkey ctrl-c", 0.08)

        time.sleep(1.0)
        type_str(port, f"echo {MARKER}", delay=0.05)
        mon(port, "sendkey ret", 0.8)
        time.sleep(0.5)

        text = read_log(log)
        for tag in FATAL:
            if tag in text:
                print(f"✗ fatal: {tag}", file=sys.stderr)
                return 1
        if text.count("CONSOLE_SESSION_REPROMPT") > reprompt_base:
            print("✗ false logout (REPROMPT after ^C spam)", file=sys.stderr)
            return 1
        if "[[" in text[mark:] and "not found" in text[mark:]:
            print("✗ shell corruption after ^C spam", file=sys.stderr)
            return 1
        if text.count("USER_RESUME_KSTACK_GPR_LEAK") > 0:
            print("✗ GPR leak after ^C spam", file=sys.stderr)
            return 1
        if MARKER not in text[mark:]:
            print("✗ shell dead after ^C spam (marker missing)", file=sys.stderr)
            print(sanitize(text[mark:][-2000:]), file=sys.stderr)
            return 1

        window = text[mark:]
        for ln in window.splitlines():
            if "@" in ln and "$" in ln:
                run = max((len(m.group(0)) for m in
                           re.finditer(r"[^\x20-\x7e]+", ln)), default=0)
                if run >= 4:
                    print("✗ non-printable burst on prompt line", file=sys.stderr)
                    print(repr(ln), file=sys.stderr)
                    return 1

        print(f"✓ smoke-ctrl-c-spam PASS (^C x{SPAM_COUNT}, marker OK)")
        return 0
    finally:
        if proc is not None:
            kill_qemu(proc)
        disk.unlink(missing_ok=True)
        seed.unlink(missing_ok=True)


if __name__ == "__main__":
    sys.exit(main())
