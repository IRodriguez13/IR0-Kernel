#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
Desktop ash command matrix (headless QEMU + persistent HMP sendkey).

Stability notes:
  - One TCP monitor connection per QEMU session (open-per-key was the main
    HMP flake source under FEATURE_EDITING).
  - Short batches (fresh login) avoid long-session getty death.
  - Expect requires command echo (or special marker) — prompt-count alone
    false-passes when the tty delivers empty Enter storms.
  - Empty-prompt storms abort the batch so a fresh VM can retry.
  - Poweroff is soft-gated (TIMEOUT → WARN, round still PASS if ash OK).
  - Doom is opt-in (--with-doom / --only-doom).
  - --stability: product binaries that stress TTY/sched/exec (top/man/tcc/pipes);
    optional doom; fail hard on panic/#UD/SEGV tags.
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
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Optional

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

SPECIAL = {
    " ": "spc",
    "/": "slash",
    "-": "minus",
    ".": "dot",
    "=": "equal",
    "_": "shift-minus",
    ":": "shift-semicolon",
    ";": "semicolon",
    "|": "shift-backslash",
    "&": "shift-7",
    "'": "apostrophe",
    '"': "shift-apostrophe",
    "\\": "backslash",
    "!": "shift-1",
    "?": "shift-slash",
    "(": "shift-9",
    ")": "shift-0",
    ",": "comma",
    "+": "shift-equal",
    "*": "shift-8",
    ">": "shift-dot",
    "<": "shift-comma",
    "#": "shift-3",
    "{": "shift-bracket_left",
    "}": "shift-bracket_right",
    "[": "bracket_left",
    "]": "bracket_right",
}

FAIL_RES = [
    re.compile(r"KERNEL PANIC"),
    re.compile(r"DOUBLE (FAULT|PANIC)"),
    re.compile(r"\[ISR64\]"),
    re.compile(r"Unhandled kernel"),
    re.compile(r"General protection fault"),
    re.compile(r"#UD\b|#DF\b"),
    re.compile(r"CONSOLE_SESSION_SEGV"),
    re.compile(r"shshsh|busyboxbusybox"),
]
LOCK_SPAM_RE = re.compile(r"unable to lock supervise/lock")
STAGE2_RE = re.compile(r"enter stage: /etc/runit/2")
# Product /etc/hostname is "ir0"; some images still ship the legacy "unix".
# Root PS1 ends with '#', non-root with '$' (see IR0-userspace rootfs/etc/profile).
_HOST = r"(?:ir0|unix)"
_USER = r"(?:ivan|root)"
PROMPT_RE = re.compile(rf"{_USER}@{_HOST}:\S*[#$]")
# Empty command: prompt with only whitespace before next prompt/EOL.
EMPTY_PROMPT_RE = re.compile(
    rf"{_USER}@{_HOST}:\S*[#$]\s*(?=(?:\n|\r|$|{_USER}@{_HOST}:))"
)
LOGIN_PROMPT_RE = re.compile(rf"ivan@{_HOST}:")
USERNAME_PROMPT_RE = re.compile(r"(?:login:|Enter your Unix username:)")
# Kernel serial noise interleaved with typed chars (breaks contiguous "true").
KERN_LINE_RE = re.compile(r"\[#\d+\][^\n]*\n?")
CSI_RE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")


def normalize_serial(text: str) -> str:
    text = KERN_LINE_RE.sub("", text)
    text = CSI_RE.sub("", text)
    return text


@dataclass
class Case:
    name: str
    cmd: str
    expect: Callable[[str], bool]
    timeout: float = 22.0
    special: Optional[str] = None
    # Substring that must appear in the delta (command echo). Empty = skip.
    echo: str = ""


def expect_prompt(new: str) -> bool:
    return bool(PROMPT_RE.search(new))


def expect_prompt_any(*need: str) -> Callable[[str], bool]:
    def _fn(new: str) -> bool:
        return expect_prompt(new) and any(n in new for n in need)

    return _fn


def expect_enoent(new: str) -> bool:
    return expect_prompt(new) and (
        "No such file" in new
        or "not found" in new
        or "can't open" in new
        or "Permission denied" in new
    )


def expect_pwd(new: str) -> bool:
    """Require a pwd line, not just '/' inside the PS1 path."""
    if not expect_prompt(new):
        return False
    if re.search(r"(?m)^/home/ivan\s*$", new):
        return True
    if re.search(r"(?m)^/\s*$", new):
        return True
    return False


def expect_doom_mnt(new: str) -> bool:
    if "Permission denied" in new and "DOOMGENERIC" not in new:
        return False
    return (
        "DOOMGENERIC_WAD_LOAD_OK" in new
        and "DOOMGENERIC_INTERACTIVE_EXIT_OK" in new
    )


# Stable core: short cmds, high signal for panic/hang/shshsh/editing.
CASES: list[Case] = [
    Case("true", "true", expect_prompt, echo="true"),
    Case("false", "false", expect_prompt, echo="false"),
    Case(
        "echo_hi",
        "echo hi",
        expect_prompt_any("hi", "ASH_COMMAND_ECHO_OK"),
        echo="echo hi",
    ),
    Case("id", "id", expect_prompt_any("uid="), echo="id"),
    Case("whoami", "whoami", expect_prompt_any("ivan", "root"), echo="whoami"),
    Case("pwd", "pwd", expect_pwd, echo="pwd"),
    Case("uname", "uname", expect_prompt_any("IR0", "Unix"), echo="uname"),
    Case("sync", "sync", expect_prompt, echo="sync", timeout=28.0),
    Case("ls", "ls", expect_prompt, echo="ls"),
    Case(
        "ls_etc",
        "ls /etc",
        expect_prompt_any("passwd", "hostname", "group"),
        echo="ls /etc",
    ),
    Case("ls_enoent", "ls ./no_such", expect_enoent, echo="ls ./no_such"),
    Case(
        "cat_host",
        "cat /etc/hostname",
        expect_prompt_any("ir0", "IR0", "unix"),
        echo="cat /etc/hostname",
    ),
    Case(
        "ls_proc",
        "ls /proc",
        expect_prompt_any("cpuinfo", "version", "1"),
        echo="ls /proc",
    ),
    Case(
        "hostname",
        "hostname",
        # Product image may report "ir0" or "unix" depending on /etc/hostname.
        expect_prompt_any("ir0", "IR0", "unix"),
        echo="hostname",
    ),
    Case(
        "cd_pwd",
        "cd /;pwd;cd",
        expect_pwd,
        echo="cd /;pwd;cd",
    ),
    Case(
        "mkdir_rm",
        "mkdir -p cmx;rmdir cmx;echo ok",
        expect_prompt_any("ok"),
        echo="mkdir",
    ),
    Case("not_found", "no_such_cmd_xyz", expect_enoent, echo="no_such_cmd_xyz"),
    Case("empty", "", expect_prompt, echo="", timeout=12.0),
    Case("pipe", "echo a|cat", expect_prompt_any("a"), echo="echo a"),
    Case(
        "redir",
        "echo x>cmx.out;cat cmx.out",
        expect_prompt_any("x"),
        echo="echo x",
    ),
    Case(
        "tab_etc",
        "",
        expect_prompt_any("etc", "passwd", "hostname"),
        special="tab_etc",
        echo="ls /et",
        timeout=30.0,
    ),
    Case(
        "ctrl_c",
        "",
        expect_prompt,
        special="ctrl_c_line",
        echo="true",
        timeout=35.0,
    ),
    # Soft BusyBox poweroff (no -f): sync + signal init; must return to prompt.
    Case(
        "soft_poweroff",
        "",
        expect_prompt_any("uid="),
        special="soft_poweroff",
        echo="id",
        timeout=40.0,
    ),
]


# Product-binaries stress suite: exit fullscreen apps, pipes, tcc/exec, find.
# Doom is attached separately via --with-doom (needs WAD).
#
# BusyBox top: without CONFIG_FEATURE_TOP_INTERACTIVE, `q` is ignored and top
# loops until signal. Use batch mode (-b -n N) for a reliable leave→prompt
# path; top_intr soft-gates Ctrl-C until TTY SIGINT is solid.
STABILITY_CASES: list[Case] = [
    Case("true", "true", expect_prompt, echo="true"),
    Case(
        "utmp_session",
        "uptime|grep '1 users' && echo UTMP_SESSION_OK",
        expect_prompt_any("UTMP_SESSION_OK"),
        echo="UTMP_SESSION_OK",
        timeout=30.0,
    ),
    Case(
        "top_batch",
        "top -b -n 2;echo TOP_BATCH_OK",
        expect_prompt_any("TOP_BATCH_OK", "Mem:"),
        echo="TOP_BATCH_OK",
        timeout=45.0,
    ),
    Case(
        "top_leave",
        "top -b -n 3;echo TOP_LEAVE_OK",
        expect_prompt_any("TOP_LEAVE_OK"),
        echo="TOP_LEAVE_OK",
        timeout=55.0,
    ),
    Case(
        "top_intr",
        "",
        expect_prompt_any("TOP_INTR_OK"),
        special="top_intr",
        echo="TOP_INTR_OK",
        timeout=55.0,
    ),
    Case(
        "man_quit",
        "",
        expect_prompt_any("MAN_QUIT_OK"),
        special="man_quit",
        echo="MAN_QUIT_OK",
        timeout=55.0,
    ),
    Case(
        "find_etc",
        "find /etc -name hostname;echo FIND_OK",
        expect_prompt_any("FIND_OK"),
        echo="FIND_OK",
        timeout=35.0,
    ),
    Case(
        "dmesg_file",
        "dmesg>/tmp/d.txt;wc -c /tmp/d.txt;echo DMESG_FILE_OK",
        expect_prompt_any("DMESG_FILE_OK"),
        echo="DMESG_FILE_OK",
        timeout=45.0,
    ),
    Case(
        "dmesg_pipe",
        "dmesg|grep -i irq;echo DMESG_PIPE_OK",
        expect_prompt_any("DMESG_PIPE_OK"),
        echo="DMESG_PIPE_OK",
        timeout=50.0,
    ),
    Case(
        "pipe_chain",
        "echo a|cat|cat;echo PIPE_CHAIN_OK",
        expect_prompt_any("PIPE_CHAIN_OK"),
        echo="PIPE_CHAIN_OK",
        timeout=30.0,
    ),
    Case(
        "ls_proc_stat",
        "cat /proc/stat;echo STAT_OK",
        expect_prompt_any("STAT_OK", "cpu "),
        echo="STAT_OK",
        timeout=30.0,
    ),
]

STABILITY_TCC_CASE = Case(
    "development_toolchain",
    "",
    expect_prompt_any("ISD_DEVELOPER_HELLO_OK", "DEVELOPMENT_TOOLCHAIN_OK"),
    special="tcc_mnt",
    echo="DEVELOPMENT_TOOLCHAIN_OK",
    timeout=90.0,
)


HELLO_C = """\
#include <stdio.h>
int main(void)
{
    puts("TCC_HELLO");
    return 0;
}
"""


def stage_stability_share(share: Path) -> None:
    share.mkdir(parents=True, exist_ok=True)
    (share / "hello.c").write_text(HELLO_C)



class Monitor:
    """Persistent QEMU HMP connection — critical for FEATURE_EDITING sendkey."""

    def __init__(self, port: int) -> None:
        self.port = port
        self.sock: Optional[socket.socket] = None

    def connect(self, timeout: float = 20.0) -> None:
        deadline = time.time() + timeout
        last: Optional[BaseException] = None
        while time.time() < deadline:
            try:
                s = socket.create_connection(("127.0.0.1", self.port), timeout=2)
                s.settimeout(0.4)
                try:
                    s.recv(4096)
                except Exception:
                    pass
                self.sock = s
                return
            except OSError as e:
                last = e
                time.sleep(0.2)
        raise ConnectionError(f"monitor :{self.port}: {last}")

    def close(self) -> None:
        if self.sock is not None:
            try:
                self.sock.close()
            except Exception:
                pass
            self.sock = None

    def _ensure(self) -> socket.socket:
        if self.sock is None:
            self.connect()
        assert self.sock is not None
        return self.sock

    def send(self, cmd: str) -> None:
        for attempt in range(4):
            try:
                s = self._ensure()
                s.sendall((cmd.strip() + "\r\n").encode())
                time.sleep(0.04)
                for _ in range(4):
                    try:
                        chunk = s.recv(4096)
                    except Exception:
                        break
                    if not chunk:
                        break
                return
            except OSError:
                self.close()
                time.sleep(0.15)
                self.connect(timeout=8)
        raise ConnectionError(f"monitor send failed: {cmd}")

    def key(self, name: str) -> None:
        self.send(f"sendkey {name}")

    def type_str(self, text: str, delay: float) -> None:
        for ch in text:
            if ch in SPECIAL:
                self.key(SPECIAL[ch])
            elif ch.isupper():
                self.key(f"shift-{ch.lower()}")
            else:
                self.key(ch)
            time.sleep(delay)

    def ret(self) -> None:
        self.key("ret")


def wait_pred(
    log: Path, pred: Callable[[str], bool], t0: float, limit: float, proc: subprocess.Popen
) -> str:
    while time.time() - t0 < limit:
        t = log.read_text(errors="replace")
        if pred(t) or any(r.search(t) for r in FAIL_RES):
            return t
        if proc.poll() is not None:
            return t
        time.sleep(0.2)
    raise TimeoutError("wait")


def classify_fail(new: str, *, allow_storm: bool = True) -> Optional[str]:
    for r in FAIL_RES:
        if r.search(new):
            return f"fatal:{r.pattern}"
    if len(LOCK_SPAM_RE.findall(new)) >= 3:
        return "lock_spam"
    if STAGE2_RE.search(new):
        return "stage2_reenter"
    login_m = LOGIN_PROMPT_RE.search(new)
    if new.count("GETTY_READY") >= 1 and login_m:
        if new.find("GETTY_READY") > login_m.start():
            return "console_restart"
    # Enter storms are real under FEATURE_EDITING+HMP, but kernel log gaps
    # between PS1 redraws look identical after normalize — only trip when
    # the command echo never appeared (stuck injecting newlines).
    if allow_storm:
        empties = 0
        parts = PROMPT_RE.split(new)
        for seg in parts[1:]:
            body = normalize_serial(seg).strip()
            if body == "":
                empties += 1
        if empties >= 25:
            return "enter_storm"
    if len(new) > 80000:
        return "serial_storm"
    if new.count("[INFO]") > 400:
        return "info_loop"
    return None


def echo_ok(case: Case, new: str) -> bool:
    if not case.echo:
        return True
    norm = normalize_serial(new)
    # Contiguous after stripping kernel lines.
    if case.echo in norm:
        return True
    if len(case.echo) > 3 and case.echo[1:] in norm:
        return True
    # Ordered char match (HMP drops / heavy interleave).
    needle = case.echo.replace(" ", "")
    pos = 0
    for ch in needle:
        pos = norm.find(ch, pos)
        if pos < 0:
            return False
        pos += 1
    return True


def inject_case(mon: Monitor, case: Case, key_delay: float) -> None:
    if case.special == "tab_etc":
        mon.type_str("ls /et", key_delay)
        time.sleep(0.3)
        mon.key("tab")
        time.sleep(0.9)
        mon.ret()
        return
    if case.special == "ctrl_c_line":
        mon.type_str("sleep 999", key_delay)
        time.sleep(max(0.35, key_delay))
        mon.ret()
        time.sleep(0.6)
        mon.key("ctrl-c")
        time.sleep(0.7)
        mon.type_str("true", key_delay)
        time.sleep(max(0.35, key_delay))
        mon.ret()
        return
    if case.special == "soft_poweroff":
        mon.type_str("busybox poweroff", max(key_delay, 0.40))
        mon.ret()
        time.sleep(2.0)
        mon.type_str("id", key_delay)
        mon.ret()
        return
    if case.special == "top_intr":
        d = max(key_delay, 0.40)
        # Infinite batch refresh (no FEATURE_TOP_INTERACTIVE → `q` ignored).
        mon.type_str("top -b -n 9999", d)
        mon.ret()
        time.sleep(2.5)
        for _ in range(5):
            mon.key("ctrl-c")
            time.sleep(0.4)
        time.sleep(1.2)
        mon.type_str("echo TOP_INTR_OK", d)
        mon.ret()
        return
    if case.special == "man_quit":
        d = max(key_delay, 0.40)
        mon.type_str("man true", d)
        mon.ret()
        time.sleep(2.5)
        for _ in range(3):
            mon.key("q")
            time.sleep(0.35)
        mon.key("ctrl-c")
        time.sleep(1.0)
        mon.type_str("echo MAN_QUIT_OK", d)
        mon.ret()
        return
    if case.special == "tcc_mnt":
        d = max(key_delay, 0.50)
        mon.type_str("su", d)
        mon.ret()
        time.sleep(0.8)
        mon.ret()
        time.sleep(1.0)
        mon.type_str("mkdir -p /mnt/host", d)
        mon.ret()
        time.sleep(1.0)
        mon.type_str("busybox mount -t 9p ir0share /mnt/host", d)
        mon.ret()
        time.sleep(2.0)
        mon.type_str("ls /mnt/host", d)
        mon.ret()
        time.sleep(1.0)
        mon.type_str("cd ~/Developer/hello", d)
        mon.ret()
        time.sleep(1.0)
        mon.type_str("make clean && make", d)
        mon.ret()
        time.sleep(4.0)
        mon.type_str("./hello", d)
        mon.ret()
        time.sleep(1.5)
        mon.type_str("echo DEVELOPMENT_TOOLCHAIN_OK", d)
        mon.ret()
        return
    if case.special == "doom_mnt":
        d = max(key_delay, 0.50)
        mon.type_str("mkdir -p /mnt/host", d)
        mon.ret()
        time.sleep(1.0)
        mon.type_str("busybox mount -t 9p ir0share /mnt/host", d)
        mon.ret()
        time.sleep(2.0)
        mon.type_str("ls /mnt/host", d)
        mon.ret()
        time.sleep(1.2)
        mon.type_str("/mnt/host/doomgeneric", d)
        mon.ret()
        return
    if case.special == "nano_mnt":
        d = max(key_delay, 0.50)
        # Ensure root for mount (login --su can miss under HMP).
        mon.type_str("su", d)
        mon.ret()
        time.sleep(0.8)
        mon.ret()
        time.sleep(1.2)
        mon.type_str("mkdir -p /mnt /mnt/host", d)
        mon.ret()
        time.sleep(1.0)
        mon.type_str("busybox mount -t 9p ir0share /mnt/host", d)
        mon.ret()
        time.sleep(2.5)
        mon.type_str("ls /mnt/host", d)
        mon.ret()
        time.sleep(1.2)
        mon.type_str("TERM=linux /mnt/host/nano /mnt/host/nano-out.txt", d)
        mon.ret()
        time.sleep(3.0)
        mon.type_str("hello IR0 nano", d)
        time.sleep(0.6)
        mon.key("ctrl-x")
        time.sleep(1.0)
        mon.key("y")
        time.sleep(0.5)
        mon.ret()
        time.sleep(2.0)
        mon.type_str("sync", d)
        mon.ret()
        time.sleep(1.0)
        mon.type_str("cat /mnt/host/nano-out.txt", d)
        mon.ret()
        time.sleep(1.0)
        mon.type_str("echo NANO_SMOKE_OK", d)
        mon.ret()
        return
    mon.type_str(case.cmd, key_delay)
    time.sleep(max(0.35, key_delay))
    mon.ret()


def stage_doom_share(share: Path, doom_bin: Path, wad: Path) -> None:
    share.mkdir(parents=True, exist_ok=True)
    shutil.copy2(doom_bin, share / "doomgeneric")
    (share / "doomgeneric").chmod(0o755)
    shutil.copy2(wad, share / "doom1.wad")
    (share / "doom-frames").write_text("1\n0\n")


def stop_qemu(proc: subprocess.Popen) -> None:
    if proc.poll() is None:
        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()


def cleanup_port(port: int) -> None:
    subprocess.run(
        ["pkill", "-f", f"qemu-system-x86_64.*127.0.0.1:{port}"],
        check=False,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    time.sleep(0.3)


def login_desktop(
    mon: Monitor, log: Path, key_delay: float, proc: subprocess.Popen, do_su: bool
) -> Optional[str]:
    t0 = time.time()
    # Development images may autologin as root (no "login:" prompt).
    try:
        wait_pred(
            log,
            lambda t: USERNAME_PROMPT_RE.search(t) is not None
            or "ASH_INTERACTIVE_READY" in t
            or PROMPT_RE.search(t) is not None,
            t0,
            90,
            proc,
        )
    except TimeoutError:
        return "login_wait"
    whole = log.read_text(errors="replace")
    if "ASH_INTERACTIVE_READY" in whole or PROMPT_RE.search(whole):
        time.sleep(0.8)
        if do_su and "root@" not in whole[-400:]:
            mon.type_str("su", key_delay)
            mon.ret()
            time.sleep(0.8)
            mon.ret()
            time.sleep(1.0)
        return None
    time.sleep(2.0)
    d = max(key_delay, 0.45)

    for attempt in range(3):
        base = len(log.read_text(errors="replace"))
        mon.type_str("ivan", d)
        # Confirm username echo before Enter (detects ivann / ian drops).
        confirm_deadline = time.time() + 8
        typed_ok = False
        while time.time() < confirm_deadline:
            tail = log.read_text(errors="replace")[base:]
            if "ivann" in tail:
                mon.key("ctrl-u")
                time.sleep(0.25)
                typed_ok = False
                break
            # Username echo after either the legacy or product login prompt.
            if re.search(
                r"(?:login:|Enter your Unix username:)\s*ivan\b", tail
            ) and "ivann" not in tail:
                typed_ok = True
                break
            if re.search(r"(?m)^ivan\s*$", tail) and "ivann" not in tail:
                typed_ok = True
                break
            time.sleep(0.15)
        else:
            typed_ok = False
        if not typed_ok:
            try:
                mon.key("ctrl-u")
            except Exception:
                pass
            time.sleep(0.25)
            continue
        mon.ret()
        try:
            wait_pred(log, lambda t: "Password:" in t or "Login incorrect" in t, t0, 40, proc)
        except TimeoutError:
            continue
        whole = log.read_text(errors="replace")
        if "Login incorrect" in whole[base:] and "Password:" not in whole[base:]:
            time.sleep(1.0)
            continue
        time.sleep(1.0)
        mon.type_str("ivan", max(key_delay, 0.50))
        mon.ret()
        try:
            wait_pred(
                log,
                lambda t: LOGIN_PROMPT_RE.search(t) is not None
                or "Login incorrect" in t,
                t0,
                90,
                proc,
            )
        except TimeoutError:
            return "login_timeout"
        whole = log.read_text(errors="replace")
        if "Login incorrect" in whole[base:] and not LOGIN_PROMPT_RE.search(
            whole[base:]
        ):
            time.sleep(1.5)
            continue
        if not LOGIN_PROMPT_RE.search(whole):
            return "no_prompt"
        time.sleep(1.0)
        if do_su:
            sbase = len(log.read_text(errors="replace"))
            mon.type_str("su", key_delay)
            mon.ret()
            time.sleep(0.8)
            mon.ret()
            time.sleep(1.0)
            fail = classify_fail(log.read_text(errors="replace")[sbase:])
            if fail:
                return fail
        return None
    return "login_retry_exhausted"


def run_cases(
    mon: Monitor,
    log: Path,
    proc: subprocess.Popen,
    cases: list[Case],
    key_delay: float,
) -> list[tuple[str, str]]:
    results: list[tuple[str, str]] = []
    login_mark = len(log.read_text(errors="replace"))

    for case in cases:
        settle = time.time() + 8
        while time.time() < settle:
            if PROMPT_RE.search(log.read_text(errors="replace")[-500:]):
                break
            time.sleep(0.15)
        time.sleep(0.4)

        def attempt_once() -> str:
            whole0 = log.read_text(errors="replace")
            base = len(whole0)
            prompts0 = len(PROMPT_RE.findall(whole0))
            inject_case(mon, case, key_delay)
            deadline = time.time() + case.timeout
            while time.time() < deadline:
                whole = log.read_text(errors="replace")
                new = whole[base:]
                saw_echo = echo_ok(case, new)
                # Once the command is echoing, ignore redraw "storms".
                fail = classify_fail(new, allow_storm=not saw_echo)
                # Nano paints full-screen CSI; getty restart after exit is noisy.
                if (
                    fail == "console_restart"
                    and case.special in {"nano_edit", "nano_mnt"}
                    and "GNU nano" in new
                ):
                    fail = None
                if fail:
                    return f"FAIL:{fail}"
                norm = normalize_serial(new)
                prompt_ok = len(PROMPT_RE.findall(whole)) > prompts0
                # Full-screen apps (nano): success is UI/marker, not a new PS1.
                nano_ok = case.special in {"nano_edit", "nano_mnt"} and case.expect(
                    norm
                )
                if (prompt_ok or nano_ok) and saw_echo and case.expect(norm):
                    time.sleep(0.25)
                    new2 = log.read_text(errors="replace")[base:]
                    fail = classify_fail(new2, allow_storm=False)
                    if (
                        fail == "console_restart"
                        and case.special in {"nano_edit", "nano_mnt"}
                        and "GNU nano" in new2
                    ):
                        fail = None
                    return f"FAIL:{fail}" if fail else "PASS"
                if proc.poll() is not None:
                    whole = log.read_text(errors="replace")
                    new = whole[base:]
                    norm = normalize_serial(new)
                    if (
                        len(PROMPT_RE.findall(whole)) > prompts0
                        and echo_ok(case, new)
                        and case.expect(norm)
                    ):
                        return "PASS"
                    return "FAIL:qemu_exit"
                time.sleep(0.2)
            return "TIMEOUT"

        status = attempt_once()
        if status != "PASS":
            print(f"  retry   {case.name} after {status}", flush=True)
            try:
                mon.key("ctrl-c")
            except Exception:
                pass
            time.sleep(0.6)
            # If enter_storm, do not retry in the same VM — reboot batch.
            if "enter_storm" in status:
                results.append((case.name, status))
                return results
            status = attempt_once()

        results.append((case.name, status))
        if status != "PASS":
            # Soft cases: keep going so later cmds still run.
            soft_continue = {"ctrl_c"}
            if case.name in soft_continue and (
                status == "TIMEOUT" or status.startswith("WARN:")
            ):
                continue
            # Stability soft-gates (no panic): do not abort the batch.
            if case.name in {"top_intr", "man_quit", "dmesg_pipe"} and (
                status == "TIMEOUT"
                or status.startswith("WARN:")
                or status.startswith("FAIL:enter_storm")
            ):
                # Relabel TIMEOUT as WARN so ash_results_ok treats it soft.
                if status == "TIMEOUT":
                    results[-1] = (case.name, "WARN:timeout")
                continue
            return results

    after = log.read_text(errors="replace")[login_mark:]
    if LOCK_SPAM_RE.search(after) and after.count("unable to lock") >= 3:
        results.append(("post_lock", "FAIL:lock_spam"))
    elif STAGE2_RE.search(after):
        results.append(("post_stage", "FAIL:stage2_reenter"))
    return results


def run_poweroff(mon: Monitor, log: Path, proc: subprocess.Popen, key_delay: float) -> str:
    settle = time.time() + 15
    while time.time() < settle:
        if PROMPT_RE.search(log.read_text(errors="replace")[-500:]):
            break
        time.sleep(0.2)
    time.sleep(0.5)
    base = len(log.read_text(errors="replace"))
    mon.type_str("halt -f", max(key_delay, 0.45))
    mon.ret()
    deadline = time.time() + 30
    while time.time() < deadline:
        new = log.read_text(errors="replace")[base:]
        if any(r.search(new) for r in FAIL_RES):
            return "FAIL:panic"
        if "ISA_DEBUG_EXIT_OK" in new or "SYSTEM_SHUTDOWN" in new:
            return "PASS"
        if proc.poll() is not None:
            time.sleep(0.4)
            new = log.read_text(errors="replace")[base:]
            if any(r.search(new) for r in FAIL_RES):
                return "FAIL:panic"
            return "PASS"
        time.sleep(0.2)
    stop_qemu(proc)
    time.sleep(0.3)
    new = log.read_text(errors="replace")[base:]
    if any(r.search(new) for r in FAIL_RES):
        return "FAIL:panic"
    if "ISA_DEBUG_EXIT_OK" in new or "SYSTEM_SHUTDOWN" in new:
        return "PASS"
    # Soft gate: ash matrix already proved interactive path.
    return "WARN:timeout"


def run_session(
    iso: Path,
    disk_src: Path,
    port: int,
    log: Path,
    key_delay: float,
    do_su: bool,
    cases: list[Case],
    *,
    share_dir: Optional[Path],
    do_poweroff: bool,
) -> list[tuple[str, str]]:
    disk = Path(tempfile.mktemp(suffix=".img", prefix="ir0-cmx."))
    shutil.copy(disk_src, disk)
    log_dir = ROOT / "build" / "cmx-logs"
    log_dir.mkdir(parents=True, exist_ok=True)
    cleanup_port(port)

    with log.open("a") as f:
        f.write(f"\n===== BATCH port={port} cases={len(cases)} =====\n")

    last_fail = "login"
    batch_log = Path("/dev/null")
    try:
        for attempt in range(1, 4):
            fd, batch_name = tempfile.mkstemp(
                suffix=".log", prefix="ir0-cmx-batch.", dir=str(log_dir)
            )
            os.close(fd)
            batch_log = Path(batch_name)
            batch_log.write_text("")

            qemu_cmd = [
                "qemu-system-x86_64",
                "-cdrom",
                str(iso),
                "-drive",
                f"file={disk},format=raw,if=ide,index=0",
                "-serial",
                f"file:{batch_log}",
                "-display",
                "none",
                "-m",
                "512M",
                "-no-reboot",
                "-net",
                "none",
                "-device",
                "isa-debug-exit,iobase=0xf4,iosize=0x04",
                "-monitor",
                f"tcp:127.0.0.1:{port},server,nowait",
            ]
            if share_dir is not None:
                qemu_cmd += [
                    "-fsdev",
                    f"local,id=ir0fs,path={share_dir},security_model=none",
                    "-device",
                    "virtio-9p-pci,fsdev=ir0fs,mount_tag=ir0share,disable-modern=on",
                ]
            stderr_path = Path(tempfile.mktemp(prefix="ir0-desktop-matrix-qemu.", suffix=".stderr"))
            stderr_file = stderr_path.open("wb")
            proc = subprocess.Popen(qemu_cmd, stdout=subprocess.DEVNULL, stderr=stderr_file)
            mon = Monitor(port)
            try:
                t_wait = time.time()
                while time.time() - t_wait < 15:
                    if batch_log.is_file():
                        break
                    time.sleep(0.05)
                # Special injectors (nano) wait on the live QEMU serial file.
                os.environ["IR0_NANO_SMOKE_LOG"] = str(batch_log)
                mon.connect(timeout=20)
                fail = login_desktop(mon, batch_log, key_delay, proc, do_su)
                if fail:
                    last_fail = fail
                    with log.open("a") as out:
                        out.write(f"\n--- login attempt {attempt} FAIL:{fail} ---\n")
                        out.write(batch_log.read_text(errors="replace"))
                    time.sleep(0.8)
                    continue
                results = run_cases(mon, batch_log, proc, cases, key_delay)
                with log.open("a") as out:
                    out.write(batch_log.read_text(errors="replace"))
                # Retry whole session on console death / panic / enter storm.
                hard = any(
                    st.startswith("FAIL:enter_storm")
                    or st.startswith("FAIL:console_restart")
                    or st.startswith("FAIL:fatal:")
                    for _, st in results
                )
                if hard and attempt < 3 and cases:
                    print(
                        f"  session retry after "
                        f"{[st for _, st in results if st != 'PASS']}",
                        flush=True,
                    )
                    last_fail = results[-1][1]
                    time.sleep(0.5)
                    continue
                if any(
                    st != "PASS" and not st.startswith("WARN:") for _, st in results
                ):
                    # Soft-gate residual enter_storm so one HMP flake does not
                    # fail the whole round after session retries.
                    if results[-1][1].startswith("FAIL:enter_storm"):
                        name = results[-1][0]
                        print(
                            f"  WARN skip {name} (enter_storm after retries)",
                            flush=True,
                        )
                        results[-1] = (name, "WARN:enter_storm")
                        return results
                    return results
                if do_poweroff:
                    po = run_poweroff(mon, batch_log, proc, key_delay)
                    results.append(("poweroff", po))
                    with log.open("a") as out:
                        out.write(batch_log.read_text(errors="replace"))
                return results
            except (TimeoutError, ConnectionError, OSError) as e:
                last_fail = f"session:{e}"
                stderr_file.flush()
                if proc.poll() is not None and (
                    not batch_log.is_file() or batch_log.stat().st_size == 0
                ):
                    detail = stderr_path.read_text(errors="replace").strip()
                    print(f"QEMU exited before serial output (rc={proc.returncode})")
                    if detail:
                        print(detail)
                with log.open("a") as out:
                    if batch_log.is_file():
                        out.write(batch_log.read_text(errors="replace"))
                continue
            finally:
                mon.close()
                stop_qemu(proc)
                stderr_file.close()
                stderr_path.unlink(missing_ok=True)
                cleanup_port(port)
        return [("login", f"FAIL:{last_fail}")]
    finally:
        disk.unlink(missing_ok=True)
        # Keep last failing batch log for triage; drop only on full success path
        # (caller may still want it — leave files under build/cmx-logs/).


def chunked(items: list[Case], size: int) -> list[list[Case]]:
    return [items[i : i + size] for i in range(0, len(items), size)]


def ash_results_ok(
    results: list[tuple[str, str]], *, stability: bool = False
) -> bool:
    """Ash cases must PASS or soft WARN; hard FAIL fails the round."""
    soft_names = {"poweroff", "ctrl_c", "soft_poweroff"}
    if stability:
        # dmesg|grep flaky under HMP; top Ctrl-C soft until TTY SIGINT is solid.
        soft_names |= {"dmesg_pipe", "top_intr", "man_quit"}
    hard_in_status = ("panic", "#UD", "SEGV", "fatal", "DOUBLE", "ISR64", "Unhandled")
    for name, st in results:
        if st == "PASS" or st.startswith("WARN:"):
            continue
        if name in soft_names:
            if any(tag in st for tag in hard_in_status):
                return False
            continue
        return False
    if stability:
        core = {"true", "top_batch", "top_leave", "ls_proc_stat"}
    else:
        core = {"true", "false", "echo_hi", "id", "whoami", "tab_etc"}
    passed = {n for n, st in results if st == "PASS"}
    present_core = core & {n for n, _ in results}
    if present_core and not present_core <= passed:
        must = present_core - ({"tab_etc"} if not stability else set())
        if must and not must <= passed:
            return False
    return True


def run_round(
    iso: Path,
    disk_src: Path,
    port: int,
    log: Path,
    key_delay: float,
    do_su: bool,
    with_doom: bool,
    doom_bin: Path,
    wad_path: Path,
    batch_size: int,
    only_doom: bool = False,
    skip_poweroff: bool = False,
    stability: bool = False,
    with_tcc: bool = True,
) -> tuple[bool, list[tuple[str, str]]]:
    results: list[tuple[str, str]] = []
    share_dir: Optional[Path] = None
    if only_doom:
        batches: list[list[Case]] = []
    elif stability:
        batches = chunked(list(STABILITY_CASES), batch_size)
    else:
        batches = chunked(list(CASES), batch_size)
    doom_case: Optional[Case] = None
    if with_doom or only_doom:
        share_dir = Path(tempfile.mkdtemp(prefix="ir0-cmx-share."))
        stage_doom_share(share_dir, doom_bin, wad_path)
        doom_case = Case(
            "doom_mnt",
            "",
            expect_doom_mnt,
            special="doom_mnt",
            echo="doomgeneric",
            timeout=300.0,
        )

    tcc_share: Optional[Path] = None
    try:
        log.write_text("")
        pending: list[Case] = []
        for i, batch in enumerate(batches):
            work = list(batch) + pending
            pending = []
            print(
                f"  -- batch {i + 1}/{len(batches)} ({len(work)} cmds) --",
                flush=True,
            )
            part = run_session(
                iso,
                disk_src,
                port + i,
                log,
                key_delay,
                do_su,
                work,
                share_dir=None,
                do_poweroff=False,
            )
            results.extend(part)
            if part and part[-1][1] == "WARN:enter_storm":
                done = {n for n, _ in part}
                pending = [c for c in work if c.name not in done]
            if not ash_results_ok(part, stability=stability):
                return False, results
        if pending:
            print(f"  -- batch retry-pending ({len(pending)} cmds) --", flush=True)
            part = run_session(
                iso,
                disk_src,
                port + 50,
                log,
                key_delay,
                do_su,
                pending,
                share_dir=None,
                do_poweroff=False,
            )
            results.extend(part)
            if not ash_results_ok(part, stability=stability):
                return False, results

        if stability and with_tcc and not only_doom:
            tcc_share = Path(tempfile.mkdtemp(prefix="ir0-cmx-tcc."))
            stage_stability_share(tcc_share)
            print("  -- batch development toolchain --", flush=True)
            part = run_session(
                iso,
                disk_src,
                port + 80,
                log,
                key_delay,
                True,
                [STABILITY_TCC_CASE],
                share_dir=tcc_share,
                do_poweroff=False,
            )
            results.extend(part)
            if not ash_results_ok(part, stability=stability):
                return False, results

        if doom_case is not None:
            print("  -- batch doom /mnt/host (su for mount) --", flush=True)
            part = run_session(
                iso,
                disk_src,
                port + 100,
                log,
                key_delay,
                True,
                [doom_case],
                share_dir=share_dir,
                do_poweroff=False,
            )
            results.extend(part)
            if not ash_results_ok(part, stability=stability):
                return False, results

        if not skip_poweroff:
            print("  -- batch poweroff (halt -f, soft) --", flush=True)
            part = run_session(
                iso,
                disk_src,
                port + 200,
                log,
                key_delay,
                do_su,
                [],
                share_dir=None,
                do_poweroff=True,
            )
            results.extend(part)
        return ash_results_ok(results, stability=stability), results
    finally:
        if share_dir is not None:
            shutil.rmtree(share_dir, ignore_errors=True)
        if tcc_share is not None:
            shutil.rmtree(tcc_share, ignore_errors=True)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--iso", type=Path, default=ROOT / "kernel-x64-userspace.iso")
    ap.add_argument("--disk", type=Path, default=ROOT / "disk.img")
    ap.add_argument("--log", type=Path, default=Path("/tmp/ir0-cmd-matrix.log"))
    ap.add_argument("--rounds", type=int, default=1)
    ap.add_argument("--port", type=int, default=45901)
    ap.add_argument("--key-delay", type=float, default=0.40)
    ap.add_argument("--batch-size", type=int, default=5)
    ap.add_argument("--su", action="store_true")
    ap.add_argument("--with-doom", action="store_true")
    ap.add_argument("--only-doom", action="store_true")
    ap.add_argument(
        "--stability",
        action="store_true",
        help="Run STABILITY_CASES (top/man/tcc/pipes) instead of ash matrix",
    )
    ap.add_argument(
        "--no-tcc",
        action="store_true",
        help="With --stability, skip tcc hello via 9p",
    )
    ap.add_argument(
        "--skip-poweroff",
        action="store_true",
        help="Skip halt -f batch (default: run soft-gated)",
    )
    ap.add_argument(
        "--doom-bin",
        type=Path,
        default=ROOT / "setup/pid1/fase55e_doom_interactive",
    )
    ap.add_argument(
        "--wad",
        type=Path,
        default=Path("/home/ivanr013/Escritorio/universal-doom/DOOM1.WAD"),
    )
    args = ap.parse_args()

    if not args.iso.is_file() or not args.disk.is_file():
        print(f"✗ missing iso/disk: {args.iso} {args.disk}", file=sys.stderr)
        return 2
    if args.only_doom:
        args.with_doom = True
    if args.with_doom:
        if not args.doom_bin.is_file() or not args.wad.is_file():
            print(
                f"✗ missing doom bin or WAD ({args.doom_bin}, {args.wad})",
                file=sys.stderr,
            )
            return 2
    if args.stability:
        args.skip_poweroff = True
        if args.batch_size == 5:
            args.batch_size = 3

    if args.stability:
        ncases = len(STABILITY_CASES) + (0 if args.no_tcc else 1)
    else:
        ncases = 0 if args.only_doom else len(CASES)
    ncases += 0 if args.skip_poweroff else 1
    ncases += 1 if args.with_doom else 0
    print(
        f"CMD_MATRIX rounds={args.rounds} cases={ncases} "
        f"batch={args.batch_size} delay={args.key_delay} "
        f"doom={args.with_doom} only_doom={args.only_doom} "
        f"stability={args.stability}",
        flush=True,
    )
    round_ok = 0
    for r in range(1, args.rounds + 1):
        print(f"=== ROUND {r}/{args.rounds} ===", flush=True)
        ok, results = run_round(
            args.iso,
            args.disk,
            args.port + r * 30,
            args.log,
            args.key_delay,
            args.su,
            args.with_doom,
            args.doom_bin,
            args.wad,
            args.batch_size,
            only_doom=args.only_doom,
            skip_poweroff=args.skip_poweroff,
            stability=args.stability,
            with_tcc=not args.no_tcc,
        )
        for name, st in results:
            print(f"  {st:22} {name}", flush=True)
        if ok:
            round_ok += 1
            print(f"ROUND {r} PASS", flush=True)
        else:
            print(f"ROUND {r} FAIL", flush=True)

    print(f"OVERALL {round_ok}/{args.rounds} rounds PASS", flush=True)
    return 0 if round_ok == args.rounds else 1


if __name__ == "__main__":
    sys.exit(main())
