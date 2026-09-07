#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Headless firstboot and persistent second-boot smoke.

Boot one applies /etc/firstboot.seed and writes account state. Boot two reuses
the same disk and must skip provisioning while retaining the written data.
"""

from __future__ import annotations

import argparse
import crypt
import os
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TIMEOUT = 90

NEED_TAGS = [
    "RUNIT_STAGE1_OK",
    "FIRSTBOOT_OK",
    "GETTY_READY",
]
SECOND_BOOT_TAGS = [
    "RUNIT_STAGE1_OK",
    "FIRSTBOOT_SKIP",
    "GETTY_READY",
]


def read_log(path: Path) -> str:
    if not path.is_file():
        return ""
    return path.read_text(errors="replace")


def kill_qemu(proc: subprocess.Popen[bytes]) -> None:
    if proc.poll() is not None:
        return
    try:
        proc.send_signal(signal.SIGTERM)
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=5)
    except ProcessLookupError:
        pass


def main() -> int:
    parser = argparse.ArgumentParser(description="Firstboot seed smoke (minimal)")
    parser.add_argument("--log", default="/tmp/firstboot-seed-smoke.log")
    parser.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT)
    parser.add_argument("--qemu", default=os.environ.get("QEMU", "qemu-system-x86_64"))
    parser.add_argument("--iso", default=str(ROOT / "kernel-x64-userspace.iso"))
    parser.add_argument("--disk", default=str(ROOT / "disk.img"))
    args = parser.parse_args()

    log_path = Path(args.log)
    log_path.unlink(missing_ok=True)
    iso = Path(args.iso)
    src = Path(args.disk)
    if not iso.is_file() or not src.is_file():
        print(
            "✗ missing iso/disk — IR0_PRODUCT_PROFILE=minimal make load-userspace-runit",
            file=sys.stderr,
        )
        return 1

    user = "ivan"
    hashed = crypt.crypt("testpass", crypt.METHOD_SHA512)
    if not hashed or not hashed.startswith("$6$"):
        print("✗ host crypt() could not build SHA-512 hash", file=sys.stderr)
        return 1

    seed_body = (
        f"username={user}\n"
        f"hostname=unix\n"
        f"password_hash={hashed}\n"
        "wheel=1\n"
        "lock_root=1\n"
        "recovery=1\n"
    )

    disk = Path(tempfile.mktemp(prefix="ir0-fb-seed.", suffix=".img"))
    seed_path = Path(tempfile.mktemp(prefix="ir0-fb-seed.", suffix=".txt"))
    try:
        subprocess.run(["cp", "-f", str(src), str(disk)], check=True)
        seed_path.write_text(seed_body, encoding="utf-8")
        inject = ROOT / "scripts" / "inject_init_minix.py"
        subprocess.run(
            [
                sys.executable,
                str(inject),
                str(disk),
                str(seed_path),
                "etc/firstboot.seed",
            ],
            check=True,
        )

        proc = subprocess.Popen(
            [
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
                "256M",
                "-no-reboot",
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

        deadline = time.time() + args.timeout
        ok = False
        while time.time() < deadline:
            text = read_log(log_path)
            if "FIRSTBOOT_FAIL" in text:
                kill_qemu(proc)
                print("✗ FIRSTBOOT_FAIL in serial log", file=sys.stderr)
                print(text[-4000:], file=sys.stderr)
                return 1
            if all(tag in text for tag in NEED_TAGS):
                ok = True
                break
            if proc.poll() is not None:
                break
            time.sleep(0.25)

        kill_qemu(proc)
        text = read_log(log_path)
        if not ok:
            print("✗ missing tags:", file=sys.stderr)
            for tag in NEED_TAGS:
                print(f"  {'OK' if tag in text else 'MISS'} {tag}", file=sys.stderr)
            print(text[-4000:], file=sys.stderr)
            return 1

        # Give IDE a moment to flush; then scan the image for persisted state.
        time.sleep(0.5)
        data = disk.read_bytes()
        # Directory entry stores basename only (≤14 chars on current pack FS).
        if b"firstboot.done" not in data:
            print(
                "✗ firstboot.done marker missing on disk after firstboot",
                file=sys.stderr,
            )
            return 1
        if f"{user}:x:1000:".encode() not in data:
            print("✗ passwd account missing on disk after firstboot", file=sys.stderr)
            return 1
        if hashed.encode() not in data:
            print("✗ exact shadow hash missing on disk after firstboot", file=sys.stderr)
            return 1

        # Regression: rename/unlink nlink bug collapsed shadow→"ok\n".
        sys.path.insert(0, str(ROOT / "scripts"))
        from verify_minix_rootfs import parse_super, read_block, resolve_path, read_file_prefix

        with disk.open("rb") as f:
            sb = parse_super(read_block(f, 1))
            for path, must in (
                ("etc/shadow", f"{user}:$6$"),
                ("etc/group", "wheel:"),
                ("etc/hostname", "unix"),
                ("etc/firstboot.done", "ok"),
            ):
                inode, err = resolve_path(f, sb, path)
                if err or not inode:
                    print(f"✗ missing {path}: {err}", file=sys.stderr)
                    return 1
                body = read_file_prefix(f, sb, inode, inode["size"]).decode(
                    "utf-8", errors="replace"
                )
                if path == "etc/shadow" and body.strip() == "ok":
                    print("✗ etc/shadow collapsed to done-marker (rename/nlink bug)", file=sys.stderr)
                    return 1
                if must not in body:
                    print(f"✗ {path} missing {must!r}: {body!r}", file=sys.stderr)
                    return 1

        # A new QEMU process is a real second boot. The persisted done marker
        # must suppress provisioning, while the account remains readable.
        log_path.unlink(missing_ok=True)
        proc = subprocess.Popen(
            [
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
                "256M",
                "-no-reboot",
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        deadline = time.time() + args.timeout
        second_ok = False
        while time.time() < deadline:
            text = read_log(log_path)
            if "FIRSTBOOT_FAIL" in text or "KERNEL PANIC" in text or "Oops:" in text:
                break
            if all(tag in text for tag in SECOND_BOOT_TAGS):
                second_ok = True
                break
            if proc.poll() is not None:
                break
            time.sleep(0.25)
        kill_qemu(proc)
        text = read_log(log_path)
        if not second_ok:
            print("✗ persistent second boot failed", file=sys.stderr)
            for tag in SECOND_BOOT_TAGS:
                print(f"  {'OK' if tag in text else 'MISS'} {tag}", file=sys.stderr)
            print(text[-4000:], file=sys.stderr)
            return 1

        data = disk.read_bytes()
        if f"{user}:x:1000:".encode() not in data or hashed.encode() not in data:
            print("✗ account data missing after second boot", file=sys.stderr)
            return 1

        print("✓ smoke-firstboot-seed OK (state survived second boot)")
        return 0
    finally:
        seed_path.unlink(missing_ok=True)
        disk.unlink(missing_ok=True)


if __name__ == "__main__":
    sys.exit(main())
