#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Host↔guest clipboard bridge via virtio-9p (mount_tag=dennis → /heart/dennis/src).
#
# Host → guest:  make clip-send   then in VM: ir0-paste  (alias: paste)
# Guest → host:  in VM: ir0-clip-out "text"   then on host: make clip-pull
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CLIP_DIR="${IR0_CLIP_DIR:-${ROOT}/.ir0/clipboard}"
INBOX="${CLIP_DIR}/inbox.txt"
OUTBOX="${CLIP_DIR}/outbox.txt"
META="${CLIP_DIR}/meta.txt"

host_read_clipboard()
{
	if command -v wl-paste >/dev/null 2>&1 && { [ -n "${WAYLAND_DISPLAY:-}" ] || [ -n "${XDG_RUNTIME_DIR:-}" ]; }; then
		wl-paste -n 2>/dev/null && return 0
	fi
	if command -v xclip >/dev/null 2>&1 && [ -n "${DISPLAY:-}" ]; then
		xclip -selection clipboard -o 2>/dev/null && return 0
	fi
	if command -v xsel >/dev/null 2>&1 && [ -n "${DISPLAY:-}" ]; then
		xsel --clipboard --output 2>/dev/null && return 0
	fi
	return 1
}

host_write_clipboard()
{
	local data="$1"
	if command -v wl-copy >/dev/null 2>&1 && { [ -n "${WAYLAND_DISPLAY:-}" ] || [ -n "${XDG_RUNTIME_DIR:-}" ]; }; then
		printf '%s' "$data" | wl-copy -n 2>/dev/null && return 0
	fi
	if command -v xclip >/dev/null 2>&1 && [ -n "${DISPLAY:-}" ]; then
		printf '%s' "$data" | xclip -selection clipboard 2>/dev/null && return 0
	fi
	if command -v xsel >/dev/null 2>&1 && [ -n "${DISPLAY:-}" ]; then
		printf '%s' "$data" | xsel --clipboard --input 2>/dev/null && return 0
	fi
	return 1
}

cmd_send()
{
	local data
	mkdir -p "$CLIP_DIR"
	if ! data="$(host_read_clipboard)"; then
		echo "✗ no host clipboard tool (install wl-clipboard, xclip, or xsel)" >&2
		exit 1
	fi
	printf '%s' "$data" > "$INBOX"
	{
		echo "updated=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
		echo "bytes=$(printf '%s' "$data" | wc -c)"
	} > "$META"
	echo "✓ clip-send → ${INBOX}"
	echo "  guest: ir0-paste   (or: paste)"
	echo "  path:  /heart/dennis/src/.ir0/clipboard/inbox.txt"
}

cmd_pull()
{
	if [ ! -f "$OUTBOX" ]; then
		echo "✗ no outbox (run ir0-clip-out in the guest first)" >&2
		exit 1
	fi
	local data
	data="$(cat "$OUTBOX")"
	if ! host_write_clipboard "$data"; then
		echo "✗ could not write host clipboard" >&2
		echo "--- outbox ---"
		cat "$OUTBOX"
		exit 1
	fi
	echo "✓ clip-pull ← ${OUTBOX} ($(wc -c < "$OUTBOX") bytes)"
}

cmd_status()
{
	echo "CLIP_DIR=${CLIP_DIR}"
	for f in "$INBOX" "$OUTBOX" "$META"; do
		if [ -f "$f" ]; then
			echo "  $(basename "$f"): $(wc -c < "$f") bytes  $(stat -c '%y' "$f" 2>/dev/null || stat -f '%Sm' "$f")"
		else
			echo "  $(basename "$f"): (missing)"
		fi
	done
}

usage()
{
	cat <<EOF
Usage: scripts/ir0-clip.sh send|pull|status

  send    host clipboard → .ir0/clipboard/inbox.txt (9p dennis)
  pull    outbox.txt → host clipboard
  status  show bridge files

Makefile: make clip-send | clip-pull | clip-status
EOF
}

case "${1:-}" in
send) cmd_send ;;
pull) cmd_pull ;;
status) cmd_status ;;
-h | --help | help) usage ;;
*)
	usage >&2
	exit 2
	;;
esac
