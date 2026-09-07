#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
#
# Backward-compatible wrapper around scripts/smoke_autokill.py.
# Maps legacy --done TAG to --success TAG; forwards exit code (PASS=0, FAIL!=0).

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PYTHON="${SMOKE_AUTOKILL_PYTHON:-python3}"
AUTOKILL="$ROOT/scripts/smoke_autokill.py"

log_file=""
timeout_sec=""
stale_sec=""
profile=""
success_mode="all"
success_tags=()
fail_patterns=()
expected_panic=0
qemu_args=()

usage()
{
	echo "usage: $0 --log FILE [--timeout SEC] [--stale-sec SEC] [--profile NAME]" \
		"[--success-mode all|any] [--done TAG]... [--fail-regex RE]... -- QEMU_ARGS..." >&2
	exit 2
}

while [[ $# -gt 0 ]]; do
	case "$1" in
	--log)
		log_file="${2:-}"
		shift 2
		;;
	--timeout)
		timeout_sec="${2:-}"
		shift 2
		;;
	--stale-sec)
		stale_sec="${2:-}"
		shift 2
		;;
	--profile)
		profile="${2:-}"
		shift 2
		;;
	--success-mode)
		success_mode="${2:-}"
		shift 2
		;;
	--done)
		success_tags+=("${2:-}")
		shift 2
		;;
	--fail-regex)
		fail_patterns+=("${2:-}")
		shift 2
		;;
	--expected-panic)
		expected_panic=1
		shift
		;;
	--)
		shift
		qemu_args=("$@")
		break
		;;
	-h|--help)
		usage
		;;
	*)
		echo "$0: unknown option: $1" >&2
		usage
		;;
	esac
done

if [[ -z "$log_file" ]] || [[ ${#qemu_args[@]} -eq 0 ]]; then
	usage
fi

cmd=("$PYTHON" "$AUTOKILL" "--log" "$log_file" "--success-mode" "$success_mode")

if [[ -n "$timeout_sec" ]]; then
	cmd+=("--timeout" "$timeout_sec")
fi
if [[ -n "$stale_sec" ]]; then
	cmd+=("--stale-sec" "$stale_sec")
fi
if [[ -n "$profile" ]]; then
	cmd+=("--profile" "$profile")
fi
if [[ "$expected_panic" -eq 1 ]]; then
	cmd+=("--expected-panic")
fi

for tag in "${success_tags[@]}"; do
	cmd+=("--success" "$tag")
done
for pat in "${fail_patterns[@]}"; do
	cmd+=("--fail" "$pat")
done

cmd+=("--" "${qemu_args[@]}")
"${cmd[@]}"
