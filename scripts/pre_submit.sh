#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
#
# pre_submit.sh — Local contributor gate (does not publish).
#
#   make pre-submit
#   make pre-submit SUBSYSTEM=mm
#
# Prints a PRE_SUBMIT_OK / PRE_SUBMIT_FAIL summary. No network, no push.
#

set -eu

SUBSYSTEM="${1:-all}"
KERNEL_ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
cd "$KERNEL_ROOT"

TESTS_PASSED=0
SMOKES_PASSED=0
FMT_STATUS="OK"
FAIL=0

echo "=========================================="
echo "IR0 pre-submit  SUBSYSTEM=${SUBSYSTEM}"
echo "=========================================="

run_host_tests() {
	echo ""
	echo "[pre-submit] host tests (tests/host)"
	_out=$(mktemp)
	if make -s -C tests/host run >"$_out" 2>&1; then
		# Prefer explicit pass counts if the suite prints them
		_n=$(grep -Eic 'PASS|ok |OK' "$_out" || true)
		if [ -z "$_n" ] || [ "$_n" -eq 0 ]; then
			TESTS_PASSED=1
		else
			TESTS_PASSED=$_n
		fi
		echo "  host tests: OK"
		rm -f "$_out"
		return 0
	fi
	echo "  host tests: FAIL"
	tail -n 40 "$_out" || true
	rm -f "$_out"
	FAIL=1
	return 1
}

run_smoke() {
	_target=$1
	echo ""
	echo "[pre-submit] smoke: make ${_target}"
	if make -s "$_target"; then
		SMOKES_PASSED=$((SMOKES_PASSED + 1))
		echo "  ${_target}: OK"
		return 0
	fi
	echo "  ${_target}: FAIL"
	FAIL=1
	return 1
}

# Patch formatting: whitespace / conflict markers in the working tree diff
check_fmt() {
	echo ""
	echo "[pre-submit] patch formatting"
	if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
		FMT_STATUS="SKIP (not a git checkout)"
		echo "  ${FMT_STATUS}"
		return 0
	fi
	if git diff --check >/dev/null 2>&1 && git diff --cached --check >/dev/null 2>&1; then
		FMT_STATUS="OK"
		echo "  git diff --check: OK"
		return 0
	fi
	FMT_STATUS="FAIL"
	echo "  git diff --check: FAIL (trailing whitespace / conflict markers)"
	git diff --check || true
	git diff --cached --check || true
	FAIL=1
	return 1
}

echo ""
echo "[pre-submit] build + arch-guard"
if ! make -s kernel-x64.bin; then
	echo "  kernel-x64.bin: FAIL"
	FAIL=1
else
	echo "  kernel-x64.bin: OK"
	SMOKES_PASSED=$((SMOKES_PASSED + 1))
fi

if ! make -s arch-guard; then
	echo "  arch-guard: FAIL"
	FAIL=1
else
	echo "  arch-guard: OK"
	SMOKES_PASSED=$((SMOKES_PASSED + 1))
fi

if ! make -s repo-hygiene-guard; then
	echo "  repo-hygiene-guard: FAIL"
	FAIL=1
else
	echo "  repo-hygiene-guard: OK"
	SMOKES_PASSED=$((SMOKES_PASSED + 1))
fi

run_host_tests || true
check_fmt || true

case "$SUBSYSTEM" in
all|generic|"")
	;;
mm|memory)
	run_smoke smoke-mm-cow-lazy || true
	;;
net|network)
	run_smoke smoke-stream-sock || true
	;;
vfs|fs)
	# Keep gate light: arch-guard + host already cover facades
	;;
sched|scheduler)
	;;
arm|arm64|hub)
	run_smoke smoke-arm64 || true
	;;
*)
	echo ""
	echo "[pre-submit] unknown SUBSYSTEM=${SUBSYSTEM} — ran default battery only"
	echo "  Known: all | mm | net | vfs | sched | arm64"
	;;
esac

echo ""
echo "=========================================="
if [ "$FAIL" -eq 0 ]; then
	echo "PRE_SUBMIT_OK"
else
	echo "PRE_SUBMIT_FAIL"
fi
echo "Subsystem: ${SUBSYSTEM}"
echo "Tests: ${TESTS_PASSED} passed"
echo "Smoke gates: ${SMOKES_PASSED} passed"
echo "Patch formatting: ${FMT_STATUS}"
echo "=========================================="
echo "Does not commit or push. See CONTRIBUTING.md."

if [ "$FAIL" -ne 0 ]; then
	exit 1
fi
exit 0
