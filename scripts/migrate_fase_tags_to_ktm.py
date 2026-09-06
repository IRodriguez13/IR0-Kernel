#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Rename FASE* harness log tags → KTM_* across smokes and Makefile grep."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Longest match first. File-specific overrides applied after global pass.
REPLACEMENTS: list[tuple[str, str]] = [
    # Harness source filenames in HARNESS_ID=
    ("init_fase58c_fbdev.c", "ktm_fbdev_gui_smoke.c"),
    ("init_fase58c_boot_halt.c", "ktm_boot_halt_smoke.c"),
    ("init_fase55a_doom_prereq.c", "ktm_doom_prereq_smoke.c"),
    ("init_fase54c_input_deterministic.c", "ktm_input_det_smoke.c"),
    ("init_fase54b_input.c", "ktm_input_smoke.c"),
    ("init_fase54a_fbdev.c", "ktm_fbdev_smoke.c"),
    ("init_fase53b_posix_pseudofs.c", "ktm_posix_pseudofs_smoke.c"),
    ("init_fase53a_fs_dev.c", "ktm_fs_dev_smoke.c"),
    ("init_fase52_tcc.c", "ktm_tcc_smoke.c"),
    ("init_fase50_busybox.c", "ktm_busybox_smoke.c"),
    ("init_fase50_exec_only.c", "ktm_exec_only_smoke.c"),
    ("fase58l_busybox_smoke.c", "ktm_busybox_manifest_smoke.c"),
    # BusyBox manifest (was FASE58L)
    ("FASE58L_HARNESS_ID", "KTM_BB_MANIFEST_HARNESS_ID"),
    ("[FASE58L]", "[KTM_BB_MANIFEST]"),
    ("FASE58L_", "KTM_BB_MANIFEST_"),
    # FS/dev (was FASE53A)
    ("FASE53A_FS_DEV_HARNESS_ID", "KTM_FS_DEV_HARNESS_ID"),
    ("FASE50_51_52_NO_REGRESSION", "KTM_LEGACY_STACK_NO_REGRESSION"),
    ("[FASE53A]", "[KTM_FS_DEV]"),
    ("FASE53A_", "KTM_FS_DEV_"),
    # POSIX pseudofs (was FASE53B)
    ("FASE53B_POSIX_PSEUDOFS_HARNESS_ID", "KTM_POSIX_PSEUDOFS_HARNESS_ID"),
    ("[FASE53B]", "[KTM_POSIX_PSEUDOFS]"),
    ("FASE53B_", "KTM_POSIX_PSEUDOFS_"),
    # Fbdev slice (was FASE54A)
    ("FASE54A_FBDEV_HARNESS_ID", "KTM_FBDEV_HARNESS_ID"),
    ("[FASE54A]", "[KTM_FBDEV]"),
    ("FASE54A_", "KTM_FBDEV_"),
    # Input (was FASE54B)
    ("FASE54B_INPUT_HARNESS_ID", "KTM_INPUT_HARNESS_ID"),
    ("[FASE54B]", "[KTM_INPUT]"),
    ("FASE54B_", "KTM_INPUT_"),
    # Input deterministic (was FASE54C)
    ("FASE54C_INPUT_DETERMINISTIC_HARNESS_ID", "KTM_INPUT_DET_HARNESS_ID"),
    ("FASE54C_INPUT_DETERMINISTIC_", "KTM_INPUT_DET_"),
    ("[FASE54C]", "[KTM_INPUT_DET]"),
    ("FASE54C_", "KTM_INPUT_DET_"),
    # Doom prereq (was FASE55A)
    ("FASE55A_DOOM_PREREQ_HARNESS_ID", "KTM_DOOM_PREREQ_HARNESS_ID"),
    ("[FASE55A]", "[KTM_DOOM_PREREQ]"),
    ("FASE55A_", "KTM_DOOM_PREREQ_"),
    # Doom stub/timing/generic (was FASE55B/C/D)
    ("FASE55B_DOOM_STUB_HARNESS_ID", "KTM_DOOM_STUB_HARNESS_ID"),
    ("[FASE55B]", "[KTM_DOOM_STUB]"),
    ("FASE55B_", "KTM_DOOM_STUB_"),
    ("[FASE55C]", "[KTM_DOOM_TIMING]"),
    ("FASE55C_", "KTM_DOOM_TIMING_"),
    ("FASE55D_DOOMGENERIC_OK", "KTM_DOOMGENERIC_OK"),
    ("[FASE55D]", "[KTM_DOOMGENERIC]"),
    ("FASE55D_", "KTM_DOOMGENERIC_"),
    ("RUNSV_FASE55D_START", "RUNSV_KTM_DOOMGENERIC_START"),
    ("FASE55D_DOOMGENERIC_REAL_WAD_OK", "KTM_DOOMGENERIC_REAL_WAD_OK"),
    # Fbdev GUI probe (was FASE58C in fbdev_gui smoke)
    ("FASE58C_FBDEV_HARNESS_ID", "KTM_FBDEV_GUI_HARNESS_ID"),
    ("[FASE58C]", "[KTM_FBDEV_GUI]"),
    ("FASE58C_FB_", "KTM_FBDEV_GUI_FB_"),
    ("FASE58C_", "KTM_FBDEV_GUI_"),
    # BusyBox tier smoke (was FASE50x)
    ("FASE50_BUSYBOX_HARNESS_ID", "KTM_BUSYBOX_HARNESS_ID"),
    ("FASE50_BUSYBOX_COREUTILS_MINIMAL_OK", "KTM_BUSYBOX_COREUTILS_MINIMAL_OK"),
    ("FASE50E_NO_REGRESSION_VERIFIED", "KTM_BUSYBOX_NO_REGRESSION_VERIFIED"),
    ("FASE50E_NO_REGRESSION", "KTM_BUSYBOX_NO_REGRESSION"),
    ("FASE50E_BASELINE_STABLE", "KTM_BUSYBOX_BASELINE_STABLE"),
    ("[FASE50E]", "[KTM_BUSYBOX_E]"),
    ("FASE50E_", "KTM_BUSYBOX_E_"),
    ("[FASE50D]", "[KTM_BUSYBOX_D]"),
    ("FASE50D_", "KTM_BUSYBOX_D_"),
    ("[FASE50C]", "[KTM_BUSYBOX_C]"),
    ("FASE50C_", "KTM_BUSYBOX_C_"),
    ("[FASE50B]", "[KTM_BUSYBOX_B]"),
    ("FASE50B_", "KTM_BUSYBOX_B_"),
    # Shell (was FASE51)
    ("DEBUG_FASE51_GATED", "KTM_SHELL_DEBUG_GATED"),
    ("[FASE51]", "[KTM_SHELL]"),
    ("FASE51_", "KTM_SHELL_"),
    # TCC (was FASE52)
    ("FASE52_TCC_HARNESS_ID", "KTM_TCC_HARNESS_ID"),
    ("DEBUG_FASE52_GATED", "KTM_TCC_DEBUG_GATED"),
    ("FASE52_FAIL_REASON", "KTM_TCC_FAIL_REASON"),
    ("FASE52_FAIL", "KTM_TCC_FAIL"),
    ("[FASE52]", "[KTM_TCC]"),
    ("FASE52D_", "KTM_TCC_D_"),
    ("FASE52C_", "KTM_TCC_C_"),
    ("FASE52B_", "KTM_TCC_B_"),
    ("FASE52_", "KTM_TCC_"),
    # Exec-only
    ("EXEC_ONLY_HARNESS_ID", "KTM_EXEC_ONLY_HARNESS_ID"),
    ("EXEC_ONLY_", "KTM_EXEC_ONLY_"),
]

FILE_OVERRIDES: dict[str, list[tuple[str, str]]] = {
    "setup/pid1/ktm_boot_halt_smoke.c": [
        ("KTM_FBDEV_GUI_BOOT_HALT", "KTM_BOOT_HALT_TAG"),
        ("KTM_FBDEV_GUI_BOOT_GUI_HOLD", "KTM_BOOT_HALT_GUI_HOLD"),
        ("KTM_FBDEV_GUI_OK", "KTM_BOOT_HALT_OK"),
    ],
}

# C identifier renames in tcc smoke (functions)
C_IDENT_REPLACEMENTS = [
    ("fase52d_large_file_harness", "ktm_tcc_d_large_file_harness"),
    ("fase52d_large_file_truncate", "ktm_tcc_d_large_file_truncate"),
    ("fase52_fail_msg", "ktm_tcc_fail_msg"),
    ("fase52_fail", "ktm_tcc_fail"),
    ("fase50d_emit_classify", "ktm_busybox_d_emit_classify"),
]

GLOBS = [
    "setup/pid1/ktm_*.c",
    "scripts/make/testing.mk",
    "setup/make/legacy-smokes.mk",
    "scripts/smoke_autokill.py",
    "scripts/migrate_fase_to_ktm.py",
]

FAIL_TAG_RE = re.compile(r"\[FASE[0-9A-Z]+\]\[FAIL\]")


def apply_replacements(text: str, pairs: list[tuple[str, str]]) -> str:
    for old, new in pairs:
        text = text.replace(old, new)
    return text


def patch_file(path: Path) -> bool:
    rel = str(path.relative_to(ROOT))
    orig = path.read_text(encoding="utf-8")
    text = apply_replacements(orig, REPLACEMENTS)
    text = apply_replacements(text, C_IDENT_REPLACEMENTS)
    if rel in FILE_OVERRIDES:
        text = apply_replacements(text, FILE_OVERRIDES[rel])
    text = FAIL_TAG_RE.sub(lambda m: m.group(0).replace("FASE", "KTM_", 1).replace("[KTM_", "[KTM_"), text)
    # Fix fail bracket tags: [FASE53A][FAIL] already handled by REPLACEMENTS
    if text != orig:
        path.write_text(text, encoding="utf-8")
        return True
    return False


def main() -> int:
    changed: list[str] = []
    for pattern in GLOBS:
        for path in sorted(ROOT.glob(pattern)):
            if path.is_file() and patch_file(path):
                changed.append(str(path.relative_to(ROOT)))
    # smoke_autokill fail patterns
    autokill = ROOT / "scripts/smoke_autokill.py"
    if autokill.is_file():
        t = autokill.read_text(encoding="utf-8")
        t2 = t.replace('r"FASE52_FAIL"', 'r"KTM_TCC_FAIL"')
        t2 = t2.replace(
            'r"BUSYBOX_FAIL_REASON=|FASE52_FAIL_REASON=|EXEC_ONLY_FAIL="',
            'r"BUSYBOX_FAIL_REASON=|KTM_TCC_FAIL_REASON=|KTM_EXEC_ONLY_FAIL="',
        )
        t2 = t2.replace(
            'r"\\[FASE[0-9A-Z]+\\]\\[FAIL\\]"',
            'r"\\[KTM_[A-Z0-9_]+\\]\\[FAIL\\]"',
        )
        if t2 != t:
            autokill.write_text(t2, encoding="utf-8")
            if "scripts/smoke_autokill.py" not in changed:
                changed.append("scripts/smoke_autokill.py")
    print(f"patched {len(changed)} files")
    for c in changed:
        print(f"  {c}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
