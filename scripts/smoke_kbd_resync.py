#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Keyboard decoder resync across a session boundary.

Guards the "random characters after a crash/relogin" symptom. Flow:
  firstboot.seed -> login -> `exec ls` (session end, ISD issues TCFLSH)
  -> re-login -> type `abc`.

PASS requires:
  * second LOGIN_OK (session survived the blocked-read resume),
  * KBD_STATE_RESYNC on the session flush (PS/2 decoder resynced, so a
    modifier/E0 held at session end cannot stick into the next getty),
  * the post-relogin `abc` echoes clean ("abc" + "not found"), no burst of
    non-printable garbage on the input line,
  * no STACK_TOP_OVERRUN / KTM_USER_CANARY_BROKEN / USER_FAULT_FRAME /
    CONSOLE_SESSION_SEGV anywhere (the real corruption fingerprints).

Reuses the desktop-relogin harness helpers.
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
wait_count = relogin.wait_count
wait_prompt = relogin.wait_prompt
login = relogin.login
PROMPT_RE = relogin.PROMPT_RE
NEED_BOOT = relogin.NEED_BOOT
FATAL = relogin.FATAL


def sanitize(s: str) -> str:
    return "".join(c if (32 <= ord(c) < 127 or c in "\n\t") else "." for c in s)


def main() -> int:
    iso = Path(os.environ.get("ISO", str(ROOT / "kernel-x64-userspace.iso")))
    src = Path(os.environ.get("DISK", str(ROOT / "disk.img")))
    log = Path("/tmp/ir0-kbd-resync.log")
    port = int(os.environ.get("PORT", "46733"))
    if not iso.is_file() or not src.is_file():
        print("✗ missing iso/disk", file=sys.stderr)
        return 1

    user, password = "labuser", "testpass"
    hashed = crypt.crypt(password, crypt.METHOD_SHA512)
    seed_body = (
        f"username={user}\nhostname=unix\npassword_hash={hashed}\n"
        "wheel=1\nlock_root=1\nrecovery=1\n"
    )

    disk = Path(tempfile.mktemp(prefix="ir0-kbd-resync.", suffix=".img"))
    seed = Path(tempfile.mktemp(prefix="ir0-kbd-resync-seed.", suffix=".txt"))
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
        # Logger racing the first username read resets getty; let it settle.
        wait_tags(log, ["RUNSV_LOGGER_START"], proc, 15)
        time.sleep(3.0)

        if not login(port, log, proc, user, password, 0, 0, 0):
            print("✗ first login failed", file=sys.stderr)
            print(read_log(log)[-4000:], file=sys.stderr)
            return 1

        text = read_log(log)
        prompts = len(list(PROMPT_RE.finditer(text)))
        ends = text.count("CONSOLE_SESSION_END")
        user_base = text.count("Enter your Unix username")
        pass_base = text.count("Password:")
        login_ok = text.count("LOGIN_OK")

        # Session boundary: exec ls -> logout -> ISD TCFLSH -> resync.
        type_str(port, "exec ls")
        mon(port, "sendkey ret", 0.4)
        if not wait_count(log, proc, "CONSOLE_SESSION_END", ends + 1, 30):
            print("✗ exec ls did not end the session", file=sys.stderr)
            print(read_log(log)[-4000:], file=sys.stderr)
            return 1
        time.sleep(1.0)

        if not login(port, log, proc, user, password,
                     user_base, pass_base, prompts):
            print("✗ re-login failed", file=sys.stderr)
            print(read_log(log)[-6000:], file=sys.stderr)
            return 1

        # Type into the fresh session; capture the echoed input line.
        time.sleep(0.5)
        mark = len(read_log(log))
        type_str(port, "abc")
        mon(port, "sendkey ret", 0.6)
        time.sleep(0.6)

        text = read_log(log)
        for tag in FATAL:
            if tag in text:
                print(f"✗ fatal fingerprint present: {tag}", file=sys.stderr)
                print(sanitize(text[-4000:]), file=sys.stderr)
                return 1
        if text.count("LOGIN_OK") < login_ok + 1:
            print("✗ missing second LOGIN_OK", file=sys.stderr)
            print(sanitize(text[-4000:]), file=sys.stderr)
            return 1
        if "KBD_STATE_RESYNC" not in text:
            print("✗ KBD_STATE_RESYNC not observed on session flush",
                  file=sys.stderr)
            print(sanitize(text[-4000:]), file=sys.stderr)
            return 1

        leaks = text.count("USER_RESUME_KSTACK_GPR_LEAK")
        if leaks > 0:
            print(f"✗ USER_RESUME_KSTACK_GPR_LEAK x{leaks}", file=sys.stderr)
            print(sanitize(text[-4000:]), file=sys.stderr)
            return 1

        # The `abc` we sent must appear; the input line must not be a burst of
        # control bytes (class B/D garbage). Look at the tail after `mark`.
        window = text[mark:]
        if "abc" not in window:
            print("✗ typed 'abc' did not echo cleanly after relogin",
                  file=sys.stderr)
            print(sanitize(window[-2000:]), file=sys.stderr)
            return 1
        # Garbage heuristic: a run of >=6 non-printables on an input line.
        raw_lines = [ln for ln in window.splitlines() if "labuser@" in ln]
        for ln in raw_lines:
            run = max((len(m.group(0)) for m in
                       re.finditer(r"[^\x20-\x7e]+", ln)), default=0)
            if run >= 6:
                print("✗ non-printable burst on input line after relogin",
                      file=sys.stderr)
                print(repr(ln), file=sys.stderr)
                return 1

        print("✓ smoke-kbd-resync PASS "
              f"(LOGIN_OK x{text.count('LOGIN_OK')}, KBD_STATE_RESYNC seen)")
        return 0
    finally:
        if proc is not None:
            kill_qemu(proc)
        disk.unlink(missing_ok=True)
        seed.unlink(missing_ok=True)


if __name__ == "__main__":
    sys.exit(main())
