#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Shared TTY / keyboard corruption guards for QEMU console smokes."""

from __future__ import annotations

import re
import sys
from typing import Iterable

# Shell prompt lines and ash error lines must stay 7-bit printable.
PROMPT_LINE_RE = re.compile(
    r"^[a-zA-Z0-9_-]+@[a-zA-Z0-9_-]+:\S*[#$]\s*(.*)$"
)
ASH_CMD_ERR_RE = re.compile(r"^-sh:\s*(.+?):\s*(not found|Invalid argument)")
NONASCII_RUN_RE = re.compile(r"[^\x20-\x7e]+")
# Stuck-key / QEMU double-fire (hheexxdduummpp), not intentional typos (llss).
DOUBLED_PAIR_RUN_RE = re.compile(r"(?:([a-zA-Z])\1){3,}")
# run supervisor must not get musl SIGCHLD handler delivery during wait4.
RUN_MUSL_SIGCHLD_RE = re.compile(
    r"DELIVER_CTX sig=17[^\n]*handler=401[a-f0-9]{3}"
)

FATAL_TAGS = (
    "KERNEL PANIC",
    "KERNEL_UACCESS_FAULT",
    "USER_FAULT_FRAME",
    "CONSOLE_SESSION_SEGV",
    "FILES_STRUCT_BAD",
    "USER_RESUME_KSTACK_GPR_LEAK",
    "sh: out of memory",
)

ABNORMAL_SESSION = "shell exited abnormally"


def sanitize(s: str) -> str:
    return "".join(
        c if (32 <= ord(c) < 127 or c in "\n\t") else f"\\x{ord(c):02x}"
        for c in s
    )


def find_nonascii_runs(line: str, min_run: int = 1) -> list[str]:
    return [m.group(0) for m in NONASCII_RUN_RE.finditer(line) if len(m.group(0)) >= min_run]


def check_fatal_tags(text: str, window: str | None = None) -> list[str]:
    hay = window if window is not None else text
    return [tag for tag in FATAL_TAGS if tag in hay]


def check_abnormal_session_end(text: str, baseline_ends: int) -> str | None:
    if text.count("CONSOLE_SESSION_END") <= baseline_ends:
        return None
    if ABNORMAL_SESSION in text:
        return ABNORMAL_SESSION
    return "unexpected CONSOLE_SESSION_END"


def check_run_supervisor_stable(text: str, *, mark: int = 0) -> list[str]:
    """Fail if runit respawns getty mid-session or run gets musl SIGCHLD."""
    errors: list[str] = []
    window = text[mark:]
    pos = 0
    while True:
        idx = window.find("CONSOLE_SESSION_START", pos)
        if idx < 0:
            break
        segment = window[idx:]
        end = segment.find("CONSOLE_SESSION_END")
        body = segment[:end] if end >= 0 else segment
        if "RUNSV_CONSOLE_START" in body:
            errors.append(
                "run supervisor restarted mid-session (RUNSV_CONSOLE_START "
                "before CONSOLE_SESSION_END)"
            )
        for m in RUN_MUSL_SIGCHLD_RE.finditer(body):
            errors.append(
                f"SIGCHLD delivered to run musl handler during session: "
                f"{sanitize(m.group(0))!r}"
            )
        pos = idx + len("CONSOLE_SESSION_START")
    return errors


def check_prompt_username_garbled(text: str, *, mark: int = 0) -> list[str]:
    """Fail when prompt user@host shows injected prefix (bivan, bin/true typos)."""
    errors: list[str] = []
    for ln in text[mark:].splitlines():
        m = re.match(r"^([a-zA-Z0-9_-]+)@([a-zA-Z0-9_-]+):", ln.strip())
        if not m:
            continue
        user, host = m.group(1), m.group(2)
        if user.startswith("bin") or user.startswith("in") or len(user) > 32:
            errors.append(f"garbled shell username in prompt: {ln.strip()!r}")
        if host.startswith("in") or "/" in host:
            errors.append(f"garbled shell hostname in prompt: {ln.strip()!r}")
    return errors


def check_doubled_keystrokes(text: str, *, mark: int = 0) -> list[str]:
    errors: list[str] = []
    window = text[mark:]
    for ln in window.splitlines():
        m = PROMPT_LINE_RE.match(ln.strip())
        if m and DOUBLED_PAIR_RUN_RE.search(m.group(1)):
            errors.append(
                f"doubled keystrokes on prompt line: {sanitize(ln)!r}"
            )
        em = ASH_CMD_ERR_RE.match(ln.strip())
        if em and DOUBLED_PAIR_RUN_RE.search(em.group(1)):
            errors.append(
                f"doubled keystrokes in ash error: {sanitize(ln)!r}"
            )
    return errors


def check_typing_garbage(text: str, *, mark: int = 0, min_run: int = 1) -> list[str]:
    """Fail on non-ASCII echo, garbage ash errors, or Invalid argument on ASCII cmds."""
    errors: list[str] = []
    window = text[mark:]

    for tag in check_fatal_tags(text, window):
        errors.append(f"fatal tag: {tag}")

    errors.extend(check_run_supervisor_stable(text, mark=mark))
    errors.extend(check_doubled_keystrokes(text, mark=mark))
    errors.extend(check_prompt_username_garbled(text, mark=mark))

    for ln in window.splitlines():
        m = PROMPT_LINE_RE.match(ln.strip())
        if m:
            tail = m.group(1)
            for run in find_nonascii_runs(tail, min_run):
                errors.append(
                    f"non-ASCII on prompt input line: {sanitize(ln)!r} run={sanitize(run)!r}"
                )

        em = ASH_CMD_ERR_RE.match(ln.strip())
        if em:
            cmd = em.group(1)
            reason = em.group(2)
            bad = find_nonascii_runs(cmd, 1)
            if bad:
                errors.append(
                    f"non-ASCII command in ash error: {sanitize(ln)!r}"
                )
            elif reason == "Invalid argument" and all(32 <= ord(c) < 127 for c in cmd):
                errors.append(
                    f"Invalid argument on pure-ASCII command (keyboard/TTY bug): {cmd!r}"
                )

        if "Invalid argument" in ln and find_nonascii_runs(ln, 1):
            errors.append(f"Invalid argument line with non-ASCII: {sanitize(ln)!r}")

    return errors


def report_guard_failures(errors: Iterable[str], log_tail: str = "") -> int:
    for err in errors:
        print(f"✗ {err}", file=sys.stderr)
    if log_tail:
        print("--- serial tail ---", file=sys.stderr)
        print(log_tail[-4000:], file=sys.stderr)
    return 1 if errors else 0
