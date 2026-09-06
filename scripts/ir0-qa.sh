#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# QA / smoke / test / CI gates — targets live in scripts/make/testing.mk (included by default).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

show_help() {
	cat <<'EOF'
IR0 QA / smoke / CI — scripts/ir0-qa.sh

Usage:
  scripts/ir0-qa.sh <make-target> [make-args...]
  IR0_LEGACY_SMOKE=1 scripts/ir0-qa.sh smoke-fase50-busybox

Interactive testing:
  make test                  TUI suite picker (scripts/test_catalog.yaml)
  make test-list             List suites
  make test-run SUITE=host,ktest

Common gates:
  make ctr                   kernel + arch-guard + matrix-min + tests/host
  make test-fast             arch-guard + tests/host only
  make kernel-tests          in-kernel ktest suite (QEMU)
  make smoke-tier1           runit boot + ash interactive
  make release-0.0.1         release gate
  make health                analyze + memsafe + kernel-tests

Extended GUI / legacy (IR0_LEGACY_SMOKE=1):
  run-fase58e-ash-gui          runit + BusyBox ash (GTK)
  run-fase55d-doomgeneric-gui  Doom interactive (set REAL_WAD_PATH=...)

List all make targets:
  scripts/ir0-qa.sh targets

Kernel build/run: make help
EOF
}

if [[ $# -eq 0 ]] || [[ "${1:-}" == "help" ]] || [[ "${1:-}" == "-h" ]] || [[ "${1:-}" == "--help" ]]; then
	show_help
	exit 0
fi

if [[ "${1:-}" == "targets" ]]; then
	make -pR 2>/dev/null \
		| awk -F: '/^[a-zA-Z0-9_.-]+:/ {print $1}' \
		| sort -u \
		| grep -Ev '^(Makefile|\.|%)' || true
	exit 0
fi

make "$@"
