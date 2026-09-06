#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Interactive test suite picker (curses TUI) — make test."""

from __future__ import annotations

import curses
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / "scripts" / "test_runner.py"


def load_rows() -> list[dict]:
    proc = subprocess.run(
        [sys.executable, str(RUNNER), "--list"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr or "test_runner --list failed")
    rows: list[dict] = []
    for line in proc.stdout.splitlines():
        line = line.strip()
        if not line or line[0] not in "* ":
            continue
        default = line[0] == "*"
        rest = line[2:]
        sid, _, tail = rest.partition(" ")
        grp = ""
        label = tail
        if tail.startswith("["):
            g_end = tail.find("]")
            if g_end > 1:
                grp = tail[1:g_end].strip()
                label = tail[g_end + 1 :].strip()
        rows.append({"id": sid, "group": grp, "label": label, "default": default, "on": default})
    return rows


def draw(stdscr, rows: list[dict], idx: int, persist: bool, status: str) -> None:
    stdscr.erase()
    h, w = stdscr.getmaxyx()
    title = " IR0 Test Menu (KTM-style) "
    stdscr.addstr(0, max(0, (w - len(title)) // 2), title, curses.A_BOLD)
    stdscr.addstr(1, 0, "Space toggle  A all/none  D defaults  R run  P persist  Q quit")
    stdscr.addstr(2, 0, f"Persist disk: {'ON' if persist else 'off'}  {status}"[: max(0, w - 1)])

    y = 4
    last_grp = None
    for i, row in enumerate(rows):
        if y >= h - 2:
            break
        if row["group"] != last_grp:
            if y > 4:
                y += 1
            grp_title = row["group"] or "other"
            if y < h - 2:
                stdscr.addstr(y, 0, f"[{grp_title}]", curses.A_UNDERLINE)
                y += 1
            last_grp = row["group"]
        mark = "[x]" if row["on"] else "[ ]"
        line = f"{mark} {row['id']:16} {row['label']}"
        attr = curses.A_REVERSE if i == idx else curses.A_NORMAL
        if y < h - 2:
            stdscr.addstr(y, 0, line[: max(0, w - 1)], attr)
            y += 1

    stdscr.refresh()


def run_selected(rows: list[dict], persist: bool) -> tuple[int, str]:
    ids = [r["id"] for r in rows if r["on"]]
    if not ids:
        return 2, "nothing selected"
    cmd = [sys.executable, str(RUNNER), "--suites", ",".join(ids)]
    if persist:
        cmd.append("--persist")
    proc = subprocess.run(cmd, cwd=ROOT)
    if proc.returncode == 0:
        return 0, "OK"
    return proc.returncode, f"exit {proc.returncode}"


def main_curses(stdscr) -> int:
    curses.curs_set(0)
    stdscr.keypad(True)
    rows = load_rows()
    if not rows:
        stdscr.addstr(0, 0, "empty catalog")
        stdscr.refresh()
        stdscr.getch()
        return 2

    idx = 0
    persist = False
    status = ""

    while True:
        draw(stdscr, rows, idx, persist, status)
        ch = stdscr.getch()
        if ch in (ord("q"), ord("Q"), 27):
            return 0
        if ch in (curses.KEY_UP, ord("k")):
            idx = (idx - 1) % len(rows)
        elif ch in (curses.KEY_DOWN, ord("j")):
            idx = (idx + 1) % len(rows)
        elif ch == ord(" "):
            rows[idx]["on"] = not rows[idx]["on"]
        elif ch in (ord("a"), ord("A")):
            val = not all(r["on"] for r in rows)
            for r in rows:
                r["on"] = val
        elif ch in (ord("d"), ord("D")):
            for r in rows:
                r["on"] = r["default"]
        elif ch in (ord("p"), ord("P")):
            persist = not persist
        elif ch in (ord("r"), ord("R")):
            status = "running..."
            draw(stdscr, rows, idx, persist, status)
            rc, msg = run_selected(rows, persist)
            status = msg
            if rc != 0:
                stdscr.addstr(curses.LINES - 1, 0, f"Failed ({msg}). Press any key.")
                stdscr.refresh()
                stdscr.getch()


def main() -> int:
    if not sys.stdin.isatty():
        sys.stderr.write("test_tui requires a terminal; use: make test-run SUITE=host,ktest\n")
        return 2
    try:
        return curses.wrapper(main_curses)
    except RuntimeError as exc:
        sys.stderr.write(f"{exc}\n")
        return 2


if __name__ == "__main__":
    sys.exit(main())
