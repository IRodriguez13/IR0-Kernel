#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare normalized Linux and IR0 ABI audit traces."""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


@dataclass
class CompareResult:
    contract: str
    ok: bool
    divergences: list[str] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)

    def to_dict(self) -> dict[str, Any]:
        return {
            "contract": self.contract,
            "ok": self.ok,
            "divergences": self.divergences,
            "notes": self.notes,
        }


def _load(path: Path) -> dict:
    return json.loads(path.read_text())


def compare_brk(
    linux: dict,
    ir0: dict,
    grow_bytes: int,
    host_test_ok: bool | None,
    ktest_ok: bool | None,
) -> CompareResult:
    res = CompareResult(contract="brk", ok=True)

    l_steps = linux.get("audit_steps") or linux.get("strace_steps") or []
    i_steps = ir0.get("audit_steps") or []

    if len(l_steps) < 2:
        res.ok = False
        res.divergences.append("linux trace has fewer than 2 brk steps")
    if len(i_steps) < 2:
        res.ok = False
        res.divergences.append("ir0 trace has fewer than 2 brk steps")

    if len(l_steps) >= 2 and len(i_steps) >= 2:
        l0, l1 = l_steps[0], l_steps[1]
        i0, i1 = i_steps[0], i_steps[1]

        if l0.get("ret", -1) <= 0:
            res.ok = False
            res.divergences.append(f"linux brk(0) invalid ret={l0.get('ret')}")
        if i0.get("ret", -1) <= 0:
            res.ok = False
            res.divergences.append(f"ir0 brk(0) invalid ret={i0.get('ret')}")

        l_expected = l0.get("ret", 0) + grow_bytes
        i_expected = i0.get("ret", 0) + grow_bytes

        if l1.get("ret") != l_expected:
            res.ok = False
            res.divergences.append(
                f"linux brk grow: expected ret={l_expected} got={l1.get('ret')}"
            )
        if i1.get("ret") != i_expected:
            res.ok = False
            res.divergences.append(
                f"ir0 brk grow: expected ret={i_expected} got={i1.get('ret')}"
            )

        l_delta = l1.get("ret", 0) - l0.get("ret", 0)
        i_delta = i1.get("ret", 0) - i0.get("ret", 0)
        if l_delta != grow_bytes:
            res.ok = False
            res.divergences.append(f"linux brk delta={l_delta} expected={grow_bytes}")
        if i_delta != grow_bytes:
            res.ok = False
            res.divergences.append(f"ir0 brk delta={i_delta} expected={grow_bytes}")
        if l_delta != i_delta:
            res.ok = False
            res.divergences.append(
                f"brk delta mismatch linux={l_delta} ir0={i_delta} (absolute brk may differ by design)"
            )

        res.notes.append(
            f"linux brk0=0x{l0.get('ret', 0):x} -> 0x{l1.get('ret', 0):x} (delta=0x{l_delta:x})"
        )
        res.notes.append(
            f"ir0 brk0=0x{i0.get('ret', 0):x} -> 0x{i1.get('ret', 0):x} (delta=0x{i_delta:x})"
        )

    if host_test_ok is False:
        res.ok = False
        res.divergences.append("host test elf_initial_brk_abi FAILED")
    elif host_test_ok is True:
        res.notes.append("host test elf_initial_brk_abi OK")

    if ktest_ok is False:
        res.ok = False
        res.divergences.append("ktest brk_post_exec FAILED")
    elif ktest_ok is True:
        res.notes.append("ktest brk_post_exec OK")

    return res


def _find_step(steps: list[dict], op: str) -> dict | None:
    for s in steps:
        if s.get("op") == op:
            return s
    return None


def compare_wait4(
    linux: dict,
    ir0: dict,
    child_exit_status: int,
    ktest_ok: bool | None,
) -> CompareResult:
    res = CompareResult(contract="wait4", ok=True)
    expected_status = child_exit_status << 8

    l_steps = linux.get("audit_steps") or linux.get("strace_steps") or []
    i_steps = ir0.get("audit_steps") or []

    if len(l_steps) < 2:
        res.ok = False
        res.divergences.append("linux trace has fewer than 2 wait4 steps")
    if len(i_steps) < 2:
        res.ok = False
        res.divergences.append("ir0 trace has fewer than 2 wait4 steps")

    if len(l_steps) >= 2 and len(i_steps) >= 2:
        l_fork = _find_step(l_steps, "fork")
        l_wait = _find_step(l_steps, "wait4")
        i_fork = _find_step(i_steps, "fork")
        i_wait = _find_step(i_steps, "wait4")

        for label, fork_s, wait_s in (
            ("linux", l_fork, l_wait),
            ("ir0", i_fork, i_wait),
        ):
            if not fork_s or not wait_s:
                res.ok = False
                res.divergences.append(f"{label} trace missing fork or wait4 step")
                continue
            if fork_s.get("ret", -1) <= 0:
                res.ok = False
                res.divergences.append(f"{label} fork invalid ret={fork_s.get('ret')}")
            if wait_s.get("ret", -1) <= 0:
                res.ok = False
                res.divergences.append(f"{label} wait4 invalid ret={wait_s.get('ret')}")
            if wait_s.get("ret") != fork_s.get("ret"):
                res.ok = False
                res.divergences.append(
                    f"{label} wait4 pid mismatch: fork={fork_s.get('ret')} wait={wait_s.get('ret')}"
                )
            got_status = wait_s.get("status")
            if got_status is None:
                res.ok = False
                res.divergences.append(f"{label} wait4 missing status word")
            elif got_status != expected_status:
                res.ok = False
                res.divergences.append(
                    f"{label} wait4 status=0x{got_status:x} expected=0x{expected_status:x} (exit={child_exit_status})"
                )

        if l_fork and l_wait and i_fork and i_wait:
            res.notes.append(
                f"linux fork={l_fork.get('ret')} wait4 status=0x{(l_wait.get('status') or 0):x}"
            )
            res.notes.append(
                f"ir0 fork={i_fork.get('ret')} wait4 status=0x{(i_wait.get('status') or 0):x}"
            )

    if ktest_ok is False:
        res.ok = False
        res.divergences.append("ktest wait4_status FAILED")
    elif ktest_ok is True:
        res.notes.append("ktest wait4_status OK")

    return res


def compare_wait4_wnohang(
    linux: dict,
    ir0: dict,
    child_exit_status: int,
    echild_errno: int,
    ktest_ok: bool | None,
) -> CompareResult:
    res = CompareResult(contract="wait4_wnohang", ok=True)
    expected_status = child_exit_status << 8

    l_steps = linux.get("audit_steps") or linux.get("strace_steps") or []
    i_steps = ir0.get("audit_steps") or []

    required_ops = (
        "fork",
        "wait4_wnohang_alive",
        "wait4_block_reap",
        "wait4_wnohang_echild",
    )

    for op in required_ops:
        l_s = _find_step(l_steps, op)
        i_s = _find_step(i_steps, op)
        if not l_s or not i_s:
            res.ok = False
            res.divergences.append(f"missing {op} (linux={bool(l_s)} ir0={bool(i_s)})")

    l_fork = _find_step(l_steps, "fork")
    i_fork = _find_step(i_steps, "fork")
    if l_fork and i_fork and l_fork.get("ret") != i_fork.get("ret"):
        res.notes.append(
            f"fork pid differs linux={l_fork.get('ret')} ir0={i_fork.get('ret')} (allowed if both >0)"
        )

    l_wh = _find_step(l_steps, "wait4_wnohang_alive")
    i_wh = _find_step(i_steps, "wait4_wnohang_alive")
    if l_wh and i_wh:
        for label, step in (("linux", l_wh), ("ir0", i_wh)):
            if step.get("ret") != 0:
                res.ok = False
                res.divergences.append(
                    f"{label} wait4_wnohang_alive: ret={step.get('ret')} expected 0"
                )

    l_reap = _find_step(l_steps, "wait4_block_reap")
    i_reap = _find_step(i_steps, "wait4_block_reap")
    if l_reap and i_reap and l_fork and i_fork:
        for label, step, fork_s in (
            ("linux", l_reap, l_fork),
            ("ir0", i_reap, i_fork),
        ):
            if step.get("ret", -1) <= 0:
                res.ok = False
                res.divergences.append(
                    f"{label} wait4_block_reap: ret={step.get('ret')} expected pid>0"
                )
            elif step.get("ret") != fork_s.get("ret"):
                res.ok = False
                res.divergences.append(
                    f"{label} wait4_block_reap: ret={step.get('ret')} expected fork pid={fork_s.get('ret')}"
                )
            st = step.get("status")
            if st != expected_status:
                res.ok = False
                res.divergences.append(
                    f"{label} wait4_block_reap: status=0x{(st or 0):x} expected 0x{expected_status:x}"
                )

    l_ec = _find_step(l_steps, "wait4_wnohang_echild")
    i_ec = _find_step(i_steps, "wait4_wnohang_echild")
    if l_ec and i_ec:
        for label, step in (("linux", l_ec), ("ir0", i_ec)):
            if step.get("ret") != -1:
                res.ok = False
                res.divergences.append(
                    f"{label} wait4_wnohang_echild: ret={step.get('ret')} expected -1"
                )
            if step.get("errno") != echild_errno:
                res.ok = False
                res.divergences.append(
                    f"{label} wait4_wnohang_echild: errno={step.get('errno')} expected={echild_errno}"
                )

    if ktest_ok is False:
        res.ok = False
        res.divergences.append("ktest wait4_status FAILED")
    elif ktest_ok is True:
        res.notes.append("ktest wait4_status OK")

    return res


def compare_kill_sigterm(
    linux: dict,
    ir0: dict,
    ktest_ok: bool | None,
) -> CompareResult:
    res = CompareResult(contract="kill_sigterm", ok=True)
    expected_status = 0xF

    l_steps = linux.get("audit_steps") or []
    i_steps = ir0.get("audit_steps") or []

    l_fork = _find_step(l_steps, "fork")
    l_kill = _find_step(l_steps, "kill")
    l_wait = _find_step(l_steps, "kill_sigterm")
    i_fork = _find_step(i_steps, "fork")
    i_kill = _find_step(i_steps, "kill")
    i_wait = _find_step(i_steps, "kill_sigterm")

    for label, fork_s, kill_s, wait_s in (
        ("linux", l_fork, l_kill, l_wait),
        ("ir0", i_fork, i_kill, i_wait),
    ):
        if not fork_s or not kill_s or not wait_s:
            res.ok = False
            res.divergences.append(
                f"{label} trace missing fork/kill/kill_sigterm "
                f"(fork={bool(fork_s)} kill={bool(kill_s)} wait={bool(wait_s)})"
            )
            continue
        if fork_s.get("ret", -1) <= 0:
            res.ok = False
            res.divergences.append(f"{label} fork invalid ret={fork_s.get('ret')}")
        if kill_s.get("ret", -1) != 0:
            res.ok = False
            res.divergences.append(
                f"{label} kill ret={kill_s.get('ret')} errno={kill_s.get('errno')} expected 0"
            )
        if wait_s.get("ret", -1) <= 0:
            res.ok = False
            res.divergences.append(f"{label} wait4 invalid ret={wait_s.get('ret')}")
        if wait_s.get("ret") != fork_s.get("ret"):
            res.ok = False
            res.divergences.append(
                f"{label} wait pid mismatch: fork={fork_s.get('ret')} wait={wait_s.get('ret')}"
            )
        got_status = wait_s.get("status")
        if got_status is None:
            res.ok = False
            res.divergences.append(f"{label} kill_sigterm missing status word")
        elif (got_status & 0x7F) != 15:
            res.ok = False
            res.divergences.append(
                f"{label} kill_sigterm status=0x{got_status:x} WTERMSIG={(got_status & 0x7F)} expected 15"
            )
        elif got_status != expected_status:
            res.notes.append(
                f"{label} kill_sigterm raw status=0x{got_status:x} (expected 0x{expected_status:x})"
            )

    if l_wait and i_wait and (l_wait.get("status") or 0) != (i_wait.get("status") or 0):
        res.ok = False
        res.divergences.append(
            f"kill_sigterm status mismatch linux=0x{(l_wait.get('status') or 0):x} "
            f"ir0=0x{(i_wait.get('status') or 0):x}"
        )

    if not linux.get("done_tag"):
        res.notes.append("linux missing [KILLSIGTERMOK] (audit steps used)")
    if not ir0.get("done_tag"):
        res.ok = False
        res.divergences.append("ir0 missing [KILLSIGTERMOK] smoke tag")

    if ktest_ok is False:
        res.ok = False
        res.divergences.append("ktest kill_sigterm_wait_status FAILED")
    elif ktest_ok is True:
        res.notes.append("ktest kill_sigterm_wait_status OK")

    return res


def compare_read(
    linux: dict,
    ir0: dict,
    pipe_read_len: int,
    pipe_data_hex: str,
    ebadf_errno: int,
    ktest_ok: bool | None,
) -> CompareResult:
    res = CompareResult(contract="read", ok=True)

    l_steps = linux.get("audit_steps") or linux.get("strace_steps") or []
    i_steps = ir0.get("audit_steps") or []

    if len(l_steps) < 3:
        res.ok = False
        res.divergences.append("linux trace has fewer than 3 read steps")
    if len(i_steps) < 3:
        res.ok = False
        res.divergences.append("ir0 trace has fewer than 3 read steps")

    checks = (
        ("read_pipe", pipe_read_len, pipe_data_hex, None),
        ("read_eof", 0, None, None),
        ("read_ebadf", -1, None, ebadf_errno),
    )

    for op, exp_ret, exp_hex, exp_errno in checks:
        l_s = _find_step(l_steps, op)
        i_s = _find_step(i_steps, op)
        if not l_s or not i_s:
            res.ok = False
            res.divergences.append(f"missing {op} step (linux={bool(l_s)} ir0={bool(i_s)})")
            continue

        for label, step in (("linux", l_s), ("ir0", i_s)):
            got_ret = step.get("ret")
            if got_ret != exp_ret:
                res.ok = False
                res.divergences.append(
                    f"{label} {op}: ret={got_ret} expected={exp_ret}"
                )
            if exp_hex is not None:
                got_hex = (step.get("data_hex") or "").lower()
                if got_hex != exp_hex.lower():
                    res.ok = False
                    res.divergences.append(
                        f"{label} {op}: data_hex={got_hex} expected={exp_hex}"
                    )
            if exp_errno is not None:
                if step.get("errno") != exp_errno:
                    res.ok = False
                    res.divergences.append(
                        f"{label} {op}: errno={step.get('errno')} expected={exp_errno}"
                    )

        if l_s.get("ret") != i_s.get("ret"):
            res.ok = False
            res.divergences.append(
                f"{op} ret mismatch linux={l_s.get('ret')} ir0={i_s.get('ret')}"
            )
        if exp_hex is not None:
            l_hex = (l_s.get("data_hex") or "").lower()
            i_hex = (i_s.get("data_hex") or "").lower()
            if l_hex != i_hex:
                res.ok = False
                res.divergences.append(
                    f"{op} data mismatch linux={l_hex} ir0={i_hex}"
                )

    l_pipe = _find_step(l_steps, "read_pipe")
    i_pipe = _find_step(i_steps, "read_pipe")
    if l_pipe and i_pipe:
        res.notes.append(
            f"linux read_pipe ret={l_pipe.get('ret')} data={l_pipe.get('data_hex')}"
        )
        res.notes.append(
            f"ir0 read_pipe ret={i_pipe.get('ret')} data={i_pipe.get('data_hex')}"
        )

    if ktest_ok is False:
        res.ok = False
        res.divergences.append("ktest syscall_pipe FAILED")
    elif ktest_ok is True:
        res.notes.append("ktest syscall_pipe OK")

    return res


MMAP_FAILED_RET = 0xFFFFFFFFFFFFFFFF


def _mmap_ok(ret: int | None) -> bool:
    return ret is not None and ret != MMAP_FAILED_RET


def compare_mmap(
    linux: dict,
    ir0: dict,
    page_size: int,
    none_size: int,
    verify_data_hex: str,
    ebadf_errno: int,
    ktest_ok: bool | None,
) -> CompareResult:
    res = CompareResult(contract="mmap", ok=True)

    l_steps = linux.get("audit_steps") or linux.get("strace_steps") or []
    i_steps = ir0.get("audit_steps") or []

    required_ops = (
        "mmap_anon_rw",
        "mmap_verify_rw",
        "mmap_anon_none",
        "mmap_fixed",
        "munmap_rw",
        "mmap_bad_nanon",
        "mmap_bad_fd",
    )

    if len(l_steps) < len(required_ops):
        res.ok = False
        res.divergences.append(
            f"linux trace has {len(l_steps)} mmap steps, need >={len(required_ops)}"
        )
    if len(i_steps) < len(required_ops):
        res.ok = False
        res.divergences.append(
            f"ir0 trace has {len(i_steps)} mmap steps, need >={len(required_ops)}"
        )

    for op in required_ops:
        l_s = _find_step(l_steps, op)
        i_s = _find_step(i_steps, op)
        if not l_s or not i_s:
            res.ok = False
            res.divergences.append(f"missing {op} step (linux={bool(l_s)} ir0={bool(i_s)})")
            continue

        for label, step in (("linux", l_s), ("ir0", i_s)):
            ret = step.get("ret")
            if op in ("mmap_bad_nanon", "mmap_bad_fd"):
                if _mmap_ok(ret):
                    res.ok = False
                    res.divergences.append(f"{label} {op}: expected MAP_FAILED got 0x{ret:x}")
                if step.get("errno") != ebadf_errno:
                    res.ok = False
                    res.divergences.append(
                        f"{label} {op}: errno={step.get('errno')} expected={ebadf_errno}"
                    )
                continue

            if op == "munmap_rw":
                if ret != 0:
                    res.ok = False
                    res.divergences.append(f"{label} {op}: ret=0x{ret:x} expected 0")
                continue

            if not _mmap_ok(ret):
                res.ok = False
                res.divergences.append(f"{label} {op}: MAP_FAILED errno={step.get('errno')}")
                continue

            exp_len = page_size if op != "mmap_anon_none" else none_size
            if step.get("len") != exp_len:
                res.ok = False
                res.divergences.append(
                    f"{label} {op}: len={step.get('len')} expected={exp_len}"
                )

            if op == "mmap_verify_rw":
                got_hex = (step.get("data_hex") or "").lower()
                if got_hex != verify_data_hex.lower():
                    res.ok = False
                    res.divergences.append(
                        f"{label} {op}: data_hex={got_hex} expected={verify_data_hex}"
                    )

            if op == "mmap_fixed":
                req = step.get("req", 0)
                if ret != req:
                    res.ok = False
                    res.divergences.append(
                        f"{label} {op}: ret=0x{ret:x} req=0x{req:x} (MAP_FIXED mismatch)"
                    )

        if op == "mmap_verify_rw":
            l_hex = (l_s.get("data_hex") or "").lower()
            i_hex = (i_s.get("data_hex") or "").lower()
            if l_hex != i_hex:
                res.ok = False
                res.divergences.append(f"{op} data mismatch linux={l_hex} ir0={i_hex}")

        if op in ("mmap_bad_nanon", "mmap_bad_fd"):
            if l_s.get("errno") != i_s.get("errno"):
                res.ok = False
                res.divergences.append(
                    f"{op} errno mismatch linux={l_s.get('errno')} ir0={i_s.get('errno')}"
                )

    l_rw = _find_step(l_steps, "mmap_anon_rw")
    i_rw = _find_step(i_steps, "mmap_anon_rw")
    if l_rw and i_rw and _mmap_ok(l_rw.get("ret")) and _mmap_ok(i_rw.get("ret")):
        res.notes.append(
            f"linux mmap_anon_rw ok len={l_rw.get('len')} (addr not compared)"
        )
        res.notes.append(
            f"ir0 mmap_anon_rw ok len={i_rw.get('len')} ret=0x{i_rw.get('ret'):x}"
        )

    if ktest_ok is False:
        res.ok = False
        res.divergences.append("ktest mmap_null_placement FAILED")
    elif ktest_ok is True:
        res.notes.append("ktest mmap_null_placement OK")

    return res


def compare_mount(
    linux: dict,
    ir0: dict,
    mount_path: str,
    mount_noent_path: str,
    rw_data_hex: str,
    enoent_errno: int,
    enodev_errno: int,
    ktest_ok: bool | None,
) -> CompareResult:
    res = CompareResult(contract="mount", ok=True)

    l_steps = linux.get("audit_steps") or linux.get("strace_steps") or []
    i_steps = ir0.get("audit_steps") or []

    required_ops = (
        "mount_tmpfs",
        "tmpfs_rw",
        "umount_tmpfs",
        "mount_noent",
        "mount_badfs",
    )

    if len(l_steps) < len(required_ops):
        res.ok = False
        res.divergences.append(
            f"linux trace has {len(l_steps)} mount steps, need >={len(required_ops)}"
        )
    if len(i_steps) < len(required_ops):
        res.ok = False
        res.divergences.append(
            f"ir0 trace has {len(i_steps)} mount steps, need >={len(required_ops)}"
        )

    checks = (
        ("mount_tmpfs", 0, mount_path, None, None),
        ("tmpfs_rw", 0, None, rw_data_hex, None),
        ("umount_tmpfs", 0, mount_path, None, None),
        ("mount_noent", -1, mount_noent_path, None, enoent_errno),
        ("mount_badfs", -1, mount_path, None, enodev_errno),
    )

    for op, exp_ret, exp_path, exp_hex, exp_errno in checks:
        l_s = _find_step(l_steps, op)
        i_s = _find_step(i_steps, op)
        if not l_s or not i_s:
            res.ok = False
            res.divergences.append(f"missing {op} step (linux={bool(l_s)} ir0={bool(i_s)})")
            continue

        for label, step in (("linux", l_s), ("ir0", i_s)):
            if step.get("ret") != exp_ret:
                res.ok = False
                res.divergences.append(
                    f"{label} {op}: ret={step.get('ret')} expected={exp_ret}"
                )
            if exp_path is not None and step.get("path") != exp_path:
                res.ok = False
                res.divergences.append(
                    f"{label} {op}: path={step.get('path')} expected={exp_path}"
                )
            if exp_hex is not None:
                got_hex = (step.get("data_hex") or "").lower()
                if got_hex != exp_hex.lower():
                    res.ok = False
                    res.divergences.append(
                        f"{label} {op}: data_hex={got_hex} expected={exp_hex}"
                    )
            if exp_errno is not None and step.get("errno") != exp_errno:
                res.ok = False
                res.divergences.append(
                    f"{label} {op}: errno={step.get('errno')} expected={exp_errno}"
                )

        if exp_errno is not None and l_s.get("errno") != i_s.get("errno"):
            res.ok = False
            res.divergences.append(
                f"{op} errno mismatch linux={l_s.get('errno')} ir0={i_s.get('errno')}"
            )

    l_m = _find_step(l_steps, "mount_tmpfs")
    i_m = _find_step(i_steps, "mount_tmpfs")
    if l_m and i_m and l_m.get("ret") == 0 and i_m.get("ret") == 0:
        res.notes.append(f"linux mount_tmpfs ok path={l_m.get('path')}")
        res.notes.append(f"ir0 mount_tmpfs ok path={i_m.get('path')}")

    if ktest_ok is False:
        res.ok = False
        res.divergences.append("ktest mount_tmpfs_contract FAILED")
    elif ktest_ok is True:
        res.notes.append("ktest mount_tmpfs_contract OK")

    return res


def compare_execve(
    linux: dict,
    ir0: dict,
    enoent_errno: int,
    ktest_ok: bool | None,
) -> CompareResult:
    res = CompareResult(contract="execve", ok=True)

    l_steps = linux.get("audit_steps") or linux.get("strace_steps") or []
    i_steps = ir0.get("audit_steps") or []

    ok_step = _find_step(l_steps, "execve_ok")
    i_ok = _find_step(i_steps, "execve_ok")
    noent_l = _find_step(l_steps, "execve_noent")
    noent_i = _find_step(i_steps, "execve_noent")
    helper_l = _find_step(l_steps, "helper_run")
    helper_i = _find_step(i_steps, "helper_run")

    if not ok_step or not i_ok:
        res.ok = False
        res.divergences.append(
            f"missing execve_ok step (linux={bool(ok_step)} ir0={bool(i_ok)})"
        )
    else:
        for label, step in (("linux", ok_step), ("ir0", i_ok)):
            if step.get("ret", -1) <= 0:
                res.ok = False
                res.divergences.append(
                    f"{label} execve_ok: invalid ret={step.get('ret')}"
                )
            st = step.get("status")
            if st is None:
                res.ok = False
                res.divergences.append(f"{label} execve_ok: missing status word")
            elif st != 0:
                res.ok = False
                res.divergences.append(
                    f"{label} execve_ok: status=0x{st:x} expected 0x0 (exit 0)"
                )
        if ok_step.get("status") != i_ok.get("status"):
            res.ok = False
            res.divergences.append(
                f"execve_ok status mismatch linux=0x{(ok_step.get('status') or 0):x} "
                f"ir0=0x{(i_ok.get('status') or 0):x}"
            )

    if not noent_l or not noent_i:
        res.ok = False
        res.divergences.append(
            f"missing execve_noent step (linux={bool(noent_l)} ir0={bool(noent_i)})"
        )
    else:
        for label, step in (("linux", noent_l), ("ir0", noent_i)):
            if step.get("ret") != -1:
                res.ok = False
                res.divergences.append(
                    f"{label} execve_noent: ret={step.get('ret')} expected -1"
                )
            if step.get("errno") != enoent_errno:
                res.ok = False
                res.divergences.append(
                    f"{label} execve_noent: errno={step.get('errno')} expected={enoent_errno}"
                )
        if noent_l.get("errno") != noent_i.get("errno"):
            res.ok = False
            res.divergences.append(
                f"execve_noent errno mismatch linux={noent_l.get('errno')} ir0={noent_i.get('errno')}"
            )

    if helper_l and helper_i:
        if helper_l.get("ret") != 0 or helper_i.get("ret") != 0:
            res.ok = False
            res.divergences.append("helper_run ret mismatch")
    elif not helper_i and ok_step and i_ok:
        res.notes.append(
            "ir0 helper_run audit line absent (serial interleave); execve_ok status=0 accepted"
        )
    elif not helper_i:
        res.ok = False
        res.divergences.append("ir0 missing helper_run audit line")

    if ok_step and i_ok:
        res.notes.append(
            f"linux execve_ok pid={ok_step.get('ret')} status=0x{(ok_step.get('status') or 0):x}"
        )
        res.notes.append(
            f"ir0 execve_ok pid={i_ok.get('ret')} status=0x{(i_ok.get('status') or 0):x}"
        )

    if ktest_ok is False:
        res.ok = False
        res.divergences.append("ktest execve contract FAILED")
    elif ktest_ok is True:
        res.notes.append("ktest execve contract OK")

    return res


def compare_openat(
    linux: dict,
    ir0: dict,
    enoent_errno: int,
    ebadf_errno: int,
    ktest_ok: bool | None,
) -> CompareResult:
    res = CompareResult(contract="openat", ok=True)

    l_steps = linux.get("audit_steps") or linux.get("strace_steps") or []
    i_steps = ir0.get("audit_steps") or []

    checks = (
        ("open_existing", lambda r: r is not None and r >= 3, 0),
        ("close_ok", lambda r: r == 0, 0),
        ("open_noent", lambda r: r == -1, enoent_errno),
        ("close_ebadf", lambda r: r == -1, ebadf_errno),
    )

    for op, ret_ok, exp_errno in checks:
        l_s = _find_step(l_steps, op)
        i_s = _find_step(i_steps, op)
        if not l_s or not i_s:
            res.ok = False
            res.divergences.append(f"missing {op} step (linux={bool(l_s)} ir0={bool(i_s)})")
            continue

        for label, step in (("linux", l_s), ("ir0", i_s)):
            got_ret = step.get("ret")
            if not ret_ok(got_ret):
                res.ok = False
                res.divergences.append(
                    f"{label} {op}: ret={got_ret} unexpected"
                )
            if step.get("errno") != exp_errno:
                res.ok = False
                res.divergences.append(
                    f"{label} {op}: errno={step.get('errno')} expected={exp_errno}"
                )

        if op == "open_noent" and l_s.get("errno") != i_s.get("errno"):
            res.ok = False
            res.divergences.append(
                f"open_noent errno mismatch linux={l_s.get('errno')} ir0={i_s.get('errno')}"
            )
        if op == "close_ebadf" and l_s.get("errno") != i_s.get("errno"):
            res.ok = False
            res.divergences.append(
                f"close_ebadf errno mismatch linux={l_s.get('errno')} ir0={i_s.get('errno')}"
            )

    l_open = _find_step(l_steps, "open_existing")
    i_open = _find_step(i_steps, "open_existing")
    if l_open and i_open:
        res.notes.append(f"linux open_existing fd={l_open.get('ret')}")
        res.notes.append(f"ir0 open_existing fd={i_open.get('ret')}")

    if ktest_ok is False:
        res.ok = False
        res.divergences.append("ktest syscall_open_close FAILED")
    elif ktest_ok is True:
        res.notes.append("ktest syscall_open_close OK")

    return res


S_IFMT = 0o0170000
S_IFREG = 0o0100000


def _mode_type(mode: int | None) -> int | None:
    if mode is None:
        return None
    return mode & S_IFMT


def compare_stat(
    linux: dict,
    ir0: dict,
    enoent_errno: int,
    ebadf_errno: int,
    host_ok: bool | None,
) -> CompareResult:
    res = CompareResult(contract="stat", ok=True)

    l_steps = linux.get("audit_steps") or linux.get("strace_steps") or []
    i_steps = ir0.get("audit_steps") or []

    def check_ok(op: str, need_size: bool, need_reg: bool) -> None:
        l_s = _find_step(l_steps, op)
        i_s = _find_step(i_steps, op)
        if not l_s or not i_s:
            res.ok = False
            res.divergences.append(f"missing {op} step (linux={bool(l_s)} ir0={bool(i_s)})")
            return
        for label, step in (("linux", l_s), ("ir0", i_s)):
            if step.get("ret") != 0:
                res.ok = False
                res.divergences.append(f"{label} {op}: ret={step.get('ret')} expected 0")
            mt = _mode_type(step.get("mode"))
            if need_reg and mt != S_IFREG:
                res.ok = False
                res.divergences.append(f"{label} {op}: mode type 0{mt:o} expected regular file")
            if need_size and (step.get("size") or 0) <= 0:
                res.ok = False
                res.divergences.append(f"{label} {op}: size={step.get('size')} expected >0")
        l_mt = _mode_type(l_s.get("mode"))
        i_mt = _mode_type(i_s.get("mode"))
        if l_mt != i_mt:
            res.ok = False
            res.divergences.append(
                f"{op} mode type mismatch linux=0{l_mt:o} ir0=0{i_mt:o}"
            )
        l_nl = l_s.get("nlink")
        i_nl = i_s.get("nlink")
        if l_nl is not None and i_nl is not None and l_nl > 0 and i_nl > 0:
            if l_nl != i_nl:
                res.notes.append(
                    f"{op} nlink differs linux={l_nl} ir0={i_nl} (informational)"
                )

    check_ok("stat_proc", need_size=False, need_reg=False)
    check_ok("stat_file", need_size=True, need_reg=True)
    check_ok("fstat_proc", need_size=False, need_reg=False)

    for op, exp_errno in (("stat_noent", enoent_errno), ("fstat_ebadf", ebadf_errno)):
        l_s = _find_step(l_steps, op)
        i_s = _find_step(i_steps, op)
        if not l_s or not i_s:
            res.ok = False
            res.divergences.append(f"missing {op} step (linux={bool(l_s)} ir0={bool(i_s)})")
            continue
        for label, step in (("linux", l_s), ("ir0", i_s)):
            if step.get("ret") != -1:
                res.ok = False
                res.divergences.append(f"{label} {op}: ret={step.get('ret')} expected -1")
            if step.get("errno") != exp_errno:
                res.ok = False
                res.divergences.append(
                    f"{label} {op}: errno={step.get('errno')} expected={exp_errno}"
                )

    l_proc = _find_step(l_steps, "stat_proc")
    i_proc = _find_step(i_steps, "stat_proc")
    if l_proc and i_proc:
        res.notes.append(
            f"linux stat_proc mode=0{l_proc.get('mode', 0):o} size={l_proc.get('size')}"
        )
        res.notes.append(
            f"ir0 stat_proc mode=0{i_proc.get('mode', 0):o} size={i_proc.get('size')}"
        )

    if host_ok is False:
        res.ok = False
        res.divergences.append("host test stat_user_abi FAILED")
    elif host_ok is True:
        res.notes.append("host test stat_user_abi OK")

    return res


VFS_WRITE_REQUIRED_OPS = [
    "setup_wd",
    "open_creat",
    "open_excl_eexist",
    "open_trunc",
    "write_hello",
    "open_read",
    "read_data",
    "read_eof",
    "open_append",
    "write_append",
    "stat_after_append",
    "open_lseek",
    "lseek_set",
    "read_after_lseek_set",
    "lseek_cur",
    "lseek_end",
    "unlink_ok",
    "unlink_noent",
    "unlink_dir",
    "rename_ok",
    "rename_overwrite",
    "rename_noent",
    "mkdir_ok",
    "mkdir_eexist",
    "rmdir_empty",
    "rmdir_nonempty",
    "truncate_shrink",
    "truncate_grow",
    "ftruncate_shrink",
]

VFS_WRITE_OPTIONAL_OPS: set[str] = set()

VFS_WRITE_ERROR_OPS = {
    "open_excl_eexist": "eexist_errno",
    "unlink_noent": "enoent_errno",
    "unlink_dir": "eisdir_errno",
    "rename_noent": "enoent_errno",
    "mkdir_eexist": "eexist_errno",
    "rmdir_nonempty": "enotempty_errno",
}

VFS_WRITE_FD_OPS = {
    "open_creat",
    "open_trunc",
    "open_read",
    "open_append",
    "open_lseek",
}

VFS_WRITE_RET_MATCH_OPS = {
    "write_hello",
    "read_data",
    "read_eof",
    "write_append",
    "lseek_set",
    "read_after_lseek_set",
    "lseek_cur",
    "lseek_end",
    "truncate_shrink",
    "truncate_grow",
    "ftruncate_shrink",
}

VFS_WRITE_DATA_HEX_OPS = {"read_data", "read_after_lseek_set"}


def compare_vfs_write(
    linux: dict,
    ir0: dict,
    cfg: dict,
) -> CompareResult:
    res = CompareResult(contract="vfs_write", ok=True)

    l_steps = linux.get("audit_steps") or []
    i_steps = ir0.get("audit_steps") or []
    optional_ops = set(cfg.get("optional_ops") or VFS_WRITE_OPTIONAL_OPS)
    enosys_errno = int(cfg.get("enosys_errno", 38))

    def step_table() -> None:
        res.notes.append("step | op | linux ret/errno | ir0 ret/errno | match")
        for op in VFS_WRITE_REQUIRED_OPS + sorted(optional_ops):
            l_s = _find_step(l_steps, op)
            i_s = _find_step(i_steps, op)
            if not l_s and not i_s:
                continue
            l_txt = (
                f"{l_s.get('ret')}/{l_s.get('errno')}"
                if l_s
                else "missing"
            )
            i_txt = (
                f"{i_s.get('ret')}/{i_s.get('errno')}"
                if i_s
                else "missing"
            )
            match = "?"
            if l_s and i_s:
                if op in VFS_WRITE_ERROR_OPS:
                    exp = int(cfg.get(VFS_WRITE_ERROR_OPS[op], 2))
                    match = (
                        "OK"
                        if l_s.get("ret") == -1
                        and i_s.get("ret") == -1
                        and l_s.get("errno") == exp
                        and i_s.get("errno") == exp
                        else "FAIL"
                    )
                elif op in VFS_WRITE_FD_OPS:
                    match = (
                        "OK"
                        if (l_s.get("ret") or -1) >= 3
                        and (i_s.get("ret") or -1) >= 3
                        else "FAIL"
                    )
                elif op in VFS_WRITE_RET_MATCH_OPS:
                    match = (
                        "OK"
                        if l_s.get("ret") == i_s.get("ret")
                        else "FAIL"
                    )
                elif op == "stat_after_append":
                    match = (
                        "OK"
                        if l_s.get("stat_size") == i_s.get("stat_size") == 7
                        else "FAIL"
                    )
                else:
                    match = (
                        "OK"
                        if l_s.get("ret") == i_s.get("ret")
                        and l_s.get("errno") == i_s.get("errno")
                        else "FAIL"
                    )
            else:
                match = "MISSING"
            res.notes.append(f"{op} | {l_txt} | {i_txt} | {match}")

    for op in VFS_WRITE_REQUIRED_OPS:
        l_s = _find_step(l_steps, op)
        i_s = _find_step(i_steps, op)
        if not l_s or not i_s:
            res.ok = False
            res.divergences.append(
                f"missing required op={op} (linux={bool(l_s)} ir0={bool(i_s)})"
            )
            continue

        if op in VFS_WRITE_ERROR_OPS:
            exp = int(cfg.get(VFS_WRITE_ERROR_OPS[op], 2))
            for label, step in (("linux", l_s), ("ir0", i_s)):
                if step.get("ret") != -1:
                    res.ok = False
                    res.divergences.append(
                        f"{label} {op}: ret={step.get('ret')} expected -1"
                    )
                if step.get("errno") != exp:
                    res.ok = False
                    res.divergences.append(
                        f"{label} {op}: errno={step.get('errno')} expected={exp}"
                    )
            continue

        if op in VFS_WRITE_FD_OPS:
            for label, step in (("linux", l_s), ("ir0", i_s)):
                if (step.get("ret") or -1) < 3:
                    res.ok = False
                    res.divergences.append(
                        f"{label} {op}: ret={step.get('ret')} expected fd>=3"
                    )
            continue

        if op in VFS_WRITE_RET_MATCH_OPS:
            if l_s.get("ret") != i_s.get("ret"):
                res.ok = False
                res.divergences.append(
                    f"{op} ret mismatch linux={l_s.get('ret')} ir0={i_s.get('ret')}"
                )
            if op in VFS_WRITE_DATA_HEX_OPS:
                l_hex = (l_s.get("data_hex") or "").lower()
                i_hex = (i_s.get("data_hex") or "").lower()
                if l_hex != i_hex:
                    res.ok = False
                    res.divergences.append(
                        f"{op} data_hex mismatch linux={l_hex} ir0={i_hex}"
                    )
            continue

        if op == "stat_after_append":
            if l_s.get("stat_size") != 7 or i_s.get("stat_size") != 7:
                res.ok = False
                res.divergences.append(
                    f"stat_after_append size linux={l_s.get('stat_size')} "
                    f"ir0={i_s.get('stat_size')} expected 7"
                )
            continue

        if l_s.get("ret") != i_s.get("ret") or l_s.get("errno") != i_s.get("errno"):
            res.ok = False
            res.divergences.append(
                f"{op} mismatch linux ret={l_s.get('ret')} errno={l_s.get('errno')} "
                f"ir0 ret={i_s.get('ret')} errno={i_s.get('errno')}"
            )

    optional_gaps: list[str] = []
    for op in sorted(optional_ops):
        l_s = _find_step(l_steps, op)
        i_s = _find_step(i_steps, op)
        if not l_s or not i_s:
            optional_gaps.append(f"{op}: missing step")
            continue
        if l_s.get("ret") == 0 and i_s.get("ret") == 0:
            continue
        if l_s.get("ret") == 0 and i_s.get("ret") == -1 and i_s.get("errno") == enosys_errno:
            optional_gaps.append(
                f"{op}: IR0 ENOSYS (documented MINIX/syscall gap)"
            )
            continue
        res.ok = False
        res.divergences.append(
            f"{op} optional mismatch linux ret={l_s.get('ret')} "
            f"ir0 ret={i_s.get('ret')} errno={i_s.get('errno')}"
        )

    if res.ok and not optional_gaps:
        res.notes.append("bundle_status: VERIFIED")
    elif not res.divergences and optional_gaps:
        res.ok = False
        res.notes.append("bundle_status: PARTIAL")
        for gap in optional_gaps:
            res.notes.append(f"optional_gap: {gap}")
    elif res.divergences:
        res.notes.append("bundle_status: BLOCKED")
    else:
        res.notes.append("bundle_status: PARTIAL")

    step_table()
    return res


PROCESS_LIFECYCLE_REQUIRED_OPS = [
    "wait4_exit42",
    "wait4_any",
    "wait4_wnohang_alive",
    "wait4_wnohang_reap",
    "wait4_echild",
    "execve_ok",
    "execve_noent",
    "wait4_exit1",
    "kill_sigterm",
    "sigchld_default_wait",
]

PROCESS_LIFECYCLE_IR0_REPARENT_OPS = [
    "reparent_mid_wait",
    "reparent_orphan_wait",
]


def _exit_status(code: int) -> int:
    return code << 8


def compare_process_lifecycle(
    linux: dict,
    ir0: dict,
    cfg: dict,
) -> CompareResult:
    res = CompareResult(contract="process_lifecycle", ok=True)

    l_steps = linux.get("audit_steps") or []
    i_steps = ir0.get("audit_steps") or []
    echild_errno = int(cfg.get("echild_errno", 10))
    enoent_errno = int(cfg.get("enoent_errno", 2))

    def check_wait_exit(op: str, exit_code: int) -> None:
        exp = _exit_status(exit_code)
        l_s = _find_step(l_steps, op)
        i_s = _find_step(i_steps, op)
        if not l_s or not i_s:
            res.ok = False
            res.divergences.append(
                f"missing {op} (linux={bool(l_s)} ir0={bool(i_s)})"
            )
            return
        for label, step in (("linux", l_s), ("ir0", i_s)):
            if step.get("ret", -1) <= 0:
                res.ok = False
                res.divergences.append(f"{label} {op}: ret={step.get('ret')} expected pid>0")
            st = step.get("status")
            if st != exp:
                res.ok = False
                res.divergences.append(
                    f"{label} {op}: status=0x{(st or 0):x} expected 0x{exp:x}"
                )
        if l_s.get("status") != i_s.get("status"):
            res.ok = False
            res.divergences.append(
                f"{op} status mismatch linux=0x{(l_s.get('status') or 0):x} "
                f"ir0=0x{(i_s.get('status') or 0):x}"
            )

    check_wait_exit("wait4_exit42", 42)
    check_wait_exit("wait4_any", 10)
    check_wait_exit("wait4_wnohang_reap", 5)
    check_wait_exit("wait4_exit1", 1)
    check_wait_exit("sigchld_default_wait", 7)

    l_wh = _find_step(l_steps, "wait4_wnohang_alive")
    i_wh = _find_step(i_steps, "wait4_wnohang_alive")
    if not l_wh or not i_wh:
        res.ok = False
        res.divergences.append(
            f"missing wait4_wnohang_alive (linux={bool(l_wh)} ir0={bool(i_wh)})"
        )
    else:
        for label, step in (("linux", l_wh), ("ir0", i_wh)):
            if step.get("ret") != 0:
                res.ok = False
                res.divergences.append(
                    f"{label} wait4_wnohang_alive: ret={step.get('ret')} expected 0"
                )

    l_ec = _find_step(l_steps, "wait4_echild")
    i_ec = _find_step(i_steps, "wait4_echild")
    if not l_ec or not i_ec:
        res.ok = False
        res.divergences.append(
            f"missing wait4_echild (linux={bool(l_ec)} ir0={bool(i_ec)})"
        )
    else:
        for label, step in (("linux", l_ec), ("ir0", i_ec)):
            if step.get("ret") != -1:
                res.ok = False
                res.divergences.append(
                    f"{label} wait4_echild: ret={step.get('ret')} expected -1"
                )
            if step.get("errno") != echild_errno:
                res.ok = False
                res.divergences.append(
                    f"{label} wait4_echild: errno={step.get('errno')} expected={echild_errno}"
                )

    l_ex = _find_step(l_steps, "execve_ok")
    i_ex = _find_step(i_steps, "execve_ok")
    if not l_ex or not i_ex:
        res.ok = False
        res.divergences.append(
            f"missing execve_ok (linux={bool(l_ex)} ir0={bool(i_ex)})"
        )
    else:
        for label, step in (("linux", l_ex), ("ir0", i_ex)):
            if step.get("status") != 0:
                res.ok = False
                res.divergences.append(f"{label} execve_ok: status=0x{step.get('status', 0):x} expected 0")

    exp_noent = _exit_status(127)
    l_ne = _find_step(l_steps, "execve_noent")
    i_ne = _find_step(i_steps, "execve_noent")
    if not l_ne or not i_ne:
        res.ok = False
        res.divergences.append(
            f"missing execve_noent (linux={bool(l_ne)} ir0={bool(i_ne)})"
        )
    else:
        for label, step in (("linux", l_ne), ("ir0", i_ne)):
            if step.get("status") != exp_noent:
                res.ok = False
                res.divergences.append(
                    f"{label} execve_noent: status=0x{(step.get('status') or 0):x} expected 0x{exp_noent:x}"
                )

    l_ks = _find_step(l_steps, "kill_sigterm")
    i_ks = _find_step(i_steps, "kill_sigterm")
    if not l_ks or not i_ks:
        res.ok = False
        res.divergences.append(
            f"missing kill_sigterm (linux={bool(l_ks)} ir0={bool(i_ks)})"
        )
    else:
        for label, step in (("linux", l_ks), ("ir0", i_ks)):
            st = step.get("status")
            sig = st & 0x7F if st is not None else -1
            if sig != 15:
                res.ok = False
                res.divergences.append(
                    f"{label} kill_sigterm: WTERMSIG={sig} expected 15 (SIGTERM)"
                )
        if (l_ks.get("status") or 0) != (i_ks.get("status") or 0):
            res.ok = False
            res.divergences.append(
                f"kill_sigterm status mismatch linux=0x{(l_ks.get('status') or 0):x} "
                f"ir0=0x{(i_ks.get('status') or 0):x}"
            )

    l_skip = _find_step(l_steps, "reparent_skip")
    i_skip = _find_step(i_steps, "reparent_skip")
    if l_skip and l_skip.get("flags") == "not_pid1":
        res.notes.append("linux reparent_skip (not PID1 host run — expected)")
    elif not l_skip:
        check_wait_exit("reparent_mid_wait", 0)
        check_wait_exit("reparent_orphan_wait", 99)

    if i_skip:
        res.ok = False
        res.divergences.append("ir0 reparent_skip — probe must run as PID1 on IR0")
    else:
        for op, code in (("reparent_mid_wait", 0), ("reparent_orphan_wait", 99)):
            i_s = _find_step(i_steps, op)
            if not i_s:
                res.ok = False
                res.divergences.append(f"ir0 missing required {op}")
                continue
            exp = _exit_status(code)
            if i_s.get("status") != exp:
                res.ok = False
                res.divergences.append(
                    f"ir0 {op}: status=0x{(i_s.get('status') or 0):x} expected 0x{exp:x}"
                )

    missing = []
    for op in PROCESS_LIFECYCLE_REQUIRED_OPS:
        if not _find_step(l_steps, op):
            missing.append(f"linux:{op}")
        if not _find_step(i_steps, op):
            missing.append(f"ir0:{op}")
    if missing:
        res.notes.append(f"missing ops: {', '.join(missing)}")

    bundle = "VERIFIED" if res.ok else "BLOCKED"
    res.notes.insert(0, f"bundle_status: {bundle}")
    if res.divergences:
        step_order = {op: i for i, op in enumerate(PROCESS_LIFECYCLE_REQUIRED_OPS)}
        res.divergences.sort(
            key=lambda d: next(
                (step_order.get(op, 99) for op in step_order if op in d),
                99,
            )
        )
        res.notes.insert(1, f"first_divergence: {res.divergences[0]}")

    return res


def compare_pipe(
    linux: dict,
    ir0: dict,
    pipe_read_len: int,
    pipe_data_hex: str,
    ebadf_errno: int,
    ktest_ok: bool | None,
) -> CompareResult:
    res = compare_read(
        linux, ir0, pipe_read_len, pipe_data_hex, ebadf_errno, ktest_ok
    )
    res.contract = "pipe"
    return res


def compare_poll(linux: dict, ir0: dict, ktest_ok: bool | None) -> CompareResult:
    res = CompareResult(contract="poll", ok=True)
    l_steps = linux.get("audit_steps") or []
    i_steps = ir0.get("audit_steps") or []
    l_s = _find_step(l_steps, "poll_pipe")
    i_s = _find_step(i_steps, "poll_pipe")

    if not l_s or not i_s:
        res.ok = False
        res.divergences.append(
            f"missing poll_pipe step (linux={bool(l_s)} ir0={bool(i_s)})"
        )
        return res

    for label, step in (("linux", l_s), ("ir0", i_s)):
        if step.get("ret") != 1:
            res.ok = False
            res.divergences.append(f"{label} poll_pipe: ret={step.get('ret')} expected=1")
        revents = step.get("revents")
        if revents is None or (int(revents) & 1) == 0:
            res.ok = False
            res.divergences.append(f"{label} poll_pipe: revents={revents} expected POLLIN")

    if l_s.get("ret") != i_s.get("ret"):
        res.ok = False
        res.divergences.append(
            f"poll_pipe ret mismatch linux={l_s.get('ret')} ir0={i_s.get('ret')}"
        )

    if ktest_ok is False:
        res.ok = False
        res.divergences.append("ktest poll_resume_invariant FAILED")
    elif ktest_ok is True:
        res.notes.append("ktest poll_resume_invariant OK")

    return res


def compare_nanosleep(linux: dict, ir0: dict) -> CompareResult:
    res = CompareResult(contract="nanosleep", ok=True)
    l_s = _find_step(linux.get("audit_steps") or [], "nanosleep_2ms")
    i_s = _find_step(ir0.get("audit_steps") or [], "nanosleep_2ms")

    if not l_s or not i_s:
        res.ok = False
        res.divergences.append(
            f"missing nanosleep_2ms step (linux={bool(l_s)} ir0={bool(i_s)})"
        )
        return res

    for label, step in (("linux", l_s), ("ir0", i_s)):
        if step.get("ret") != 0:
            res.ok = False
            res.divergences.append(
                f"{label} nanosleep_2ms: ret={step.get('ret')} expected=0"
            )

    return res


def compare_sigreturn_blocked_syscall(linux: dict, ir0: dict) -> CompareResult:
    res = CompareResult(contract="sigreturn_blocked_syscall", ok=True)
    ops = ("read_eintr", "read_after_eintr", "read_sa_restart")
    expected = {
        "read_eintr": {"ret": -1, "errno": 4},
        "read_after_eintr": {"ret": 1, "errno": 0},
        "read_sa_restart": {"ret": 1, "errno": 0},
    }

    for op in ops:
        l_s = _find_step(linux.get("audit_steps") or [], op)
        i_s = _find_step(ir0.get("audit_steps") or [], op)
        if not l_s or not i_s:
            res.ok = False
            res.divergences.append(
                f"missing {op} step (linux={bool(l_s)} ir0={bool(i_s)})"
            )
            continue
        exp = expected[op]
        for label, step in (("linux", l_s), ("ir0", i_s)):
            if step.get("ret") != exp["ret"]:
                res.ok = False
                res.divergences.append(
                    f"{label} {op}: ret={step.get('ret')} expected={exp['ret']}"
                )
            if step.get("errno") != exp["errno"]:
                res.ok = False
                res.divergences.append(
                    f"{label} {op}: errno={step.get('errno')} expected={exp['errno']}"
                )

    return res


def compare_getcwd(linux: dict, ir0: dict, expected_path: str) -> CompareResult:
    res = CompareResult(contract="getcwd", ok=True)
    l_s = _find_step(linux.get("audit_steps") or [], "getcwd_root")
    i_s = _find_step(ir0.get("audit_steps") or [], "getcwd_root")

    if not l_s or not i_s:
        res.ok = False
        res.divergences.append(
            f"missing getcwd_root step (linux={bool(l_s)} ir0={bool(i_s)})"
        )
        return res

    for label, step in (("linux", l_s), ("ir0", i_s)):
        path = step.get("path")
        if path != expected_path:
            res.ok = False
            res.divergences.append(
                f"{label} getcwd_root: path={path!r} expected={expected_path!r}"
            )
        if step.get("errno") != 0:
            res.ok = False
            res.divergences.append(f"{label} getcwd_root: errno={step.get('errno')}")

    l_path = l_s.get("path")
    i_path = i_s.get("path")
    if l_path != i_path:
        res.ok = False
        res.divergences.append(f"getcwd path mismatch linux={l_path!r} ir0={i_path!r}")

    res.notes.append(f"cwd path linux={l_path} ir0={i_path}")
    return res


def compare_chdir(linux: dict, ir0: dict, target_path: str) -> CompareResult:
    res = CompareResult(contract="chdir", ok=True)
    l_steps = linux.get("audit_steps") or []
    i_steps = ir0.get("audit_steps") or []
    l_ch = _find_step(l_steps, "chdir_target")
    i_ch = _find_step(i_steps, "chdir_target")
    l_cw = _find_step(l_steps, "getcwd_after")
    i_cw = _find_step(i_steps, "getcwd_after")

    if not l_ch or not i_ch or not l_cw or not i_cw:
        res.ok = False
        res.divergences.append("missing chdir_target or getcwd_after step")
        return res

    for label, step in (("linux", l_ch), ("ir0", i_ch)):
        if step.get("ret") != 0:
            res.ok = False
            res.divergences.append(f"{label} chdir_target: ret={step.get('ret')}")

    for label, step in (("linux", l_cw), ("ir0", i_cw)):
        if step.get("path") != target_path:
            res.ok = False
            res.divergences.append(
                f"{label} getcwd_after: path={step.get('path')!r} expected={target_path!r}"
            )

    return res


def compare_dup(linux: dict, ir0: dict, ebadf_errno: int) -> CompareResult:
    res = CompareResult(contract="dup", ok=True)
    l_steps = linux.get("audit_steps") or []
    i_steps = ir0.get("audit_steps") or []

    required = (
        ("open_existing", None, None),
        ("fcntl_set_cloexec", None, None),
        ("dup_clears_cloexec", None, None),
        ("dup_getfd", 0, None),
        ("dup2_clears_cloexec", 20, None),
        ("dup2_getfd", 0, None),
        ("dup2_ebadf", -1, ebadf_errno),
    )

    for op, exp_ret, exp_errno in required:
        l_s = _find_step(l_steps, op)
        i_s = _find_step(i_steps, op)
        if not l_s or not i_s:
            res.ok = False
            res.divergences.append(
                f"missing {op} step (linux={bool(l_s)} ir0={bool(i_s)})"
            )
            continue

        if op in ("open_existing", "dup_clears_cloexec"):
            for label, step in (("linux", l_s), ("ir0", i_s)):
                if step.get("ret", -1) < 0:
                    res.ok = False
                    res.divergences.append(f"{label} {op}: ret={step.get('ret')}")
            continue

        if op == "fcntl_set_cloexec":
            for label, step in (("linux", l_s), ("ir0", i_s)):
                flags = step.get("ret")
                if flags is None or (int(flags) & 1) == 0:
                    res.ok = False
                    res.divergences.append(
                        f"{label} {op}: flags={flags} expected FD_CLOEXEC"
                    )
            continue

        for label, step in (("linux", l_s), ("ir0", i_s)):
            if exp_ret is not None and step.get("ret") != exp_ret:
                res.ok = False
                res.divergences.append(
                    f"{label} {op}: ret={step.get('ret')} expected={exp_ret}"
                )
            if exp_errno is not None and step.get("errno") != exp_errno:
                res.ok = False
                res.divergences.append(
                    f"{label} {op}: errno={step.get('errno')} expected={exp_errno}"
                )

        if l_s.get("ret") != i_s.get("ret"):
            res.ok = False
            res.divergences.append(
                f"{op} ret mismatch linux={l_s.get('ret')} ir0={i_s.get('ret')}"
            )

    res.notes.append("dup/dup2 clear FD_CLOEXEC on the new descriptor (Linux man 2 dup)")
    return res


def render_markdown(results: list[CompareResult], meta: dict) -> str:
    lines = [
        "# Linux ABI audit report",
        "",
        f"> Generated: {meta.get('generated', 'unknown')}",
        f"> Contracts run: {', '.join(meta.get('contracts', []))}",
        "",
    ]

    for r in results:
        status = "PASS" if r.ok else "FAIL"
        lines.append(f"## {r.contract} — {status}")
        lines.append("")
        if r.notes:
            lines.append("Notes:")
            for n in r.notes:
                lines.append(f"- {n}")
            lines.append("")
        if r.divergences:
            lines.append("First divergences:")
            for d in r.divergences:
                lines.append(f"- {d}")
            lines.append("")
        lines.append("---")
        lines.append("")

    overall = all(r.ok for r in results)
    lines.append(f"## Overall: {'PASS' if overall else 'FAIL'}")
    lines.append("")
    return "\n".join(lines)
