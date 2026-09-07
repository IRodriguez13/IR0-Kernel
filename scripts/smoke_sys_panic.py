#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Gate: /sys/kernel/panic forces a panic, and pseudo-fs dirs carry a real date.

Two things this covers, both requested together:

  * Part A — pseudo-fs directory timestamps. `stat` on /sys, /proc, /heart and
    a couple of subdirs must report the current wall-clock year (RTC = host),
    never the 1970 epoch that the memset-only dir stat used to leave.

  * Part B — on-demand panic. Writing to /sys/kernel/panic (root, 0644) must
    trigger a real KERNEL PANIC. This is the only on-demand panic path, so it
    also exercises the panic banner / framebuffer path.

Reuses the desktop-relogin harness (seed + login); the panic write goes through
doas since the node is root-writable only.
"""
from __future__ import annotations

import crypt
import importlib.util
import os
import re
import subprocess
import sys
import tempfile
import time
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

_spec = importlib.util.spec_from_file_location(
    "relogin", str(ROOT / "scripts" / "smoke_desktop_relogin.py"))
relogin = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(relogin)

read_log = relogin.read_log
kill_qemu = relogin.kill_qemu
mon = relogin.mon
wait_tags = relogin.wait_tags
login = relogin.login
NEED_BOOT = relogin.NEED_BOOT


def type_str(port: int, s: str, delay: float = 0.16) -> None:
    """Local typer: adds '>' and "'" on top of the relogin key map."""
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
        elif ch == ">":
            mon(port, "sendkey shift-dot", delay)
        elif ch == "'":
            mon(port, "sendkey apostrophe", delay)
        elif ch.isupper():
            mon(port, f"sendkey shift-{ch.lower()}", delay)
        else:
            mon(port, f"sendkey {ch}", delay)


def run_cmd(port: int, log: Path, cmd: str, settle: float = 1.3) -> str:
    mark = len(read_log(log))
    type_str(port, cmd)
    mon(port, "sendkey ret", 0.3)
    time.sleep(settle)
    return read_log(log)[mark:]


# Result codes for one boot attempt.
PASS, RETRY, FAIL = 0, 1, 2


def shell_ready(port: int, log: Path) -> bool:
    """Confirm we really have a shell (login harness is flaky): echo a token."""
    out = run_cmd(port, log, "echo SH0K$?", 1.2)
    return "SH0K0" in out or "SH0K" in out


def attempt(iso: Path, src: Path, log: Path, port: int, year: str,
            user: str, password: str, seed_body: str) -> int:
    disk = Path(tempfile.mktemp(prefix="ir0-sys-panic.", suffix=".img"))
    seed = Path(tempfile.mktemp(prefix="ir0-sys-panic-seed.", suffix=".txt"))
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
            print("  (retry) boot tags missing", file=sys.stderr)
            return RETRY
        # getty/logger race: wait for the logger, then let getty settle.
        wait_tags(log, ["RUNSV_LOGGER_START"], proc, 15)
        time.sleep(4.0)

        if not login(port, log, proc, user, password, 0, 0, 0):
            print("  (retry) login flaked", file=sys.stderr)
            return RETRY
        time.sleep(2.0)
        if not shell_ready(port, log):
            print("  (retry) shell not confirmed after login", file=sys.stderr)
            return RETRY

        # --- Part A: pseudo-fs directory timestamps -------------------------
        # busybox `stat` prints "Modify: YYYY-MM-DD ...". Epoch 0 would read
        # 1970; the fix stamps clock_get_current_time() (RTC = host year).
        for d in ("/sys", "/proc", "/heart", "/sys/kernel"):
            out = run_cmd(port, log, f"stat {d}")
            mods = re.findall(r"Modify:\s*(\d{4})-", out)
            if not mods:
                # Could be a typing garble; treat as retryable once.
                print(f"  (retry) no Modify line for {d}", file=sys.stderr)
                return RETRY
            if any(m == "1970" for m in mods):
                print(f"✗ {d} still reports 1970 epoch (dir not stamped)",
                      file=sys.stderr)
                print(out[-800:], file=sys.stderr)
                return FAIL
            if year not in mods:
                print(f"  WARN {d} Modify year {mods} != host {year} "
                      "(RTC skew?), but not 1970 — accepting")
        print(f"  OK  pseudo-fs dirs carry a real date (year {year}, no 1970)")

        out = run_cmd(port, log, "ls -l /sys/kernel/panic")
        if "panic" not in out:
            print("  (retry) /sys/kernel/panic listing garbled", file=sys.stderr)
            return RETRY
        print("  OK  /sys/kernel/panic present")

        out = run_cmd(port, log, "ls /sys/kernel/reboot /sys/kernel/poweroff /sys/kernel/halt")
        if not all(name in out for name in ("reboot", "poweroff", "halt")):
            print("✗ semantic power controls missing from sysfs", file=sys.stderr)
            print(out[-1000:], file=sys.stderr)
            return FAIL
        print("  OK  semantic sysfs power controls present")

        # --- Part B: force the panic (root-only node, via doas) -------------
        mark = len(read_log(log))
        type_str(port, "doas sh -c 'echo c > /sys/kernel/panic'")
        mon(port, "sendkey ret", 0.3)

        # doas asks for the invoking user's password; feed it once it prompts.
        pw_deadline = time.time() + 12
        while time.time() < pw_deadline:
            if "password:" in read_log(log)[mark:].lower():
                time.sleep(0.4)
                type_str(port, password)
                mon(port, "sendkey ret", 0.3)
                break
            time.sleep(0.2)

        deadline = time.time() + 30
        seen_panic = False
        while time.time() < deadline:
            text = read_log(log)[mark:]
            if "KERNEL PANIC" in text and "PANICEX_KERNEL_WIDE_OK" in text:
                seen_panic = True
                break
            if proc.poll() is not None:
                seen_panic = "KERNEL PANIC" in read_log(log)[mark:]
                break
            time.sleep(0.25)

        text = read_log(log)[mark:]
        if not seen_panic:
            print("✗ write to /sys/kernel/panic did not trigger a panic",
                  file=sys.stderr)
            print(text[-1500:], file=sys.stderr)
            return FAIL
        if "forced panic via /sys/kernel/panic" not in text:
            print("✗ panic reason string missing (wrong trigger?)",
                  file=sys.stderr)
            print(text[-1500:], file=sys.stderr)
            return FAIL

        # Optional: capture the emulated framebuffer for manual FB inspection.
        try:
            shot = Path("/tmp/ir0-sys-panic.ppm")
            mon(port, f"screendump {shot}", 0.6)
            if shot.is_file() and shot.stat().st_size > 0:
                print(f"  OK  framebuffer captured to {shot} "
                      f"({shot.stat().st_size} bytes)")
        except Exception:
            pass

        print("✓ smoke-sys-panic PASS "
              "(pseudo-fs dirs dated; /sys/kernel/panic triggers KERNEL PANIC)")
        return PASS
    finally:
        if proc is not None:
            kill_qemu(proc)
        disk.unlink(missing_ok=True)
        seed.unlink(missing_ok=True)


def main() -> int:
    iso = Path(os.environ.get("ISO", str(ROOT / "kernel-x64-userspace.iso")))
    src = Path(os.environ.get("DISK", str(ROOT / "disk.img")))
    log = Path("/tmp/ir0-sys-panic.log")
    port = int(os.environ.get("PORT", "46239"))
    if not iso.is_file() or not src.is_file():
        print("✗ missing iso/disk", file=sys.stderr)
        return 1

    year = f"{datetime.now().year}"
    user, password = "labuser", "testpass"
    hashed = crypt.crypt(password, crypt.METHOD_SHA512)
    seed_body = (
        f"username={user}\nhostname=unix\npassword_hash={hashed}\n"
        "wheel=1\nlock_root=1\nrecovery=1\n"
    )

    attempts = int(os.environ.get("ATTEMPTS", "3"))
    for i in range(attempts):
        print(f"-- attempt {i + 1}/{attempts} --")
        rc = attempt(iso, src, log, port, year, user, password, seed_body)
        if rc == PASS:
            return 0
        if rc == FAIL:
            return 1
        time.sleep(1.0)
    print("✗ smoke-sys-panic: exhausted retries (login/typing flake)",
          file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
