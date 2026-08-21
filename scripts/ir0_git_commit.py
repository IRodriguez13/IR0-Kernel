#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
Create or rewrite a git commit with IR0 maintainer identity only.

Cloud / IDE hooks often set Author to an agent and append
Co-authored-by: <maintainer>. This helper uses git commit-tree so those
trailers never land on published history.

Usage:
  python3 scripts/ir0_git_commit.py -m "subject\\n\\nbody"
  python3 scripts/ir0_git_commit.py --amend-head
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MAINTAINER_NAME = "Iván Ezequiel Rodriguez"
MAINTAINER_EMAIL = "ivanrwcm25@gmail.com"
SOB = f"Signed-off-by: {MAINTAINER_NAME} <{MAINTAINER_EMAIL}>"


def run(args: list[str], *, input_text: str | None = None) -> str:
    proc = subprocess.run(
        args,
        cwd=ROOT,
        text=True,
        input=input_text,
        capture_output=True,
        check=False,
        env=commit_env(),
    )
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr or proc.stdout or "command failed\n")
        raise SystemExit(proc.returncode)
    return proc.stdout


def commit_env() -> dict[str, str]:
    env = os.environ.copy()
    env["GIT_AUTHOR_NAME"] = MAINTAINER_NAME
    env["GIT_AUTHOR_EMAIL"] = MAINTAINER_EMAIL
    env["GIT_COMMITTER_NAME"] = MAINTAINER_NAME
    env["GIT_COMMITTER_EMAIL"] = MAINTAINER_EMAIL
    return env


def sanitize_message(msg: str) -> str:
    lines = []
    for line in msg.replace("\r\n", "\n").split("\n"):
        if line.lower().startswith("co-authored-by:"):
            continue
        if line.lower().startswith("helped-by:"):
            continue
        lines.append(line.rstrip())
    while lines and lines[-1] == "":
        lines.pop()
    text = "\n".join(lines).rstrip() + "\n"
    if SOB not in text:
        text = text.rstrip() + "\n\n" + SOB + "\n"
    return text


def write_commit(tree: str, parents: list[str], message: str) -> str:
    cmd = ["git", "commit-tree", tree]
    for parent in parents:
        cmd.extend(["-p", parent])
    sha = run(cmd, input_text=sanitize_message(message)).strip()
    if not sha:
        raise SystemExit("git commit-tree produced no sha")
    return sha


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-m", "--message", default="", help="Commit message (subject + body)")
    ap.add_argument(
        "--amend-head",
        action="store_true",
        help="Rebuild HEAD with the same tree/parent, sanitizing identity and trailers",
    )
    args = ap.parse_args()

    if args.amend_head:
        tree = run(["git", "write-tree"]).strip()
        parents = run(["git", "rev-parse", "HEAD^@"]).split()
        message = args.message if args.message.strip() else run(
            ["git", "log", "-1", "--format=%B", "HEAD"]
        )
        sha = write_commit(tree, parents, message)
        run(["git", "reset", "--soft", sha])
        print(sha)
        return 0

    if not args.message.strip():
        ap.error("provide -m or --amend-head")

    status = run(["git", "diff", "--cached", "--name-only"]).strip()
    if not status:
        print("nothing staged", file=sys.stderr)
        return 1

    tree = run(["git", "write-tree"]).strip()
    parent = run(["git", "rev-parse", "HEAD"]).strip()
    sha = write_commit(tree, [parent], args.message)
    run(["git", "reset", "--soft", sha])
    print(sha)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
