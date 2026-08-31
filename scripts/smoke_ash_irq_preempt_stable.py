#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
Ash IRQ preempt stability smoke (post-login / ASH_INTERACTIVE_READY).

Drives firstboot/login via QEMU monitor when needed, then idles under timer
IRQ preempt for --stable-sec seconds. PASS if no CONSOLE_SESSION_SEGV / user
SEGV / panic. Appends ASH_IRQ_PREEMPT_STABLE_OK to the serial log on success.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ISD = ROOT.parent / "ISD"
DEFAULT_TIMEOUT = 180
DEFAULT_STABLE_SEC = 30
MONITOR_PORT = 4470

PASS_TAG = "ASH_IRQ_PREEMPT_STABLE_OK"

SPECIAL = {
    " ": "spc",
    "/": "slash",
    "-": "minus",
    ".": "dot",
    "_": "shift-minus",
}

FAIL_RES = [
    re.compile(r"KERNEL PANIC"),
    re.compile(r"DOUBLE PANIC"),
    re.compile(r"CONSOLE_SESSION_SEGV"),
    re.compile(r"SEGV addr="),
    re.compile(r"userspace segv", re.IGNORECASE),
    re.compile(r"GPF_IN_USERSPACE"),
    re.compile(r"General protection fault"),
    re.compile(r"UD_FAULT_RIP="),
    re.compile(r"#UD\b"),
    re.compile(r"\[FASE[0-9A-Z]+\]\[FAIL\]"),
]

USER_PROMPT = re.compile(r"Enter your Unix username:")
HOST_PROMPT = re.compile(r"Hostname")
PASS_PROMPT = re.compile(r"Password:")
CONFIRM_PROMPT = re.compile(r"Confirm password:")
LOGIN_PROMPT = re.compile(r"(?m)^(?:.*\n)?login:")
POST_FIRSTBOOT_USER = re.compile(r"Enter your Unix username:")


def log_text(path: Path) -> str:
    try:
        return path.read_text(errors="replace")
    except OSError:
        return ""


def kill_qemu(proc: subprocess.Popen[bytes]) -> None:
    if proc.poll() is not None:
        return
    try:
        proc.send_signal(signal.SIGTERM)
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            pass
    except ProcessLookupError:
        pass


def append_tag(path: Path, tag: str) -> None:
    with path.open("a", encoding="utf-8") as fh:
        fh.write(f"\n{tag}\n")


def monitor_send(port: int, cmd: str) -> None:
    with socket.create_connection(("127.0.0.1", port), timeout=5) as sock:
        sock.settimeout(0.4)
        try:
            sock.recv(4096)
        except OSError:
            pass
        sock.sendall((cmd.strip() + "\r\n").encode("ascii"))
        time.sleep(0.05)
        try:
            sock.recv(4096)
        except OSError:
            pass


def type_str(port: int, text: str, delay: float = 0.12) -> None:
    for ch in text:
        if ch in SPECIAL:
            monitor_send(port, f"sendkey {SPECIAL[ch]}")
        elif ch.isupper():
            monitor_send(port, f"sendkey shift-{ch.lower()}")
        else:
            monitor_send(port, f"sendkey {ch}")
        time.sleep(delay)
    monitor_send(port, "sendkey ret")
    time.sleep(0.35)


def ash_ready(text: str) -> bool:
    return "ASH_INTERACTIVE_READY" in text


def main() -> int:
    ap = argparse.ArgumentParser(description="Ash IRQ preempt stability smoke")
    ap.add_argument("--log", default="/tmp/ash-irq-preempt-stable.log")
    ap.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT)
    ap.add_argument("--stable-sec", type=int, default=DEFAULT_STABLE_SEC)
    ap.add_argument("--qemu", default=os.environ.get("QEMU", "qemu-system-x86_64"))
    ap.add_argument("--iso", default=str(ROOT / "kernel-x64-userspace.iso"))
    default_disk = ISD / "out/x86_64/images/desktop/disk.img"
    if not default_disk.is_file():
        default_disk = ROOT / "disk.img"
    ap.add_argument("--disk", default=str(default_disk))
    ap.add_argument("--monitor-port", type=int, default=MONITOR_PORT)
    ap.add_argument("--user", default="ivan")
    ap.add_argument("--password", default="ivan")
    args = ap.parse_args()

    iso = Path(args.iso)
    src_disk = Path(args.disk)
    if not iso.is_file():
        print(f"✗ missing ISO: {iso}", file=sys.stderr)
        return 2
    if not src_disk.is_file():
        print(f"✗ missing disk: {src_disk}", file=sys.stderr)
        return 2

    log_path = Path(args.log)
    log_path.unlink(missing_ok=True)

    subprocess.run(
        ["pkill", "-f", f"qemu-system-x86_64.*127.0.0.1:{args.monitor_port}"],
        check=False,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    time.sleep(0.3)

    disk = Path(tempfile.mktemp(prefix="ir0-ash-stable.", suffix=".img"))
    shutil.copy2(src_disk, disk)

    qemu_cmd = [
        args.qemu,
        "-cdrom",
        str(iso),
        "-drive",
        f"file={disk},format=raw,if=ide,index=0",
        "-serial",
        f"file:{log_path}",
        "-display",
        "none",
        "-m",
        "512M",
        "-no-reboot",
        "-net",
        "none",
        "-monitor",
        f"tcp:127.0.0.1:{args.monitor_port},server,nowait",
    ]

    proc = subprocess.Popen(
        qemu_cmd,
        cwd=ROOT,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        start_new_session=True,
    )

    deadline = time.monotonic() + args.timeout
    ready_at: float | None = None
    firstboot_user_done = False
    firstboot_host_done = False
    firstboot_pass_done = False
    firstboot_confirm_done = False
    login_user_done = False
    login_pass_done = False
    port = args.monitor_port

    try:
        while time.monotonic() < deadline:
            if proc.poll() is not None:
                break

            text = log_text(log_path)
            for pat in FAIL_RES:
                if pat.search(text):
                    print(f"✗ ash-irq-preempt-stable FAIL: {pat.pattern}")
                    print(f"  LOG {log_path}")
                    return 1

            if ash_ready(text):
                if ready_at is None:
                    ready_at = time.monotonic()
                    print(
                        f"  READY   ash interactive "
                        f"({args.stable_sec}s idle under timer preempt)"
                    )
            else:
                # Drive firstboot / login when the guest is waiting for input.
                if (
                    not firstboot_user_done
                    and USER_PROMPT.search(text)
                    and "FIRSTBOOT_OK" not in text
                ):
                    time.sleep(0.8)
                    type_str(port, args.user)
                    firstboot_user_done = True
                    print(f"  FIRSTBOOT typed user={args.user}")
                elif (
                    firstboot_user_done
                    and not firstboot_host_done
                    and HOST_PROMPT.search(text)
                    and "FIRSTBOOT_OK" not in text
                ):
                    time.sleep(0.5)
                    type_str(port, "unix")
                    firstboot_host_done = True
                    print("  FIRSTBOOT typed hostname=unix")
                elif (
                    firstboot_host_done
                    and not firstboot_pass_done
                    and PASS_PROMPT.search(text)
                    and "FIRSTBOOT_OK" not in text
                    and "Confirm password:" not in text
                ):
                    time.sleep(0.5)
                    type_str(port, args.password)
                    firstboot_pass_done = True
                    print("  FIRSTBOOT typed password")
                elif (
                    firstboot_pass_done
                    and not firstboot_confirm_done
                    and CONFIRM_PROMPT.search(text)
                ):
                    time.sleep(0.5)
                    type_str(port, args.password)
                    firstboot_confirm_done = True
                    print("  FIRSTBOOT typed confirm")
                elif (
                    not login_user_done
                    and "ASH_INTERACTIVE_READY" not in text
                    and (
                        LOGIN_PROMPT.search(text)
                        or (
                            "FIRSTBOOT_OK" in text
                            and POST_FIRSTBOOT_USER.search(text)
                        )
                    )
                ):
                    time.sleep(0.8)
                    type_str(port, args.user, delay=0.18)
                    login_user_done = True
                    print(f"  LOGIN typed user={args.user}")
                elif (
                    login_user_done
                    and not login_pass_done
                    and "LOGIN_USER_READ" in text
                    and "LOGIN_PASS_READ" not in text
                    and "ASH_INTERACTIVE_READY" not in text
                ):
                    # Wait until the post-login Password prompt is active.
                    if not re.search(r"LOGIN_USER_READ[\s\S]*Password:", text):
                        pass
                    else:
                        time.sleep(0.5)
                        type_str(port, args.password, delay=0.20)
                        login_pass_done = True
                        print("  LOGIN typed password")

            if ready_at is not None:
                elapsed = time.monotonic() - ready_at
                # Re-check fail patterns during stable window.
                text = log_text(log_path)
                for pat in FAIL_RES:
                    if pat.search(text):
                        print(f"✗ ash-irq-preempt-stable FAIL during idle: {pat.pattern}")
                        print(f"  LOG {log_path}")
                        return 1
                if elapsed >= args.stable_sec:
                    append_tag(log_path, PASS_TAG)
                    print(
                        f"✓ ash-irq-preempt-stable PASS "
                        f"({PASS_TAG}, stable {args.stable_sec}s)"
                    )
                    print(f"  LOG {log_path}")
                    return 0

            time.sleep(0.25)

        text = log_text(log_path)
        if ready_at is None:
            print("✗ ash-irq-preempt-stable timeout before ASH_INTERACTIVE_READY")
            for hint in (
                "FIRSTBOOT_PENDING",
                "Enter your Unix username:",
                "login:",
                "LOGIN_OK",
                "GETTY_READY",
            ):
                if hint in text:
                    print(f"  - saw {hint!r}")
        else:
            print(
                f"✗ ash-irq-preempt-stable timeout during stable window "
                f"(need {args.stable_sec}s)"
            )
        print(f"  LOG {log_path}")
        return 1
    finally:
        kill_qemu(proc)
        disk.unlink(missing_ok=True)


if __name__ == "__main__":
    sys.exit(main())
