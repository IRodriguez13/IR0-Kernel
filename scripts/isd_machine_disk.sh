#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Manage a mutable IR0 machine disk separately from reproducible ISD images.
set -euo pipefail

ACTION="${1:-}"
BASE_DISK="${IR0_MACHINE_BASE_DISK:?IR0_MACHINE_BASE_DISK is required}"
MACHINE_DISK="${IR0_MACHINE_DISK:?IR0_MACHINE_DISK is required}"
VMDK="${IR0_MACHINE_VMDK:-${MACHINE_DISK%.img}.vmdk}"

if [ "$BASE_DISK" = "$MACHINE_DISK" ]; then
	echo "✗ base and machine disks must be different files" >&2
	exit 2
fi

case "$ACTION" in
create)
	if [ -f "$MACHINE_DISK" ]; then
		echo "  MACHINE  preserving existing $MACHINE_DISK"
		exit 0
	fi
	if [ ! -f "$BASE_DISK" ]; then
		echo "✗ missing base image: $BASE_DISK" >&2
		echo "  Run make first-boot PROFILE=<profile> first." >&2
		exit 2
	fi
	mkdir -p "$(dirname "$MACHINE_DISK")"
	tmp="${MACHINE_DISK}.new.$$"
	trap 'rm -f "$tmp"' EXIT
	cp --reflink=auto --sparse=always "$BASE_DISK" "$tmp"
	mv "$tmp" "$MACHINE_DISK"
	trap - EXIT
	echo "✓ machine disk created: $MACHINE_DISK"
	;;
reset)
	if [ "${CONFIRM_RESET:-}" != "yes" ]; then
		echo "✗ reset would discard persistent guest data" >&2
		echo "  Retry with CONFIRM_RESET=yes" >&2
		exit 2
	fi
	if [ ! -f "$BASE_DISK" ]; then
		echo "✗ missing base image: $BASE_DISK" >&2
		exit 2
	fi
	mkdir -p "$(dirname "$MACHINE_DISK")"
	tmp="${MACHINE_DISK}.new.$$"
	trap 'rm -f "$tmp"' EXIT
	cp --reflink=auto --sparse=always "$BASE_DISK" "$tmp"
	mv "$tmp" "$MACHINE_DISK"
	trap - EXIT
	echo "✓ machine reset from base: $MACHINE_DISK"
	;;
export-vmdk)
	if [ ! -f "$MACHINE_DISK" ]; then
		echo "✗ missing machine disk: $MACHINE_DISK" >&2
		exit 2
	fi
	if ! command -v qemu-img >/dev/null 2>&1; then
		echo "✗ qemu-img is required for VMDK export" >&2
		exit 2
	fi
	if [ -e "$VMDK" ] && [ "${FORCE:-}" != "yes" ]; then
		echo "✗ refusing to overwrite $VMDK" >&2
		echo "  Retry with FORCE=yes" >&2
		exit 2
	fi
	tmp="${VMDK}.new.$$"
	trap 'rm -f "$tmp"' EXIT
	qemu-img convert -f raw -O vmdk -o subformat=monolithicSparse \
		"$MACHINE_DISK" "$tmp"
	mv "$tmp" "$VMDK"
	trap - EXIT
	echo "✓ VMware disk exported: $VMDK"
	;;
*)
	echo "usage: $0 create|reset|export-vmdk" >&2
	exit 2
	;;
esac
