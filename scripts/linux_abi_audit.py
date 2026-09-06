#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
linux_abi_audit.py — Linux↔IR0 ground-truth audit orchestrator (D1.19).

Runs paired workloads, host tests, ktests where configured, and emits one report.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts" / "linux_abi"))

from compare import (  # noqa: E402
    CompareResult,
    compare_brk,
    compare_chdir,
    compare_dup,
    compare_execve,
    compare_fcntl,
    compare_getcwd,
    compare_ioctl,
    compare_mmap,
    compare_mount,
    compare_nanosleep,
    compare_openat,
    compare_pipe,
    compare_poll,
    compare_process_lifecycle,
    compare_kill_sigterm,
    compare_read,
    compare_sigreturn_blocked_syscall,
    compare_stat,
    compare_vfs_write,
    compare_wait4,
    compare_wait4_wnohang,
    render_markdown,
)


def load_contracts() -> dict:
    path = ROOT / "scripts" / "linux_abi" / "contracts.json"
    return json.loads(path.read_text())


def run_cmd(cmd: list[str], *, cwd: Path | None = None) -> int:
    print(f"  RUN  {' '.join(cmd)}")
    return subprocess.run(cmd, cwd=cwd or ROOT, check=False).returncode


def build_brk_probe(report_dir: Path) -> Path:
    return build_static_probe(
        report_dir / "brk_probe",
        ROOT / "scripts" / "linux_abi" / "workloads" / "brk_probe.c",
    )


def build_wait4_probe(report_dir: Path) -> Path:
    return build_static_probe(
        report_dir / "wait4_probe",
        ROOT / "scripts" / "linux_abi" / "workloads" / "wait4_probe.c",
    )


def build_wait4_wnohang_probe(report_dir: Path) -> Path:
    return build_static_probe(
        report_dir / "wait4_wnohang_probe",
        ROOT / "scripts" / "linux_abi" / "workloads" / "wait4_wnohang_probe.c",
    )


def build_kill_sigterm_probe(report_dir: Path) -> Path:
    return build_static_probe(
        report_dir / "kill_sigterm_probe",
        ROOT / "scripts" / "linux_abi" / "workloads" / "kill_sigterm_probe.c",
    )


def build_read_probe(report_dir: Path) -> Path:
    return build_static_probe(
        report_dir / "read_probe",
        ROOT / "scripts" / "linux_abi" / "workloads" / "read_probe.c",
    )


def build_mmap_probe(report_dir: Path) -> Path:
    return build_static_probe(
        report_dir / "mmap_probe",
        ROOT / "scripts" / "linux_abi" / "workloads" / "mmap_probe.c",
    )


def build_mount_probe(report_dir: Path) -> Path:
    return build_static_probe(
        report_dir / "mount_probe",
        ROOT / "scripts" / "linux_abi" / "workloads" / "mount_probe.c",
    )


def build_execve_probe(report_dir: Path) -> tuple[Path, Path]:
    helper = build_static_probe(
        report_dir / "exec_helper",
        ROOT / "scripts" / "linux_abi" / "workloads" / "exec_helper.c",
    )
    probe_out = report_dir / "execve_probe"
    probe_src = ROOT / "scripts" / "linux_abi" / "workloads" / "execve_probe.c"
    musl_cc = "musl-gcc"
    try:
        subprocess.run(["musl-gcc", "--version"], capture_output=True, check=True)
    except (FileNotFoundError, subprocess.CalledProcessError):
        musl_cc = "gcc"
    helper_path = "/sbin/exec_helper"
    rc = run_cmd(
        [
            musl_cc,
            "-static",
            "-Os",
            f'-DEXEC_HELPER_PATH="{helper_path}"',
            "-o",
            str(probe_out),
            str(probe_src),
        ]
    )
    if rc != 0 or not probe_out.is_file():
        raise RuntimeError(f"failed to build {probe_out.name} with {musl_cc}")
    return probe_out, helper


def build_openat_probe(report_dir: Path) -> Path:
    out = report_dir / "openat_probe"
    src = ROOT / "scripts" / "linux_abi" / "workloads" / "openat_probe.c"
    out.parent.mkdir(parents=True, exist_ok=True)
    musl_cc = "musl-gcc"
    try:
        subprocess.run(["musl-gcc", "--version"], capture_output=True, check=True)
    except (FileNotFoundError, subprocess.CalledProcessError):
        musl_cc = "gcc"
    ir0_path = "/proc/uptime"
    rc = run_cmd(
        [
            musl_cc,
            "-static",
            "-Os",
            f'-DOPEN_EXISTING_PATH="{ir0_path}"',
            "-o",
            str(out),
            str(src),
        ]
    )
    if rc != 0 or not out.is_file():
        raise RuntimeError(f"failed to build {out.name}")
    return out


def build_stat_probe(report_dir: Path) -> Path:
    return build_static_probe(
        report_dir / "stat_probe",
        ROOT / "scripts" / "linux_abi" / "workloads" / "stat_probe.c",
    )


def build_vfs_write_probe(report_dir: Path) -> Path:
    return build_static_probe(
        report_dir / "vfs_write_probe",
        ROOT / "scripts" / "linux_abi" / "workloads" / "vfs_write_probe.c",
    )


def build_poll_probe(report_dir: Path) -> Path:
    return build_static_probe(
        report_dir / "poll_probe",
        ROOT / "scripts" / "linux_abi" / "workloads" / "poll_probe.c",
    )


def build_nanosleep_probe(report_dir: Path) -> Path:
    return build_static_probe(
        report_dir / "nanosleep_probe",
        ROOT / "scripts" / "linux_abi" / "workloads" / "nanosleep_probe.c",
    )


def build_sigreturn_blocked_syscall_probe(report_dir: Path) -> Path:
    return build_static_probe(
        report_dir / "sigreturn_blocked_syscall_probe",
        ROOT
        / "scripts"
        / "linux_abi"
        / "workloads"
        / "sigreturn_blocked_syscall_probe.c",
    )


def build_getcwd_probe(report_dir: Path) -> Path:
    return build_static_probe(
        report_dir / "getcwd_probe",
        ROOT / "scripts" / "linux_abi" / "workloads" / "getcwd_probe.c",
    )


def build_ioctl_tty_probe(report_dir: Path) -> Path:
    return build_static_probe(
        report_dir / "ioctl_tty_probe",
        ROOT / "scripts" / "linux_abi" / "workloads" / "ioctl_tty_probe.c",
    )


def build_fcntl_probe(report_dir: Path) -> Path:
    return build_static_probe(
        report_dir / "fcntl_probe",
        ROOT / "scripts" / "linux_abi" / "workloads" / "fcntl_probe.c",
    )


def build_chdir_probe(report_dir: Path, target: str) -> Path:
    out = report_dir / "chdir_probe"
    src = ROOT / "scripts" / "linux_abi" / "workloads" / "chdir_probe.c"
    out.parent.mkdir(parents=True, exist_ok=True)
    musl_cc = "musl-gcc"
    try:
        subprocess.run(["musl-gcc", "--version"], capture_output=True, check=True)
    except (FileNotFoundError, subprocess.CalledProcessError):
        musl_cc = "gcc"
    rc = run_cmd(
        [
            musl_cc,
            "-static",
            "-Os",
            f'-DCHDIR_TARGET="{target}"',
            "-o",
            str(out),
            str(src),
        ]
    )
    if rc != 0 or not out.is_file():
        raise RuntimeError(f"failed to build {out.name} with {musl_cc}")
    return out


def build_dup_probe(report_dir: Path, existing_path: str) -> Path:
    out = report_dir / "dup_probe"
    src = ROOT / "scripts" / "linux_abi" / "workloads" / "dup_probe.c"
    out.parent.mkdir(parents=True, exist_ok=True)
    musl_cc = "musl-gcc"
    try:
        subprocess.run(["musl-gcc", "--version"], capture_output=True, check=True)
    except (FileNotFoundError, subprocess.CalledProcessError):
        musl_cc = "gcc"
    rc = run_cmd(
        [
            musl_cc,
            "-static",
            "-Os",
            f'-DDUP_EXISTING_PATH="{existing_path}"',
            "-o",
            str(out),
            str(src),
        ]
    )
    if rc != 0 or not out.is_file():
        raise RuntimeError(f"failed to build {out.name} with {musl_cc}")
    return out


def _run_simple_workloads(
    contract: str,
    probe_basename: str,
    done_tag: str,
    strace_syscalls: str,
    linux_dir: Path,
    ir0_dir: Path,
) -> bool:
    sh_linux = ROOT / "scripts" / "linux_abi" / "run_linux_workload.sh"
    sh_ir0 = ROOT / "scripts" / "linux_abi" / "run_ir0_workload.sh"
    if run_cmd(
        ["bash", str(sh_linux), contract, probe_basename, strace_syscalls, str(linux_dir)]
    ) != 0:
        return False
    if run_cmd(
        ["bash", str(sh_ir0), contract, probe_basename, done_tag, str(ir0_dir)]
    ) != 0:
        return False
    return True


def build_static_probe(out: Path, src: Path) -> Path:
    out.parent.mkdir(parents=True, exist_ok=True)
    musl_cc = "musl-gcc"
    try:
        subprocess.run(["musl-gcc", "--version"], capture_output=True, check=True)
    except (FileNotFoundError, subprocess.CalledProcessError):
        musl_cc = "gcc"
    rc = run_cmd([musl_cc, "-static", "-Os", "-o", str(out), str(src)])
    if rc != 0 or not out.is_file():
        raise RuntimeError(f"failed to build {out.name} with {musl_cc}")
    return out


def run_host_stat_test() -> bool | None:
    if os.environ.get("LINUX_ABI_SKIP_HOST"):
        return None
    proc = subprocess.run(
        ["make", "-s", "-C", "tests/host", "run"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    combined = proc.stdout + proc.stderr
    m = re.search(r"stat_user_abi\s*\.\.\.\s*(PASS|FAIL)", combined)
    if m:
        return m.group(1) == "PASS"
    return proc.returncode == 0


def run_host_brk_test() -> bool | None:
    if os.environ.get("LINUX_ABI_SKIP_HOST"):
        return None
    proc = subprocess.run(
        ["make", "-s", "-C", "tests/host", "run"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    combined = proc.stdout + proc.stderr
    m = re.search(r"elf_initial_brk_abi\s*\.\.\.\s*(PASS|FAIL)", combined)
    if m:
        return m.group(1) == "PASS"
    return proc.returncode == 0


def run_ktest_brk(name: str) -> bool | None:
    if os.environ.get("LINUX_ABI_SKIP_KTEST"):
        return None
    rc = run_cmd(["make", "-s", "kernel-tests"])
    log = Path("/tmp/ktest.log")
    if not log.is_file():
        return False if rc != 0 else None
    text = log.read_text(errors="replace")
    if f"[KTEST] {name} ... PASS" in text:
        return True
    if f"[KTEST] {name} ... FAIL" in text:
        return False
    return rc == 0


def audit_brk(report_dir: Path, cfg: dict) -> CompareResult:
    linux_dir = report_dir / "linux" / "brk"
    ir0_dir = report_dir / "ir0" / "brk"
    grow = int(cfg.get("grow_bytes", 4096))
    ktest_log = Path("/tmp/ktest.log")

    build_brk_probe(report_dir)

    sh_linux = ROOT / "scripts" / "linux_abi" / "run_linux_brk.sh"
    sh_ir0 = ROOT / "scripts" / "linux_abi" / "run_ir0_brk.sh"

    host_ok = run_host_brk_test()
    ktest_name = cfg.get("ktest", "brk_post_exec")

    if os.environ.get("LINUX_ABI_SKIP_KTEST"):
        ktest_ok = None
    elif ktest_log.is_file() and "[KTEST] brk_post_exec ... PASS" in ktest_log.read_text(errors="replace"):
        ktest_ok = True
        print(f"  NOTE  reusing brk ktest evidence from {ktest_log}")
    else:
        ktest_ok = None

    if run_cmd(["bash", str(sh_linux), str(linux_dir)]) != 0:
        res = CompareResult(
            contract="brk",
            ok=False,
            divergences=["Linux brk workload script failed"],
        )
        if host_ok is False:
            res.divergences.append("host test elf_initial_brk_abi FAILED")
        return res

    if run_cmd(["bash", str(sh_ir0), str(ir0_dir)]) != 0:
        ir0_trace_path = ir0_dir / "trace.json"
        if ir0_trace_path.is_file():
            ir0_trace = json.loads(ir0_trace_path.read_text())
            if len(ir0_trace.get("audit_steps") or []) >= 2:
                pass
            else:
                res = CompareResult(
                    contract="brk",
                    ok=False,
                    divergences=["IR0 brk workload script failed"],
                )
                if host_ok is False:
                    res.divergences.append("host test elf_initial_brk_abi FAILED")
                return res
        else:
            res = CompareResult(
                contract="brk",
                ok=False,
                divergences=["IR0 brk workload script failed"],
            )
            if host_ok is False:
                res.divergences.append("host test elf_initial_brk_abi FAILED")
            return res

    linux_trace = json.loads((linux_dir / "trace.json").read_text())
    if not (ir0_dir / "trace.json").is_file():
        ir0_trace = {"audit_steps": []}
    else:
        ir0_trace = json.loads((ir0_dir / "trace.json").read_text())
    res = compare_brk(linux_trace, ir0_trace, grow, host_ok, None)

    if ktest_ok is None:
        ktest_ok = run_ktest_brk(ktest_name)
    if ktest_ok is False:
        res.ok = False
        res.divergences.append(f"ktest {ktest_name} FAILED")
    elif ktest_ok is True:
        res.notes.append(f"ktest {ktest_name} OK")

    if host_ok is False:
        res.ok = False
        if "host test elf_initial_brk_abi FAILED" not in res.divergences:
            res.divergences.append("host test elf_initial_brk_abi FAILED")
    elif host_ok is True and "host test elf_initial_brk_abi OK" not in res.notes:
        res.notes.append("host test elf_initial_brk_abi OK")

    return res


def audit_wait4(report_dir: Path, cfg: dict) -> CompareResult:
    linux_dir = report_dir / "linux" / "wait4"
    ir0_dir = report_dir / "ir0" / "wait4"
    child_exit = int(cfg.get("child_exit_status", 42))
    ktest_name = cfg.get("ktest", "wait4_status")
    ktest_log = Path("/tmp/ktest.log")

    build_wait4_probe(report_dir)

    sh_linux = ROOT / "scripts" / "linux_abi" / "run_linux_wait4.sh"
    sh_ir0 = ROOT / "scripts" / "linux_abi" / "run_ir0_wait4.sh"

    if os.environ.get("LINUX_ABI_SKIP_KTEST"):
        ktest_ok = None
    elif ktest_log.is_file() and f"[KTEST] {ktest_name} ... PASS" in ktest_log.read_text(
        errors="replace"
    ):
        ktest_ok = True
        print(f"  NOTE  reusing wait4 ktest evidence from {ktest_log}")
    else:
        ktest_ok = None

    if run_cmd(["bash", str(sh_linux), str(linux_dir)]) != 0:
        return CompareResult(
            contract="wait4",
            ok=False,
            divergences=["Linux wait4 workload script failed"],
        )

    if run_cmd(["bash", str(sh_ir0), str(ir0_dir)]) != 0:
        ir0_trace_path = ir0_dir / "trace.json"
        if ir0_trace_path.is_file():
            ir0_trace = json.loads(ir0_trace_path.read_text())
            if len(ir0_trace.get("audit_steps") or []) < 2:
                return CompareResult(
                    contract="wait4",
                    ok=False,
                    divergences=["IR0 wait4 workload script failed"],
                )
        else:
            return CompareResult(
                contract="wait4",
                ok=False,
                divergences=["IR0 wait4 workload script failed"],
            )

    linux_trace = json.loads((linux_dir / "trace.json").read_text())
    if not (ir0_dir / "trace.json").is_file():
        ir0_trace = {"audit_steps": []}
    else:
        ir0_trace = json.loads((ir0_dir / "trace.json").read_text())

    if ktest_ok is None:
        ktest_ok = run_ktest_brk(ktest_name)
    res = compare_wait4(linux_trace, ir0_trace, child_exit, None)
    if ktest_ok is False:
        res.ok = False
        if f"ktest {ktest_name} FAILED" not in res.divergences:
            res.divergences.append(f"ktest {ktest_name} FAILED")
    elif ktest_ok is True and f"ktest {ktest_name} OK" not in res.notes:
        res.notes.append(f"ktest {ktest_name} OK")

    return res


def audit_read(report_dir: Path, cfg: dict) -> CompareResult:
    linux_dir = report_dir / "linux" / "read"
    ir0_dir = report_dir / "ir0" / "read"
    pipe_read_len = int(cfg.get("pipe_read_len", 6))
    pipe_data_hex = str(cfg.get("pipe_data_hex", "68656c6c6f0a"))
    ebadf_errno = int(cfg.get("ebadf_errno", 9))
    ktest_name = cfg.get("ktest", "syscall_pipe")
    ktest_log = Path("/tmp/ktest.log")

    build_read_probe(report_dir)

    sh_linux = ROOT / "scripts" / "linux_abi" / "run_linux_read.sh"
    sh_ir0 = ROOT / "scripts" / "linux_abi" / "run_ir0_read.sh"

    ktest_ok = None
    if not os.environ.get("LINUX_ABI_SKIP_KTEST"):
        if ktest_log.is_file() and f"[KTEST] {ktest_name} ... PASS" in ktest_log.read_text(
            errors="replace"
        ):
            ktest_ok = True
            print(f"  NOTE  reusing read ktest evidence from {ktest_log}")

    if run_cmd(["bash", str(sh_linux), str(linux_dir)]) != 0:
        return CompareResult(
            contract="read",
            ok=False,
            divergences=["Linux read workload script failed"],
        )

    if run_cmd(["bash", str(sh_ir0), str(ir0_dir)]) != 0:
        ir0_trace_path = ir0_dir / "trace.json"
        if ir0_trace_path.is_file():
            ir0_trace = json.loads(ir0_trace_path.read_text())
            if len(ir0_trace.get("audit_steps") or []) < 3:
                return CompareResult(
                    contract="read",
                    ok=False,
                    divergences=["IR0 read workload script failed"],
                )
        else:
            return CompareResult(
                contract="read",
                ok=False,
                divergences=["IR0 read workload script failed"],
            )

    linux_trace = json.loads((linux_dir / "trace.json").read_text())
    if not (ir0_dir / "trace.json").is_file():
        ir0_trace = {"audit_steps": []}
    else:
        ir0_trace = json.loads((ir0_dir / "trace.json").read_text())

    if ktest_ok is None and not os.environ.get("LINUX_ABI_SKIP_KTEST"):
        ktest_ok = run_ktest_brk(ktest_name)

    res = compare_read(
        linux_trace,
        ir0_trace,
        pipe_read_len,
        pipe_data_hex,
        ebadf_errno,
        None,
    )
    if ktest_ok is False:
        res.ok = False
        if f"ktest {ktest_name} FAILED" not in res.divergences:
            res.divergences.append(f"ktest {ktest_name} FAILED")
    elif ktest_ok is True and f"ktest {ktest_name} OK" not in res.notes:
        res.notes.append(f"ktest {ktest_name} OK")

    return res


def audit_mmap(report_dir: Path, cfg: dict) -> CompareResult:
    linux_dir = report_dir / "linux" / "mmap"
    ir0_dir = report_dir / "ir0" / "mmap"
    page_size = int(cfg.get("page_size", 4096))
    none_size = int(cfg.get("none_size", 8192))
    verify_data_hex = str(cfg.get("verify_data_hex", "4d4d41504f4b0a"))
    ebadf_errno = int(cfg.get("ebadf_errno", 9))
    ktest_name = cfg.get("ktest", "mmap_null_placement")
    ktest_log = Path("/tmp/ktest.log")

    build_mmap_probe(report_dir)

    sh_linux = ROOT / "scripts" / "linux_abi" / "run_linux_mmap.sh"
    sh_ir0 = ROOT / "scripts" / "linux_abi" / "run_ir0_mmap.sh"

    ktest_ok = None
    if not os.environ.get("LINUX_ABI_SKIP_KTEST"):
        if ktest_log.is_file() and f"[KTEST] {ktest_name} ... PASS" in ktest_log.read_text(
            errors="replace"
        ):
            ktest_ok = True
            print(f"  NOTE  reusing mmap ktest evidence from {ktest_log}")

    if run_cmd(["bash", str(sh_linux), str(linux_dir)]) != 0:
        return CompareResult(
            contract="mmap",
            ok=False,
            divergences=["Linux mmap workload script failed"],
        )

    if run_cmd(["bash", str(sh_ir0), str(ir0_dir)]) != 0:
        ir0_trace_path = ir0_dir / "trace.json"
        if ir0_trace_path.is_file():
            ir0_trace = json.loads(ir0_trace_path.read_text())
            if len(ir0_trace.get("audit_steps") or []) < 7:
                return CompareResult(
                    contract="mmap",
                    ok=False,
                    divergences=["IR0 mmap workload script failed"],
                )
        else:
            return CompareResult(
                contract="mmap",
                ok=False,
                divergences=["IR0 mmap workload script failed"],
            )

    linux_trace = json.loads((linux_dir / "trace.json").read_text())
    if not (ir0_dir / "trace.json").is_file():
        ir0_trace = {"audit_steps": []}
    else:
        ir0_trace = json.loads((ir0_dir / "trace.json").read_text())

    if ktest_ok is None and not os.environ.get("LINUX_ABI_SKIP_KTEST"):
        ktest_ok = run_ktest_brk(ktest_name)

    res = compare_mmap(
        linux_trace,
        ir0_trace,
        page_size,
        none_size,
        verify_data_hex,
        ebadf_errno,
        None,
    )
    if ktest_ok is False:
        res.ok = False
        if f"ktest {ktest_name} FAILED" not in res.divergences:
            res.divergences.append(f"ktest {ktest_name} FAILED")
    elif ktest_ok is True and f"ktest {ktest_name} OK" not in res.notes:
        res.notes.append(f"ktest {ktest_name} OK")

    return res


def audit_mount(report_dir: Path, cfg: dict) -> CompareResult:
    linux_dir = report_dir / "linux" / "mount"
    ir0_dir = report_dir / "ir0" / "mount"
    mount_path = str(cfg.get("mount_path", "/tmp/ir0mnt"))
    mount_noent_path = str(cfg.get("mount_noent_path", "/tmp/ir0mnt_nope"))
    rw_data_hex = str(cfg.get("rw_data_hex", "6d6e746f6b0a"))
    enoent_errno = int(cfg.get("enoent_errno", 2))
    enodev_errno = int(cfg.get("enodev_errno", 19))
    ktest_name = cfg.get("ktest", "mount_tmpfs_contract")
    ktest_log = Path("/tmp/ktest.log")

    build_mount_probe(report_dir)

    sh_linux = ROOT / "scripts" / "linux_abi" / "run_linux_mount.sh"
    sh_ir0 = ROOT / "scripts" / "linux_abi" / "run_ir0_mount.sh"

    ktest_ok = None
    if not os.environ.get("LINUX_ABI_SKIP_KTEST"):
        if ktest_log.is_file() and f"[KTEST] {ktest_name} ... PASS" in ktest_log.read_text(
            errors="replace"
        ):
            ktest_ok = True
            print(f"  NOTE  reusing mount ktest evidence from {ktest_log}")

    if run_cmd(["bash", str(sh_linux), str(linux_dir)]) != 0:
        return CompareResult(
            contract="mount",
            ok=False,
            divergences=["Linux mount workload script failed"],
        )

    if run_cmd(["bash", str(sh_ir0), str(ir0_dir)]) != 0:
        ir0_trace_path = ir0_dir / "trace.json"
        if ir0_trace_path.is_file():
            ir0_trace = json.loads(ir0_trace_path.read_text())
            if len(ir0_trace.get("audit_steps") or []) < 5:
                return CompareResult(
                    contract="mount",
                    ok=False,
                    divergences=["IR0 mount workload script failed"],
                )
        else:
            return CompareResult(
                contract="mount",
                ok=False,
                divergences=["IR0 mount workload script failed"],
            )

    linux_trace = json.loads((linux_dir / "trace.json").read_text())
    if not (ir0_dir / "trace.json").is_file():
        ir0_trace = {"audit_steps": []}
    else:
        ir0_trace = json.loads((ir0_dir / "trace.json").read_text())

    if ktest_ok is None and not os.environ.get("LINUX_ABI_SKIP_KTEST"):
        ktest_ok = run_ktest_brk(ktest_name)

    res = compare_mount(
        linux_trace,
        ir0_trace,
        mount_path,
        mount_noent_path,
        rw_data_hex,
        enoent_errno,
        enodev_errno,
        None,
    )
    if ktest_ok is False:
        res.ok = False
        if f"ktest {ktest_name} FAILED" not in res.divergences:
            res.divergences.append(f"ktest {ktest_name} FAILED")
    elif ktest_ok is True and f"ktest {ktest_name} OK" not in res.notes:
        res.notes.append(f"ktest {ktest_name} OK")

    return res


def audit_execve(report_dir: Path, cfg: dict) -> CompareResult:
    linux_dir = report_dir / "linux" / "execve"
    ir0_dir = report_dir / "ir0" / "execve"
    enoent_errno = int(cfg.get("enoent_errno", 2))
    ktest_name = cfg.get("ktest")

    build_execve_probe(report_dir)

    sh_linux = ROOT / "scripts" / "linux_abi" / "run_linux_execve.sh"
    sh_ir0 = ROOT / "scripts" / "linux_abi" / "run_ir0_execve.sh"

    ktest_ok = None
    if ktest_name and not os.environ.get("LINUX_ABI_SKIP_KTEST"):
        ktest_log = Path("/tmp/ktest.log")
        if ktest_log.is_file() and f"[KTEST] {ktest_name} ... PASS" in ktest_log.read_text(
            errors="replace"
        ):
            ktest_ok = True
            print(f"  NOTE  reusing execve ktest evidence from {ktest_log}")

    if run_cmd(["bash", str(sh_linux), str(linux_dir)]) != 0:
        return CompareResult(
            contract="execve",
            ok=False,
            divergences=["Linux execve workload script failed"],
        )

    if run_cmd(["bash", str(sh_ir0), str(ir0_dir)]) != 0:
        return CompareResult(
            contract="execve",
            ok=False,
            divergences=["IR0 execve workload script failed"],
        )

    linux_trace = json.loads((linux_dir / "trace.json").read_text())
    ir0_trace = json.loads((ir0_dir / "trace.json").read_text())

    if ktest_ok is None and ktest_name and not os.environ.get("LINUX_ABI_SKIP_KTEST"):
        ktest_ok = run_ktest_brk(ktest_name)

    return compare_execve(linux_trace, ir0_trace, enoent_errno, ktest_ok)


def audit_openat(report_dir: Path, cfg: dict) -> CompareResult:
    linux_dir = report_dir / "linux" / "openat"
    ir0_dir = report_dir / "ir0" / "openat"
    enoent_errno = int(cfg.get("enoent_errno", 2))
    ebadf_errno = int(cfg.get("ebadf_errno", 9))
    ktest_name = cfg.get("ktest", "syscall_open_close")
    ktest_log = Path("/tmp/ktest.log")

    build_openat_probe(report_dir)

    sh_linux = ROOT / "scripts" / "linux_abi" / "run_linux_openat.sh"
    sh_ir0 = ROOT / "scripts" / "linux_abi" / "run_ir0_openat.sh"

    ktest_ok = None
    if not os.environ.get("LINUX_ABI_SKIP_KTEST"):
        if ktest_log.is_file() and f"[KTEST] {ktest_name} ... PASS" in ktest_log.read_text(
            errors="replace"
        ):
            ktest_ok = True
            print(f"  NOTE  reusing openat ktest evidence from {ktest_log}")

    if run_cmd(["bash", str(sh_linux), str(linux_dir)]) != 0:
        return CompareResult(
            contract="openat",
            ok=False,
            divergences=["Linux openat workload script failed"],
        )

    if run_cmd(["bash", str(sh_ir0), str(ir0_dir)]) != 0:
        return CompareResult(
            contract="openat",
            ok=False,
            divergences=["IR0 openat workload script failed"],
        )

    linux_trace = json.loads((linux_dir / "trace.json").read_text())
    ir0_trace = json.loads((ir0_dir / "trace.json").read_text())

    if ktest_ok is None and not os.environ.get("LINUX_ABI_SKIP_KTEST"):
        ktest_ok = run_ktest_brk(ktest_name)

    return compare_openat(
        linux_trace, ir0_trace, enoent_errno, ebadf_errno, ktest_ok
    )


def audit_stat(report_dir: Path, cfg: dict) -> CompareResult:
    linux_dir = report_dir / "linux" / "stat"
    ir0_dir = report_dir / "ir0" / "stat"
    enoent_errno = int(cfg.get("enoent_errno", 2))
    ebadf_errno = int(cfg.get("ebadf_errno", 9))

    build_stat_probe(report_dir)

    sh_linux = ROOT / "scripts" / "linux_abi" / "run_linux_stat.sh"
    sh_ir0 = ROOT / "scripts" / "linux_abi" / "run_ir0_stat.sh"

    host_ok = run_host_stat_test()

    if run_cmd(["bash", str(sh_linux), str(linux_dir)]) != 0:
        return CompareResult(
            contract="stat",
            ok=False,
            divergences=["Linux stat workload script failed"],
        )

    if run_cmd(["bash", str(sh_ir0), str(ir0_dir)]) != 0:
        return CompareResult(
            contract="stat",
            ok=False,
            divergences=["IR0 stat workload script failed"],
        )

    linux_trace = json.loads((linux_dir / "trace.json").read_text())
    ir0_trace = json.loads((ir0_dir / "trace.json").read_text())

    return compare_stat(linux_trace, ir0_trace, enoent_errno, ebadf_errno, host_ok)


def audit_vfs_write(report_dir: Path, cfg: dict) -> CompareResult:
    linux_dir = report_dir / "linux" / "vfs_write"
    ir0_dir = report_dir / "ir0" / "vfs_write"

    build_vfs_write_probe(report_dir)

    sh_linux = ROOT / "scripts" / "linux_abi" / "run_linux_vfs_write.sh"
    sh_ir0 = ROOT / "scripts" / "linux_abi" / "run_ir0_vfs_write.sh"

    if run_cmd(["bash", str(sh_linux), str(linux_dir)]) != 0:
        return CompareResult(
            contract="vfs_write",
            ok=False,
            divergences=["Linux vfs_write workload script failed"],
            notes=["bundle_status: BLOCKED"],
        )

    if run_cmd(["bash", str(sh_ir0), str(ir0_dir)]) != 0:
        return CompareResult(
            contract="vfs_write",
            ok=False,
            divergences=["IR0 vfs_write workload script failed"],
            notes=["bundle_status: BLOCKED"],
        )

    linux_trace = json.loads((linux_dir / "trace.json").read_text())
    ir0_trace = json.loads((ir0_dir / "trace.json").read_text())

    return compare_vfs_write(linux_trace, ir0_trace, cfg)


def audit_vfs_write_fat(report_dir: Path, cfg: dict) -> CompareResult:
    """Same vfs_write compare, IR0 workload on FAT16 (hdb) instead of MINIX."""
    linux_dir = report_dir / "linux" / "vfs_write"
    ir0_dir = report_dir / "ir0" / "vfs_write_fat"

    build_vfs_write_probe(report_dir)
    build_static_probe(
        report_dir / "vfs_write_fat_probe",
        ROOT / "setup" / "pid1" / "init_vfs_write_fat.c",
    )

    sh_linux = ROOT / "scripts" / "linux_abi" / "run_linux_vfs_write.sh"
    sh_ir0 = ROOT / "scripts" / "linux_abi" / "run_ir0_vfs_write_fat.sh"

    if not (linux_dir / "trace.json").exists():
        if run_cmd(["bash", str(sh_linux), str(linux_dir)]) != 0:
            return CompareResult(
                contract="vfs_write_fat",
                ok=False,
                divergences=["Linux vfs_write workload script failed"],
                notes=["bundle_status: BLOCKED"],
            )

    if run_cmd(["bash", str(sh_ir0), str(ir0_dir)]) != 0:
        return CompareResult(
            contract="vfs_write_fat",
            ok=False,
            divergences=["IR0 vfs_write FAT workload script failed"],
            notes=["bundle_status: BLOCKED"],
        )

    linux_trace = json.loads((linux_dir / "trace.json").read_text())
    ir0_trace = json.loads((ir0_dir / "trace.json").read_text())
    result = compare_vfs_write(linux_trace, ir0_trace, cfg)
    result.contract = "vfs_write_fat"
    return result


def build_process_lifecycle_probe(report_dir: Path) -> tuple[Path, Path]:
    helper = build_static_probe(
        report_dir / "exec_helper",
        ROOT / "scripts" / "linux_abi" / "workloads" / "exec_helper.c",
    )
    probe_out = report_dir / "process_lifecycle_probe"
    probe_src = ROOT / "scripts" / "linux_abi" / "workloads" / "process_lifecycle_probe.c"
    musl_cc = "musl-gcc"
    try:
        subprocess.run(["musl-gcc", "--version"], capture_output=True, check=True)
    except (FileNotFoundError, subprocess.CalledProcessError):
        musl_cc = "gcc"
    helper_path = str(helper.resolve())
    rc = run_cmd(
        [
            musl_cc,
            "-static",
            "-Os",
            f'-DEXEC_HELPER_PATH="{helper_path}"',
            "-o",
            str(probe_out),
            str(probe_src),
        ]
    )
    if rc != 0 or not probe_out.is_file():
        raise RuntimeError(f"failed to build {probe_out.name} with {musl_cc}")
    return probe_out, helper


def audit_process_lifecycle(report_dir: Path, cfg: dict) -> CompareResult:
    linux_dir = report_dir / "linux" / "process_lifecycle"
    ir0_dir = report_dir / "ir0" / "process_lifecycle"

    build_process_lifecycle_probe(report_dir)

    sh_linux = ROOT / "scripts" / "linux_abi" / "run_linux_process_lifecycle.sh"
    sh_ir0 = ROOT / "scripts" / "linux_abi" / "run_ir0_process_lifecycle.sh"

    ktest_ok: bool | None = None
    ktest_log = Path(os.environ.get("KTEST_LOG", "/tmp/ktest.log"))
    ktest_name = cfg.get("ktest", "wait4_status")
    if ktest_log.is_file() and not os.environ.get("LINUX_ABI_SKIP_KTEST"):
        ktest_ok = ktest_log.read_text(errors="replace").find(f"[PASS] {ktest_name}") >= 0
        if ktest_ok:
            print(f"  NOTE  reusing process_lifecycle ktest evidence from {ktest_log}")

    if run_cmd(["bash", str(sh_linux), str(linux_dir)]) != 0:
        return CompareResult(
            contract="process_lifecycle",
            ok=False,
            divergences=["Linux process_lifecycle workload script failed"],
        )

    if run_cmd(["bash", str(sh_ir0), str(ir0_dir)]) != 0:
        return CompareResult(
            contract="process_lifecycle",
            ok=False,
            divergences=["IR0 process_lifecycle workload script failed"],
        )

    linux_trace = json.loads((linux_dir / "trace.json").read_text())
    ir0_trace = json.loads((ir0_dir / "trace.json").read_text())

    res = compare_process_lifecycle(linux_trace, ir0_trace, cfg)
    if ktest_ok is False:
        res.ok = False
        res.divergences.append(f"ktest {ktest_name} FAILED")
    elif ktest_ok is True:
        res.notes.append(f"ktest {ktest_name} OK")
    return res


    return res


def audit_wait4_wnohang(report_dir: Path, cfg: dict) -> CompareResult:
    linux_dir = report_dir / "linux" / "wait4_wnohang"
    ir0_dir = report_dir / "ir0" / "wait4_wnohang"
    child_exit = int(cfg.get("child_exit_status", 5))
    echild_errno = int(cfg.get("echild_errno", 10))
    ktest_name = cfg.get("ktest", "wait4_status")
    ktest_log = Path(os.environ.get("KTEST_LOG", "/tmp/ktest.log"))

    build_wait4_wnohang_probe(report_dir)

    sh_linux = ROOT / "scripts" / "linux_abi" / "run_linux_wait4_wnohang.sh"
    sh_ir0 = ROOT / "scripts" / "linux_abi" / "run_ir0_wait4_wnohang.sh"

    if os.environ.get("LINUX_ABI_SKIP_KTEST"):
        ktest_ok = None
    elif ktest_log.is_file() and f"[KTEST] {ktest_name} ... PASS" in ktest_log.read_text(
        errors="replace"
    ):
        ktest_ok = True
        print(f"  NOTE  reusing wait4_wnohang ktest evidence from {ktest_log}")
    else:
        ktest_ok = None

    if run_cmd(["bash", str(sh_linux), str(linux_dir)]) != 0:
        return CompareResult(
            contract="wait4_wnohang",
            ok=False,
            divergences=["Linux wait4_wnohang workload script failed"],
        )

    if run_cmd(["bash", str(sh_ir0), str(ir0_dir)]) != 0:
        ir0_trace_path = ir0_dir / "trace.json"
        if ir0_trace_path.is_file():
            ir0_trace = json.loads(ir0_trace_path.read_text())
            ops = {s.get("op") for s in ir0_trace.get("audit_steps") or []}
            if "wait4_block_reap" not in ops:
                return CompareResult(
                    contract="wait4_wnohang",
                    ok=False,
                    divergences=["IR0 wait4_wnohang workload script failed"],
                )
        else:
            return CompareResult(
                contract="wait4_wnohang",
                ok=False,
                divergences=["IR0 wait4_wnohang workload script failed"],
            )

    linux_trace = json.loads((linux_dir / "trace.json").read_text())
    if not (ir0_dir / "trace.json").is_file():
        ir0_trace = {"audit_steps": []}
    else:
        ir0_trace = json.loads((ir0_dir / "trace.json").read_text())

    if ktest_ok is None:
        ktest_ok = run_ktest_brk(ktest_name)
    return compare_wait4_wnohang(
        linux_trace, ir0_trace, child_exit, echild_errno, ktest_ok
    )


def audit_kill_sigterm(report_dir: Path, cfg: dict) -> CompareResult:
    linux_dir = report_dir / "linux" / "kill_sigterm"
    ir0_dir = report_dir / "ir0" / "kill_sigterm"
    ktest_name = cfg.get("ktest", "kill_sigterm_wait_status")
    ktest_log = Path("/tmp/ktest.log")

    build_kill_sigterm_probe(report_dir)

    sh_linux = ROOT / "scripts" / "linux_abi" / "run_linux_kill_sigterm.sh"
    sh_ir0 = ROOT / "scripts" / "linux_abi" / "run_ir0_kill_sigterm.sh"

    if os.environ.get("LINUX_ABI_SKIP_KTEST"):
        ktest_ok = None
    elif ktest_log.is_file() and f"[KTEST] {ktest_name} ... PASS" in ktest_log.read_text(
        errors="replace"
    ):
        ktest_ok = True
        print(f"  NOTE  reusing kill_sigterm ktest evidence from {ktest_log}")
    else:
        ktest_ok = None

    if run_cmd(["bash", str(sh_linux), str(linux_dir)]) != 0:
        return CompareResult(
            contract="kill_sigterm",
            ok=False,
            divergences=["Linux kill_sigterm workload script failed"],
        )

    if run_cmd(["bash", str(sh_ir0), str(ir0_dir)]) != 0:
        ir0_trace_path = ir0_dir / "trace.json"
        if ir0_trace_path.is_file():
            ir0_trace = json.loads(ir0_trace_path.read_text())
            steps = ir0_trace.get("audit_steps") or []
            if not any(s.get("op") == "kill_sigterm" for s in steps):
                return CompareResult(
                    contract="kill_sigterm",
                    ok=False,
                    divergences=["IR0 kill_sigterm workload script failed"],
                )
        else:
            return CompareResult(
                contract="kill_sigterm",
                ok=False,
                divergences=["IR0 kill_sigterm workload script failed"],
            )

    linux_trace = json.loads((linux_dir / "trace.json").read_text())
    if not (ir0_dir / "trace.json").is_file():
        ir0_trace = {"audit_steps": []}
    else:
        ir0_trace = json.loads((ir0_dir / "trace.json").read_text())

    if ktest_ok is None:
        ktest_ok = run_ktest_brk(ktest_name)
    res = compare_kill_sigterm(linux_trace, ir0_trace, ktest_ok)
    return res


def audit_pipe(report_dir: Path, cfg: dict) -> CompareResult:
    linux_dir = report_dir / "linux" / "pipe"
    ir0_dir = report_dir / "ir0" / "pipe"
    pipe_read_len = int(cfg.get("pipe_read_len", 6))
    pipe_data_hex = str(cfg.get("pipe_data_hex", "68656c6c6f0a"))
    ebadf_errno = int(cfg.get("ebadf_errno", 9))
    ktest_name = cfg.get("ktest", "syscall_pipe")
    ktest_log = Path("/tmp/ktest.log")

    build_read_probe(report_dir)

    sh_linux = ROOT / "scripts" / "linux_abi" / "run_linux_read.sh"
    sh_ir0 = ROOT / "scripts" / "linux_abi" / "run_ir0_read.sh"

    ktest_ok = None
    if not os.environ.get("LINUX_ABI_SKIP_KTEST"):
        if ktest_log.is_file() and f"[KTEST] {ktest_name} ... PASS" in ktest_log.read_text(
            errors="replace"
        ):
            ktest_ok = True

    if run_cmd(["bash", str(sh_linux), str(linux_dir)]) != 0:
        return CompareResult(
            contract="pipe",
            ok=False,
            divergences=["Linux pipe workload script failed"],
        )
    if run_cmd(["bash", str(sh_ir0), str(ir0_dir)]) != 0:
        return CompareResult(
            contract="pipe",
            ok=False,
            divergences=["IR0 pipe workload script failed"],
        )

    linux_trace = json.loads((linux_dir / "trace.json").read_text())
    ir0_trace = json.loads((ir0_dir / "trace.json").read_text())

    if ktest_ok is None and not os.environ.get("LINUX_ABI_SKIP_KTEST"):
        ktest_ok = run_ktest_brk(ktest_name)

    return compare_pipe(
        linux_trace,
        ir0_trace,
        pipe_read_len,
        pipe_data_hex,
        ebadf_errno,
        ktest_ok,
    )


def audit_poll(report_dir: Path, cfg: dict) -> CompareResult:
    linux_dir = report_dir / "linux" / "poll"
    ir0_dir = report_dir / "ir0" / "poll"
    ktest_name = cfg.get("ktest", "poll_resume_invariant")
    ktest_log = Path("/tmp/ktest.log")

    build_poll_probe(report_dir)

    ktest_ok = None
    if not os.environ.get("LINUX_ABI_SKIP_KTEST"):
        if ktest_log.is_file() and f"[KTEST] {ktest_name} ... PASS" in ktest_log.read_text(
            errors="replace"
        ):
            ktest_ok = True

    if not _run_simple_workloads(
        "poll", "poll_probe", "POLLOK", "poll,pipe,write,close", linux_dir, ir0_dir
    ):
        return CompareResult(
            contract="poll",
            ok=False,
            divergences=["poll workload script failed"],
        )

    linux_trace = json.loads((linux_dir / "trace.json").read_text())
    ir0_trace = json.loads((ir0_dir / "trace.json").read_text())

    if ktest_ok is None and not os.environ.get("LINUX_ABI_SKIP_KTEST"):
        ktest_ok = run_ktest_brk(ktest_name)

    return compare_poll(linux_trace, ir0_trace, ktest_ok)


def audit_nanosleep(report_dir: Path, cfg: dict) -> CompareResult:
    linux_dir = report_dir / "linux" / "nanosleep"
    ir0_dir = report_dir / "ir0" / "nanosleep"

    build_nanosleep_probe(report_dir)

    if not _run_simple_workloads(
        "nanosleep",
        "nanosleep_probe",
        "NANOSLEEPOK",
        "nanosleep",
        linux_dir,
        ir0_dir,
    ):
        return CompareResult(
            contract="nanosleep",
            ok=False,
            divergences=["nanosleep workload script failed"],
        )

    linux_trace = json.loads((linux_dir / "trace.json").read_text())
    ir0_trace = json.loads((ir0_dir / "trace.json").read_text())
    return compare_nanosleep(linux_trace, ir0_trace)


def audit_sigreturn_blocked_syscall(report_dir: Path, cfg: dict) -> CompareResult:
    linux_dir = report_dir / "linux" / "sigreturn_blocked_syscall"
    ir0_dir = report_dir / "ir0" / "sigreturn_blocked_syscall"

    build_sigreturn_blocked_syscall_probe(report_dir)

    if not _run_simple_workloads(
        "sigreturn_blocked_syscall",
        "sigreturn_blocked_syscall_probe",
        "SIGRETURNEINTROK",
        "read,pipe,write,close,kill,fork,wait4,sigaction,rt_sigreturn",
        linux_dir,
        ir0_dir,
    ):
        return CompareResult(
            contract="sigreturn_blocked_syscall",
            ok=False,
            divergences=["sigreturn_blocked_syscall workload script failed"],
        )

    linux_trace = json.loads((linux_dir / "trace.json").read_text())
    ir0_trace = json.loads((ir0_dir / "trace.json").read_text())
    return compare_sigreturn_blocked_syscall(linux_trace, ir0_trace)


def audit_getcwd(report_dir: Path, cfg: dict) -> CompareResult:
    linux_dir = report_dir / "linux" / "getcwd"
    ir0_dir = report_dir / "ir0" / "getcwd"
    expected_path = str(cfg.get("expected_path", "/"))

    build_getcwd_probe(report_dir)

    if not _run_simple_workloads(
        "getcwd",
        "getcwd_probe",
        "GETCWDOK",
        "getcwd",
        linux_dir,
        ir0_dir,
    ):
        return CompareResult(
            contract="getcwd",
            ok=False,
            divergences=["getcwd workload script failed"],
        )

    linux_trace = json.loads((linux_dir / "trace.json").read_text())
    ir0_trace = json.loads((ir0_dir / "trace.json").read_text())
    return compare_getcwd(linux_trace, ir0_trace, expected_path)


def audit_chdir(report_dir: Path, cfg: dict) -> CompareResult:
    linux_dir = report_dir / "linux" / "chdir"
    ir0_dir = report_dir / "ir0" / "chdir"
    target_path = str(cfg.get("target_path", "/"))

    build_chdir_probe(report_dir, target_path)

    if not _run_simple_workloads(
        "chdir",
        "chdir_probe",
        "CHDIROK",
        "chdir,getcwd",
        linux_dir,
        ir0_dir,
    ):
        return CompareResult(
            contract="chdir",
            ok=False,
            divergences=["chdir workload script failed"],
        )

    linux_trace = json.loads((linux_dir / "trace.json").read_text())
    ir0_trace = json.loads((ir0_dir / "trace.json").read_text())
    return compare_chdir(linux_trace, ir0_trace, target_path)


def audit_dup(report_dir: Path, cfg: dict) -> CompareResult:
    linux_dir = report_dir / "linux" / "dup"
    ir0_dir = report_dir / "ir0" / "dup"
    existing_path = str(cfg.get("existing_path", "/proc/uptime"))
    ebadf_errno = int(cfg.get("ebadf_errno", 9))

    build_dup_probe(report_dir, existing_path)

    if not _run_simple_workloads(
        "dup",
        "dup_probe",
        "DUPOK",
        "open,dup,dup2,fcntl,close",
        linux_dir,
        ir0_dir,
    ):
        return CompareResult(
            contract="dup",
            ok=False,
            divergences=["dup workload script failed"],
        )

    linux_trace = json.loads((linux_dir / "trace.json").read_text())
    ir0_trace = json.loads((ir0_dir / "trace.json").read_text())
    return compare_dup(linux_trace, ir0_trace, ebadf_errno)


def audit_ioctl(report_dir: Path, cfg: dict) -> CompareResult:
    linux_dir = report_dir / "linux" / "ioctl"
    ir0_dir = report_dir / "ir0" / "ioctl"

    build_ioctl_tty_probe(report_dir)

    if not _run_simple_workloads(
        "ioctl",
        "ioctl_tty_probe",
        "IOCTLTTYOK",
        "ioctl,write",
        linux_dir,
        ir0_dir,
    ):
        return CompareResult(
            contract="ioctl",
            ok=False,
            divergences=["ioctl workload script failed"],
        )

    linux_trace = json.loads((linux_dir / "trace.json").read_text())
    ir0_trace = json.loads((ir0_dir / "trace.json").read_text())
    return compare_ioctl(linux_trace, ir0_trace)


def audit_fcntl(report_dir: Path, cfg: dict) -> CompareResult:
    linux_dir = report_dir / "linux" / "fcntl"
    ir0_dir = report_dir / "ir0" / "fcntl"

    build_fcntl_probe(report_dir)

    if not _run_simple_workloads(
        "fcntl",
        "fcntl_probe",
        "FCNTLOK",
        "open,fcntl,close,write",
        linux_dir,
        ir0_dir,
    ):
        return CompareResult(
            contract="fcntl",
            ok=False,
            divergences=["fcntl workload script failed"],
        )

    linux_trace = json.loads((linux_dir / "trace.json").read_text())
    ir0_trace = json.loads((ir0_dir / "trace.json").read_text())
    return compare_fcntl(linux_trace, ir0_trace)


AUDITORS = {
    "brk": audit_brk,
    "wait4": audit_wait4,
    "wait4_wnohang": audit_wait4_wnohang,
    "kill_sigterm": audit_kill_sigterm,
    "read": audit_read,
    "pipe": audit_pipe,
    "poll": audit_poll,
    "nanosleep": audit_nanosleep,
    "sigreturn_blocked_syscall": audit_sigreturn_blocked_syscall,
    "getcwd": audit_getcwd,
    "chdir": audit_chdir,
    "dup": audit_dup,
    "ioctl": audit_ioctl,
    "fcntl": audit_fcntl,
    "mmap": audit_mmap,
    "mount": audit_mount,
    "execve": audit_execve,
    "openat": audit_openat,
    "stat": audit_stat,
    "vfs_write": audit_vfs_write,
    "vfs_write_fat": audit_vfs_write_fat,
    "process_lifecycle": audit_process_lifecycle,
}


def main() -> int:
    ap = argparse.ArgumentParser(description="Linux↔IR0 ABI ground-truth audit")
    ap.add_argument("--contract", action="append", help="Audit one contract (repeatable)")
    ap.add_argument("--all", action="store_true", help="Audit all enabled contracts")
    ap.add_argument(
        "--report-dir",
        type=Path,
        default=ROOT / "build" / "linux_abi_audit",
        help="Output directory",
    )
    args = ap.parse_args()

    cfg = load_contracts()
    report_dir: Path = args.report_dir
    report_dir.mkdir(parents=True, exist_ok=True)

    if args.all:
        ids = [c["id"] for c in cfg["contracts"] if c.get("enabled")]
    elif args.contract:
        ids = args.contract
    else:
        ids = [c["id"] for c in cfg["contracts"] if c.get("enabled")]

    results: list[CompareResult] = []
    for cid in ids:
        contract_cfg = next((c for c in cfg["contracts"] if c["id"] == cid), {})
        if cid not in AUDITORS:
            results.append(
                CompareResult(
                    contract=cid,
                    ok=False,
                    divergences=[f"no automated auditor for '{cid}' yet"],
                )
            )
            continue
        print(f"\n=== ABI audit: {cid} ===")
        results.append(AUDITORS[cid](report_dir, contract_cfg))

    meta = {
        "generated": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "contracts": ids,
    }
    json_path = report_dir / "report.json"
    md_path = report_dir / "report.md"
    json_path.write_text(
        json.dumps(
            {"meta": meta, "results": [r.to_dict() for r in results]},
            indent=2,
        )
        + "\n"
    )
    md_path.write_text(render_markdown(results, meta))

    print(f"\nReport: {md_path}")
    print(f"JSON:   {json_path}")

    return 0 if all(r.ok for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
