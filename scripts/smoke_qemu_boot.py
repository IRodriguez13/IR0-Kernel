#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Shared QEMU -kernel/-append boot helpers for IR0 smokes."""

from __future__ import annotations

from pathlib import Path


def extend_qemu_kernel_boot(
    cmd: list[str],
    root: Path,
    *,
    ash_smoke: bool = False,
    loglevel: str = "normal",
) -> None:
    """
    Append -kernel / -append so smokes get ir0.ash_smoke=1 without baking it
    into the default GRUB entry used by make run / run-isd.
    """
    kernel = root / "kernel-x64.bin"
    if not kernel.is_file():
        return
    append = f"ir0.loglevel={loglevel}"
    if ash_smoke:
        append += " ir0.ash_smoke=1"
    cmd.extend(["-kernel", str(kernel), "-append", append])
