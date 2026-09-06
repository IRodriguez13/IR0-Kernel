#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""One-shot: rename setup/pid1/init_fase* → ktm_*_smoke.c and update Makefile wiring."""

from __future__ import annotations

import os
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# old_basename → new_basename (under setup/pid1/ unless noted)
RENAMES: dict[str, str] = {
    "init_fase41_reclaim.c": "ktm_reclaim_exit_smoke.c",
    "init_fase42_pt_reclaim.c": "ktm_pt_reclaim_smoke.c",
    "init_fase42_exec_storm.c": "ktm_exec_storm_smoke.c",
    "init_fase42_fork_exit_storm.c": "ktm_fork_exit_storm_smoke.c",
    "init_fase43_fork_exit_storm.c": "ktm_fork_exit_storm_deep_smoke.c",
    "init_fase43_fork_wait_storm.c": "ktm_fork_wait_storm_smoke.c",
    "init_fase43_exec_loop.c": "ktm_exec_loop_smoke.c",
    "init_fase44_fork_wait_drain.c": "ktm_fork_wait_drain_smoke.c",
    "init_fase44_exec_drain.c": "ktm_exec_drain_smoke.c",
    "init_fase44_init_exit_drain.c": "ktm_init_exit_drain_smoke.c",
    "init_fase45_fork_rollback_storm.c": "ktm_fork_rollback_smoke.c",
    "init_fase45_fork_mem_touch.c": "ktm_fork_mem_touch_smoke.c",
    "init_fase46_fork_no_recursion.c": "ktm_fork_no_recursion_smoke.c",
    "init_fase46_fork_heap.c": "ktm_fork_heap_smoke.c",
    "init_fase48_ipc.c": "ktm_ipc_smoke.c",
    "init_fase48_pingpong_only.c": "ktm_ipc_pingpong_smoke.c",
    "init_fase48_fork_pipes.c": "ktm_ipc_fork_pipes_smoke.c",
    "init_fase48_pipe2_only.c": "ktm_ipc_pipe2_smoke.c",
    "init_fase48_fork_pipes_close.c": "ktm_ipc_fork_pipes_close_smoke.c",
    "init_fase48_seq_test.c": "ktm_ipc_seq_smoke.c",
    "init_fase48_fork_only.c": "ktm_ipc_fork_only_smoke.c",
    "init_fase48_pipe_exec_only.c": "ktm_ipc_pipe_exec_smoke.c",
    "init_fase48_pipe_read.c": "ktm_ipc_pipe_read_smoke.c",
    "init_fase49_pipe.c": "ktm_pipe_smoke.c",
    "ktm_busybox_smoke.c": "ktm_busybox_smoke.c",
    "ktm_exec_only_smoke.c": "ktm_exec_only_smoke.c",
    "init_fase50_programs.c": "ktm_programs_smoke.c",
    "init_fase51_shell.c": "ktm_shell_smoke.c",
    "ktm_tcc_smoke.c": "ktm_tcc_smoke.c",
    "ktm_fs_dev_smoke.c": "ktm_fs_dev_smoke.c",
    "ktm_posix_pseudofs_smoke.c": "ktm_posix_pseudofs_smoke.c",
    "ktm_fbdev_smoke.c": "ktm_fbdev_smoke.c",
    "ktm_input_smoke.c": "ktm_input_smoke.c",
    "ktm_input_det_smoke.c": "ktm_input_det_smoke.c",
    "ktm_doom_prereq_smoke.c": "ktm_doom_prereq_smoke.c",
    "ktm_boot_halt_smoke.c": "ktm_boot_halt_smoke.c",
    "ktm_fbdev_gui_smoke.c": "ktm_fbdev_gui_smoke.c",
    "fase41_true.c": "ktm_true_helper.c",
    "fase48_cat.c": "ktm_ipc_cat_helper.c",
    "fase48_echo.c": "ktm_ipc_echo_helper.c",
    "fase48_busybox.c": "ktm_ipc_busybox_helper.c",
    "fase50_hello.c": "ktm_hello_helper.c",
    "ktm_busybox_manifest_smoke.c": "ktm_busybox_manifest_smoke.c",
}

# Makefile symbol / target renames (order: longer keys first)
SYMBOL_REPLACEMENTS: list[tuple[str, str]] = [
    ("INIT_KTM_FBDEV_GUI_FBDEV_SRC", "KTM_FBDEV_GUI_SMOKE_SRC"),
    ("INIT_KTM_FBDEV_GUI_BOOT_HALT_SRC", "KTM_BOOT_HALT_SMOKE_SRC"),
    ("INIT_KTM_DOOMGENERIC_DOOMGENERIC_SRC", "KTM_DOOMGENERIC_SRC"),
    ("INIT_KTM_DOOM_TIMING_TIMING_INPUT_SRC", "KTM_DOOM_TIMING_STUB_SRC"),
    ("INIT_KTM_DOOM_STUB_DOOM_STUB_SRC", "KTM_DOOM_STUB_SRC"),
    ("INIT_KTM_DOOM_PREREQ_DOOM_PREREQ_SRC", "KTM_DOOM_PREREQ_SMOKE_SRC"),
    ("INIT_KTM_INPUT_DET_INPUT_DET_SRC", "KTM_INPUT_DET_SMOKE_SRC"),
    ("INIT_KTM_INPUT_INPUT_SRC", "KTM_INPUT_SMOKE_SRC"),
    ("INIT_KTM_FBDEV_FBDEV_SRC", "KTM_FBDEV_SMOKE_SRC"),
    ("INIT_KTM_POSIX_PSEUDOFS_POSIX_PSEUDOFS_SRC", "KTM_POSIX_PSEUDOFS_SMOKE_SRC"),
    ("INIT_KTM_FS_DEV_FS_DEV_SRC", "KTM_FS_DEV_SMOKE_SRC"),
    ("INIT_FASE50_PROGRAMS_SRC", "KTM_PROGRAMS_SMOKE_SRC"),
    ("INIT_FASE50_KTM_EXEC_ONLY_SRC", "KTM_KTM_EXEC_ONLY_SMOKE_SRC"),
    ("INIT_FASE50_BUSYBOX_SRC", "KTM_BUSYBOX_SMOKE_SRC"),
    ("INIT_FASE49_PIPE_SRC", "KTM_PIPE_SMOKE_SRC"),
    ("INIT_FASE48_IPC_SRC", "KTM_IPC_SMOKE_SRC"),
    ("INIT_FASE46_FORK_NO_RECURSE_SRC", "KTM_FORK_NO_RECURSE_SMOKE_SRC"),
    ("INIT_FASE46_FORK_HEAP_SRC", "KTM_FORK_HEAP_SMOKE_SRC"),
    ("INIT_FASE45_FORK_ROLLBACK_STORM_SRC", "KTM_FORK_ROLLBACK_SMOKE_SRC"),
    ("INIT_FASE45_FORK_MEM_TOUCH_SRC", "KTM_FORK_MEM_TOUCH_SMOKE_SRC"),
    ("INIT_FASE44_INIT_EXIT_DRAIN_SRC", "KTM_INIT_EXIT_DRAIN_SMOKE_SRC"),
    ("INIT_FASE44_EXEC_DRAIN_SRC", "KTM_EXEC_DRAIN_SMOKE_SRC"),
    ("INIT_FASE44_FORK_WAIT_DRAIN_SRC", "KTM_FORK_WAIT_DRAIN_SMOKE_SRC"),
    ("INIT_FASE43_FORK_WAIT_STORM_SRC", "KTM_FORK_WAIT_STORM_SMOKE_SRC"),
    ("INIT_FASE43_FORK_EXIT_STORM_SRC", "KTM_FORK_EXIT_STORM_DEEP_SMOKE_SRC"),
    ("INIT_FASE43_EXEC_LOOP_SRC", "KTM_EXEC_LOOP_SMOKE_SRC"),
    ("INIT_FASE42_FORK_EXIT_STORM_SRC", "KTM_FORK_EXIT_STORM_SMOKE_SRC"),
    ("INIT_FASE42_EXEC_STORM_SRC", "KTM_EXEC_STORM_SMOKE_SRC"),
    ("INIT_FASE42_PT_RECLAIM_SRC", "KTM_PT_RECLAIM_SMOKE_SRC"),
    ("INIT_FASE41_RECLAIM_SRC", "KTM_RECLAIM_EXIT_SMOKE_SRC"),
    ("INIT_KTM_TCC_TCC_SRC", "KTM_TCC_SMOKE_SRC"),
    ("INIT_KTM_SHELL_SHELL_SRC", "KTM_SHELL_SMOKE_SRC"),
    ("KTM_BB_MANIFEST_SMOKE_SRC", "KTM_BUSYBOX_MANIFEST_SMOKE_SRC"),
    ("KTM_BB_MANIFEST_SMOKE_BIN", "KTM_BUSYBOX_MANIFEST_SMOKE_BIN"),
    ("KTM_BB_MANIFEST_SMOKE_LOG", "KTM_BUSYBOX_MANIFEST_SMOKE_LOG"),
    ("FASE50_HELLO_SRC", "KTM_HELLO_HELPER_SRC"),
    ("FASE50_HELLO_BIN", "KTM_HELLO_HELPER_BIN"),
    ("FASE48_BUSYBOX_SRC", "KTM_IPC_BUSYBOX_HELPER_SRC"),
    ("FASE48_BUSYBOX_BIN", "KTM_IPC_BUSYBOX_HELPER_BIN"),
    ("FASE48_ECHO_SRC", "KTM_IPC_ECHO_HELPER_SRC"),
    ("FASE48_ECHO_BIN", "KTM_IPC_ECHO_HELPER_BIN"),
    ("FASE48_CAT_SRC", "KTM_IPC_CAT_HELPER_SRC"),
    ("FASE48_CAT_BIN", "KTM_IPC_CAT_HELPER_BIN"),
    ("FASE41_TRUE_SRC", "KTM_TRUE_HELPER_SRC"),
    ("FASE41_TRUE_BIN", "KTM_TRUE_HELPER_BIN"),
    ("build-init-fase58c-fbdev", "build-ktm-fbdev-gui-smoke"),
    ("build-init-fase58c-boot-halt", "build-ktm-boot-halt-smoke"),
    ("build-init-fase55a-doom-prereq", "build-ktm-doom-prereq-smoke"),
    ("build-init-fase54c-input-deterministic", "build-ktm-input-det-smoke"),
    ("build-init-fase54b-input", "build-ktm-input-smoke"),
    ("build-init-fase54a-fbdev", "build-ktm-fbdev-smoke"),
    ("build-init-fase53b-posix-pseudofs", "build-ktm-posix-pseudofs-smoke"),
    ("build-init-fase53a-fs-dev", "build-ktm-fs-dev-smoke"),
    ("build-init-fase52-tcc", "build-ktm-tcc-smoke"),
    ("build-init-fase51-shell", "build-ktm-shell-smoke"),
    ("build-init-fase50-programs", "build-ktm-programs-smoke"),
    ("build-init-fase50-exec-only", "build-ktm-exec-only-smoke"),
    ("build-init-fase50-busybox", "build-ktm-busybox-smoke"),
    ("build-init-fase49-pipe", "build-ktm-pipe-smoke"),
    ("build-init-fase48-ipc", "build-ktm-ipc-smoke"),
    ("build-init-fase46-fork-heap", "build-ktm-fork-heap-smoke"),
    ("build-init-fase46-fork-no-recursion", "build-ktm-fork-no-recursion-smoke"),
    ("build-init-fase45-fork-mem-touch", "build-ktm-fork-mem-touch-smoke"),
    ("build-init-fase45-fork-rollback-storm", "build-ktm-fork-rollback-smoke"),
    ("build-init-fase44-init-exit-drain", "build-ktm-init-exit-drain-smoke"),
    ("build-init-fase44-exec-drain", "build-ktm-exec-drain-smoke"),
    ("build-init-fase44-fork-wait-drain", "build-ktm-fork-wait-drain-smoke"),
    ("build-init-fase43-exec-loop", "build-ktm-exec-loop-smoke"),
    ("build-init-fase43-fork-wait-storm", "build-ktm-fork-wait-storm-smoke"),
    ("build-init-fase43-fork-exit-storm", "build-ktm-fork-exit-storm-deep-smoke"),
    ("build-init-fase42-fork-exit-storm", "build-ktm-fork-exit-storm-smoke"),
    ("build-init-fase42-exec-storm", "build-ktm-exec-storm-smoke"),
    ("build-init-fase42-pt-reclaim", "build-ktm-pt-reclaim-smoke"),
    ("build-init-fase41-reclaim", "build-ktm-reclaim-exit-smoke"),
    ("build-fase58l-busybox-smoke", "build-ktm-busybox-manifest-smoke"),
    ("build-fase58c-fbdev", "build-ktm-fbdev-gui-bin"),
    ("build-fase58c-boot-halt", "build-ktm-boot-halt-bin"),
    ("build-init-fase52-tcc", "build-ktm-tcc-smoke"),
    ("build-tcc-fase52", "build-ktm-tcc-toolchain"),
    ("KTM_FBDEV_GUI_FBDEV_BIN", "KTM_FBDEV_GUI_BIN"),
    ("KTM_FBDEV_GUI_BOOT_BIN", "KTM_BOOT_HALT_BIN"),
    ("KTM_TCC_HARNESS_BIN", "KTM_TCC_HARNESS_BIN"),
    ("KTM_TCC_TCC_STAGE", "KTM_TCC_STAGE"),
    ("KTM_TCC_TCC_LOG", "KTM_TCC_SMOKE_LOG"),
    ("setup/pid1/fase58c_fbdev", "setup/pid1/ktm_fbdev_gui"),
    ("setup/pid1/fase58c_boot_halt", "setup/pid1/ktm_boot_halt"),
    ("setup/pid1/fase52_harness", "setup/pid1/ktm_tcc_harness"),
    ("setup/pid1/fase52_staging", "setup/pid1/ktm_tcc_staging"),
    ("build-fase50-hello", "build-ktm-hello-helper"),
    ("build-fase48-busybox", "build-ktm-ipc-busybox-helper"),
    ("build-fase48-echo", "build-ktm-ipc-echo-helper"),
    ("build-fase48-cat", "build-ktm-ipc-cat-helper"),
    ("build-fase41-true", "build-ktm-true-helper"),
]

TEXT_GLOBS = [
    "scripts/make/testing.mk",
    "setup/make/legacy-smokes.mk",
    "Makefile",
    ".gitignore",
    "scripts/inject_devtools_minix.sh",
    "scripts/ktm_prepare_runit_hostshare_disk.sh",
    "setup/tcc/build-fase52.sh",
    "setup/pid1/ktm_tcc_smoke.c",
    "scripts/repo_hygiene_guard.py",
]

SYMBOL_REPLACEMENTS.sort(key=lambda x: len(x[0]), reverse=True)


def git_mv(src: Path, dst: Path) -> None:
    if not src.is_file():
        return
    dst.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(["git", "mv", str(src), str(dst)], cwd=ROOT, check=True)


def main() -> None:
    pid1 = ROOT / "setup" / "pid1"
    for old, new in RENAMES.items():
        src = pid1 / old
        dst = pid1 / new
        if src.is_file():
            print(f"mv {old} → {new}")
            git_mv(src, dst)

    # Path string replacements in tracked files
    path_pairs = [(f"setup/pid1/{o}", f"setup/pid1/{n}") for o, n in RENAMES.items()]
    path_pairs.sort(key=lambda x: len(x[0]), reverse=True)

    for rel in TEXT_GLOBS:
        path = ROOT / rel
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8")
        orig = text
        for old, new in path_pairs:
            text = text.replace(old, new)
        for old, new in SYMBOL_REPLACEMENTS:
            text = text.replace(old, new)
        if text != orig:
            path.write_text(text, encoding="utf-8")
            print(f"updated {rel}")

    # Broad replace in setup/pid1 C sources (includes)
    for c in (ROOT / "setup" / "pid1").glob("*.c"):
        text = c.read_text(encoding="utf-8")
        orig = text
        for old, new in path_pairs:
            text = text.replace(old, new)
        if text != orig:
            c.write_text(text, encoding="utf-8")
            print(f"updated {c.relative_to(ROOT)}")


if __name__ == "__main__":
    os.chdir(ROOT)
    main()
