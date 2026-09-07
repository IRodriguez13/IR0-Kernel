#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Run a static ABI probe on Linux (strace + audit parse).

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CONTRACT="${1:?contract}"
PROBE_BASENAME="${2:?probe_basename e.g. poll_probe}"
STRACE_SYSCALLS="${3:-all}"
OUT="${4:-$ROOT/build/linux_abi_audit/linux/$CONTRACT}"
OUT="$(mkdir -p "$OUT" && cd "$OUT" && pwd)"
PROBE="$ROOT/build/linux_abi_audit/$PROBE_BASENAME"

mkdir -p "$OUT"

if [[ ! -x "$PROBE" ]]; then
	echo "run_linux_workload.sh: missing $PROBE" >&2
	exit 1
fi

STDOUT="$OUT/stdout.log"
STRACE="$OUT/strace.log"

: > "$STDOUT"
: > "$STRACE"

echo "  LINUX_ABI  strace $PROBE_BASENAME (Linux ground truth)"
(
	cd "$OUT"
	if [[ "$CONTRACT" == "ioctl" ]]; then
		python3 "$ROOT/scripts/linux_abi/run_under_pty.py" -- \
			strace -f -o "$STRACE" -e "trace=$STRACE_SYSCALLS" -s 128 "$PROBE"
	else
		strace -f -o "$STRACE" -e "trace=$STRACE_SYSCALLS" -s 128 "$PROBE"
	fi
) >"$STDOUT" 2>&1 || true

python3 "$ROOT/scripts/linux_abi/parse_simple_trace.py" \
	"$CONTRACT" linux "$STDOUT" "$OUT/trace.json"

echo "✓ Linux $CONTRACT workload -> $OUT"
