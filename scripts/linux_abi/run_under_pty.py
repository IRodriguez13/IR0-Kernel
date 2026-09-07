#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run a command with a deterministic controlling pseudo-terminal."""

from __future__ import annotations

import argparse
import fcntl
import os
import struct
import sys
import termios


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command
    if command and command[0] == "--":
        command = command[1:]
    if not command:
        parser.error("a command is required after --")

    pid, master_fd = os.forkpty()
    if pid == 0:
        winsize = struct.pack("HHHH", 24, 80, 0, 0)
        fcntl.ioctl(0, termios.TIOCSWINSZ, winsize)
        os.execvp(command[0], command)

    try:
        while True:
            try:
                data = os.read(master_fd, 4096)
            except OSError as exc:
                # Linux PTY masters report EIO after the slave is closed.
                if exc.errno == 5:
                    break
                raise
            if not data:
                break
            sys.stdout.buffer.write(data)
            sys.stdout.buffer.flush()
    finally:
        os.close(master_fd)

    _, status = os.waitpid(pid, 0)
    return os.waitstatus_to_exitcode(status)


if __name__ == "__main__":
    raise SystemExit(main())
