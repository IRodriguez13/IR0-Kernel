#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
FASE58E/K — headless ash interactive smoke with QEMU monitor key injection.

Boots runit + BusyBox ash, waits for banner + ASH_INTERACTIVE_READY, injects
  echo hi
  pwd
via HMP sendkey, validates compact serial tags, then kills QEMU.

GUI validation remains manual: make run-fase58e-ash-gui
"""

from __future__ import annotations

import argparse
import os
import re
import signal
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
DEFAULT_TIMEOUT = 90
MONITOR_PORT = 4445

CORE_TAGS = [
    "GETTY_READY",
    "LOGIN_OK",
    "ASH_INTERACTIVE_READY",
    "KBD_USER_POLL_OK",
    "TTY_CANON_LINE_READY",
    "SYS_READ_RETURN_OK",
    "ASH_COMMAND_ECHO_OK",
]

OPTIONAL_TAGS = [
    "ASH_COMMAND_EXEC_OK",
]

FAIL_RES = [
    re.compile(r"KERNEL PANIC"),
    re.compile(r"DOUBLE PANIC"),
    re.compile(r"General protection fault|GPF_IN_USERSPACE"),
    re.compile(r"UD_FAULT_RIP="),
    re.compile(r"\[FASE[0-9A-Z]+\]\[FAIL\]"),
]

ECHO_KEYS = ["e", "c", "h", "o", "spc", "h", "i", "ret"]
PWD_KEYS = ["p", "w", "d", "ret"]
LOGIN_KEYS = ["r", "o", "o", "t", "ret"]
PASSWORD_KEYS = ["ret"]


def log_has_tags(text: str, tags: list[str]) -> tuple[bool, list[str]]:
    missing = [t for t in tags if t not in text]
    return len(missing) == 0, missing


def log_has_hi(text: str) -> bool:
    return "hi\n" in text or "hi\r\n" in text


def monitor_send(port: int, cmd: str) -> None:
    with socket.create_connection(("127.0.0.1", port), timeout=5) as sock:
        sock.settimeout(2)
        try:
            sock.recv(4096)
        except socket.timeout:
            pass
        sock.sendall((cmd.strip() + "\r\n").encode("ascii"))
        time.sleep(0.05)
        try:
            sock.recv(4096)
        except socket.timeout:
            pass


def send_keys(port: int, keys: list[str], delay: float = 0.12) -> None:
    for key in keys:
        monitor_send(port, f"sendkey {key}")
        time.sleep(delay)


def build_disk(root: Path) -> Path:
    src_disk = root / "disk.img"

    if not src_disk.is_file():
        print(
            f"✗ missing disk: {src_disk} — run make load-userspace-runit",
            file=sys.stderr,
        )
        sys.exit(1)

    disk = Path(tempfile.mktemp(prefix="ir0-fase58e-smoke.", suffix=".img"))
    subprocess.run(["cp", "-f", str(src_disk), str(disk)], check=True)
    subprocess.run(
        [
            "python3",
            str(root / "scripts/verify_minix_rootfs.py"),
            str(disk),
            "/sbin/init",
            "/bin/sh",
            "/bin/busybox",
        ],
        check=True,
        cwd=root,
    )
    return disk


def read_log(path: Path) -> str:
    if not path.is_file():
        return ""
    return path.read_text(errors="replace")


def main() -> int:
    parser = argparse.ArgumentParser(description="FASE58E ash interactive smoke")
    parser.add_argument("--log", default="/tmp/fase58e-ash-smoke.log")
    parser.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT)
    parser.add_argument("--qemu", default=os.environ.get("QEMU", "qemu-system-x86_64"))
    parser.add_argument("--iso", default=str(ROOT / "kernel-x64-userspace-ash-smoke.iso"))
    parser.add_argument("--monitor-port", type=int, default=MONITOR_PORT)
    args = parser.parse_args()

    log_path = Path(args.log)
    log_path.unlink(missing_ok=True)

    if not Path(args.iso).is_file():
        print(f"✗ missing ISO: {args.iso}", file=sys.stderr)
        return 1

    disk = build_disk(ROOT)
    monitor = f"tcp:127.0.0.1:{args.monitor_port},server,nowait"

    qemu_cmd = [
        args.qemu,
        "-cdrom", args.iso,
        "-drive", f"file={disk},format=raw,if=ide,index=0",
        "-serial", f"file:{log_path}",
        "-display", "none",
        "-monitor", monitor,
        "-m", "256M",
        "-no-reboot",
        "-net", "none",
    ]
    stderr_path = Path(tempfile.mktemp(prefix="ir0-fase58e-qemu.", suffix=".stderr"))
    stderr_file = stderr_path.open("wb")
    proc = subprocess.Popen(
        qemu_cmd,
        cwd=ROOT,
        stdout=subprocess.DEVNULL,
        stderr=stderr_file,
    )

    deadline = time.monotonic() + args.timeout
    echo_sent = False
    pwd_sent = False
    rc = 1

    try:
        while time.monotonic() < deadline:
            if proc.poll() is not None:
                stderr_file.flush()
                if not read_log(log_path):
                    detail = stderr_path.read_text(errors="replace").strip()
                    print(f"✗ QEMU exited before serial output (rc={proc.returncode})")
                    if detail:
                        print(detail)
                break

            text = read_log(log_path)

            for pat in FAIL_RES:
                if pat.search(text):
                    print(f"✗ smoke-fase58e-ash-interactive FAIL pattern: {pat.pattern}")
                    return 1

            ready = (
                "LOGIN_OK" in text
                and "ASH_INTERACTIVE_READY" in text
                and "RUNSV_CONSOLE_START" in text
                and "BusyBox v" in text
            )

            if ready and not echo_sent:
                time.sleep(2.0)
                try:
                    send_keys(args.monitor_port, ECHO_KEYS)
                    echo_sent = True
                except OSError as exc:
                    print(f"✗ monitor key injection failed: {exc}")
                    print("  HINT: run make run-fase58e-ash-gui for manual GUI smoke")
                    return 1

            if echo_sent and not pwd_sent:
                if "ASH_COMMAND_ECHO_OK" in text and log_has_hi(text):
                    time.sleep(0.5)
                    try:
                        send_keys(args.monitor_port, PWD_KEYS)
                        pwd_sent = True
                    except OSError as exc:
                        print(f"✗ monitor pwd injection failed: {exc}")
                        return 1

            if echo_sent:
                ok_core, missing = log_has_tags(text, CORE_TAGS)
                if ok_core and log_has_hi(text):
                    if "ASH_COMMAND_EXEC_OK" in text:
                        print("✓ smoke-fase58e-ash-interactive PASS (echo + pwd)")
                    else:
                        print("✓ smoke-fase58e-ash-interactive PASS (echo; pwd tag optional)")
                    return 0

            time.sleep(0.25)

        text = read_log(log_path)
        print("✗ smoke-fase58e-ash-interactive timeout ({:d}s)".format(args.timeout))
        if "ASH_INTERACTIVE_READY" not in text:
            print("  - never saw ASH_INTERACTIVE_READY")
        if not echo_sent:
            print("  - echo key injection did not run")
        ok_core, missing = log_has_tags(text, CORE_TAGS)
        if not log_has_hi(text):
            print("  - literal output 'hi'")
        for tag in missing:
            print(f"  - {tag}")
        return 1
    finally:
        if proc.poll() is None:
            proc.send_signal(signal.SIGTERM)
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
        stderr_file.close()
        stderr_path.unlink(missing_ok=True)
        disk.unlink(missing_ok=True)


if __name__ == "__main__":
    sys.exit(main())
