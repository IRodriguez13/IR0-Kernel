#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Ensure a packed ISD MINIX disk with product BusyBox applets (echo/cat/true/sh/…).
#
# Usage:
#   ktm_prepare_isd_session_disk.sh
#
# Prints the absolute disk path as the last stdout line. Progress goes to stderr.
# Env:
#   IR0_ISD_ROOT / IR0_ISD_URL   sibling ISD tree (cloned if missing)
#   IR0_USERSPACE_*              deprecated aliases of IR0_ISD_*
#   PROFILE / IR0_PRODUCT_PROFILE  minimal|development|desktop|appliance
#                                (default: development)
#   ISD_ARCH                     x86_64
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [ -n "${IR0_USERSPACE_ROOT:-}" ] && [ -z "${IR0_ISD_ROOT:-}" ]; then
	echo "note: IR0_USERSPACE_ROOT is deprecated; use IR0_ISD_ROOT" >&2
	IR0_ISD_ROOT="${IR0_USERSPACE_ROOT}"
fi
if [ -n "${IR0_USERSPACE_URL:-}" ] && [ -z "${IR0_ISD_URL:-}" ]; then
	echo "note: IR0_USERSPACE_URL is deprecated; use IR0_ISD_URL" >&2
	IR0_ISD_URL="${IR0_USERSPACE_URL}"
fi

ISD_URL="${IR0_ISD_URL:-https://github.com/IRodriguez13/ISD.git}"
ARCH="${ISD_ARCH:-x86_64}"
PROFILE="${PROFILE:-${IR0_PRODUCT_PROFILE:-development}}"

case "$PROFILE" in
minimal|development|desktop|appliance) ;;
*)
	echo "✗ unknown PRODUCT PROFILE=${PROFILE}" >&2
	exit 2
	;;
esac

resolve_isd_root()
{
	local cand
	if [ -n "${IR0_ISD_ROOT:-}" ]; then
		printf '%s\n' "$IR0_ISD_ROOT"
		return 0
	fi
	for cand in \
		"$ROOT/../ISD" \
		"${HOME}/ISD"
	do
		if [ -f "${cand}/Makefile" ]; then
			printf '%s\n' "$(cd "$cand" && pwd)"
			return 0
		fi
	done
	printf '%s\n' "${HOME}/ISD"
}

ISD_ROOT="$(resolve_isd_root)"
export IR0_ISD_ROOT="$ISD_ROOT"
export IR0_USERSPACE_ROOT="$ISD_ROOT"

if [ ! -f "${ISD_ROOT}/Makefile" ]; then
	echo "  CLONE    ${ISD_URL} → ${ISD_ROOT}" >&2
	mkdir -p "$(dirname "$ISD_ROOT")"
	git clone --depth 1 "$ISD_URL" "$ISD_ROOT"
fi

DISK="${ISD_ROOT}/out/${ARCH}/images/${PROFILE}/disk.img"
mkdir -p "${ISD_ROOT}/out"
# Session-stress needs product BusyBox applets, not nano/tcc extras.
# isd-defconfig turns those extras on; keep a dedicated config so fetch/image
# stay on busybox+runit from profiles/$(PROFILE)/packages.txt.
ISD_CONFIG="${ISD_ROOT}/out/session-stress.isdconfig"
cat > "$ISD_CONFIG" <<'EOF'
# ISD extras for KTM session-stress (BusyBox applets only).
CONFIG_PKG_BUSYBOX=y
CONFIG_PKG_RUNIT=y
CONFIG_PKG_NANO=n
CONFIG_PKG_NCURSES=n
CONFIG_PKG_OPENDOAS=n
CONFIG_PKG_TINYCC=n
CONFIG_PKG_GNUMAKE=n
CONFIG_PKG_DOOM=n
CONFIG_APPLET_TOP=y
EOF
export ISD_CONFIG

echo "  ISD     PROFILE=${PROFILE} ARCH=${ARCH}" >&2
echo "          root=${ISD_ROOT}" >&2
echo "          disk=${DISK}" >&2

# Product pack: fetch sources, build musl BusyBox/runit/services, MINIX image.
make -C "$ROOT" ensure-isd-disk \
	IR0_ISD_ROOT="$ISD_ROOT" \
	IR0_ROOT="$ROOT" \
	PROFILE="$PROFILE" \
	ISD_ARCH="$ARCH" >&2

if [ ! -f "$DISK" ]; then
	echo "✗ missing packed ISD disk: $DISK" >&2
	exit 1
fi

python3 "$ROOT/scripts/verify_minix_rootfs.py" --gate "$DISK" \
	/bin/busybox /bin/echo /bin/cat /bin/true /bin/sh /bin/ls /bin/uname >&2

echo "$DISK"
