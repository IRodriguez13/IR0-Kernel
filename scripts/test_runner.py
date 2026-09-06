#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run IR0 test suites from scripts/test_catalog.yaml via make."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

try:
    import yaml
except ImportError:
    yaml = None

ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / "scripts" / "test_catalog.yaml"

GROUP_LABELS = {
    "ctr": "CTR / fast",
    "ktm": "KTM",
    "ktm-userdev": "KTM userdev",
    "net": "Net (F8)",
    "storage": "Storage / MM",
    "tier1": "Tier-1 userspace",
    "release": "Release gates",
    "abi": "ABI audit",
    "session": "ISD session",
}


def load_catalog(path: Path) -> list[dict]:
    if yaml is None:
        sys.stderr.write("PyYAML required: pip install pyyaml\n")
        sys.exit(2)
    data = yaml.safe_load(path.read_text(encoding="utf-8"))
    suites = data.get("suites") or []
    if not isinstance(suites, list):
        sys.stderr.write(f"invalid catalog: {path}\n")
        sys.exit(2)
    return suites


def suite_by_id(suites: list[dict]) -> dict[str, dict]:
    out: dict[str, dict] = {}
    for s in suites:
        sid = s.get("id")
        if not sid:
            continue
        if sid in out:
            sys.stderr.write(f"duplicate suite id: {sid}\n")
            sys.exit(2)
        out[sid] = s
    return out


def list_suites(suites: list[dict], group: str | None = None) -> None:
    for s in suites:
        if group and s.get("group") != group:
            continue
        mark = "*" if s.get("default") else " "
        grp = s.get("group", "?")
        print(f"{mark} {s['id']:16} [{grp:8}] {s.get('label', s.get('make', ''))}")


def resolve_ids(suites: list[dict], raw: str) -> list[str]:
    by_id = suite_by_id(suites)
    ids: list[str] = []
    for part in raw.split(","):
        part = part.strip()
        if not part:
            continue
        if part in by_id:
            ids.append(part)
            continue
        matched = [s["id"] for s in suites if s.get("group") == part]
        if matched:
            ids.extend(matched)
            continue
        sys.stderr.write(f"unknown suite or group: {part}\n")
        sys.exit(2)
    seen: set[str] = set()
    ordered: list[str] = []
    for sid in ids:
        if sid not in seen:
            seen.add(sid)
            ordered.append(sid)
    return ordered


def run_suite(suite: dict, *, persist: bool, dry_run: bool) -> int:
    target = suite.get("make")
    if not target:
        sys.stderr.write(f"suite {suite.get('id')} missing make target\n")
        return 2
    label = suite.get("label", target)
    print(f"\n=== {suite.get('id')}: {label} ===")
    env = os.environ.copy()
    if persist:
        env["IR0_DEV_PERSIST"] = "1"
    cmd = ["make", "-s", target]
    if dry_run:
        print(" ".join(cmd))
        return 0
    proc = subprocess.run(cmd, cwd=ROOT, env=env)
    return proc.returncode


def main() -> int:
    ap = argparse.ArgumentParser(description="Run IR0 test suites from catalog")
    ap.add_argument("--catalog", type=Path, default=CATALOG)
    ap.add_argument("--list", action="store_true", help="List suites (* = TUI default)")
    ap.add_argument("--group", help="Filter --list by group id")
    ap.add_argument("--suites", help="Comma-separated suite ids or group ids")
    ap.add_argument("--default", action="store_true", help="Run catalog default suites")
    ap.add_argument("--persist", action="store_true", help="Set IR0_DEV_PERSIST=1")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    if not args.catalog.is_file():
        sys.stderr.write(f"catalog not found: {args.catalog}\n")
        return 2

    suites = load_catalog(args.catalog)

    if args.list:
        list_suites(suites, args.group)
        return 0

    if args.default:
        ids = [s["id"] for s in suites if s.get("default")]
    elif args.suites:
        ids = resolve_ids(suites, args.suites)
    else:
        ap.print_help()
        return 2

    if not ids:
        sys.stderr.write("no suites selected\n")
        return 2

    by_id = suite_by_id(suites)
    failed: list[str] = []
    for sid in ids:
        rc = run_suite(by_id[sid], persist=args.persist, dry_run=args.dry_run)
        if rc != 0:
            failed.append(sid)

    if failed:
        print(f"\n✗ failed: {', '.join(failed)}", file=sys.stderr)
        return 1
    print("\n✓ all selected suites passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
