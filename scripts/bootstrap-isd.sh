#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
#
# First-boot: host deps → clone ISD → UAPI → packages → rootfs → disk.img → kernel ISO.
#
# Usage (from IR0 tree):
#   make first-boot PROFILE=minimal
#   ./scripts/bootstrap-isd.sh
#
# Env:
#   PROFILE / IR0_PRODUCT_PROFILE   minimal|development|desktop|appliance
#   IR0_ISD_ROOT / IR0_ISD_URL      sibling path and clone URL
#   IR0_USERSPACE_*                 deprecated aliases of IR0_ISD_*
#   IR0_DEPS_INSTALL                ask (default) | yes | never
#   ISD_ARCH                        x86_64 (only supported for product path)

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${ROOT}"

PATH="${HOME}/.cargo/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
export PATH

# Deprecated aliases
if [ -n "${IR0_USERSPACE_ROOT:-}" ] && [ -z "${IR0_ISD_ROOT:-}" ]; then
	echo "note: IR0_USERSPACE_ROOT is deprecated; use IR0_ISD_ROOT"
	IR0_ISD_ROOT="${IR0_USERSPACE_ROOT}"
fi
if [ -n "${IR0_USERSPACE_URL:-}" ] && [ -z "${IR0_ISD_URL:-}" ]; then
	echo "note: IR0_USERSPACE_URL is deprecated; use IR0_ISD_URL"
	IR0_ISD_URL="${IR0_USERSPACE_URL}"
fi

ISD_ROOT="${IR0_ISD_ROOT:-${ROOT}/../ISD}"
ISD_URL="${IR0_ISD_URL:-https://github.com/IRodriguez13/ISD.git}"
ARCH="${ISD_ARCH:-x86_64}"
PROFILE="${PROFILE:-${IR0_PRODUCT_PROFILE:-minimal}}"

case "${PROFILE}" in
minimal|development|desktop|appliance) ;;
desktop-x86_64|userspace|hub|hub-rpi4|watch|watch-rpi5-stub|all)
	PROFILE=minimal
	;;
*)
	echo "✗ unknown PRODUCT PROFILE=${PROFILE} (expected minimal|development|desktop|appliance)" >&2
	exit 1
	;;
esac

case "${ARCH}" in
x86_64|x86-64) ARCH=x86_64 ;;
*)
	echo "✗ ISD product first-boot currently supports ARCH=x86_64 only (got ${ARCH})" >&2
	exit 1
	;;
esac

HOST_M="$(uname -m 2>/dev/null || echo unknown)"
case "${HOST_M}" in
x86_64|amd64) ;;
*)
	echo "✗ host machine is ${HOST_M}; ISD first-boot builds x86_64 musl packages + QEMU x86_64." >&2
	echo "  Use an x86_64 Linux/WSL distro, or set up a cross musl toolchain (unsupported here)." >&2
	exit 1
	;;
esac

IN_WSL=0
if [ -n "${WSL_DISTRO_NAME:-}" ] || [ -n "${WSL_INTEROP:-}" ] || \
	grep -qi microsoft /proc/version 2>/dev/null || \
	grep -qi wsl /proc/sys/kernel/osrelease 2>/dev/null; then
	IN_WSL=1
fi

echo "=== IR0 first-boot ==="
echo "PROFILE      ${PROFILE}"
echo "ARCH         ${ARCH}"
echo "HOST         ${HOST_M}"
echo "KERNEL       ${ROOT}"
echo "ISD          ${ISD_ROOT}"
if [ "$IN_WSL" -eq 1 ]; then
	echo "WSL          yes (KVM optional; TCG fallback OK)"
fi
echo ""

# Free space hint (need room for sources + images ~1–2 GiB)
if command -v df >/dev/null 2>&1; then
	avail_kb="$(df -Pk "${ROOT}" 2>/dev/null | awk 'NR==2 {print $4}')"
	if [ -n "${avail_kb:-}" ] && [ "${avail_kb}" -lt 1500000 ] 2>/dev/null; then
		echo "WARN         low free space on $(df -Pk "${ROOT}" | awk 'NR==2 {print $6}') (~${avail_kb} KiB)"
		echo "             recommend ≥1.5 GiB free before first-boot"
		echo ""
	fi
fi

# 0) Kernel .config (deptest treats missing as optional; first-boot needs one)
if [ ! -f "${ROOT}/.config" ]; then
	echo "CONFIG       no .config — running make defconfig"
	make defconfig
fi

# 1) Host dependencies (ask before sudo)
echo "DEPS         checking host dependencies"
chmod +x "${ROOT}/scripts/ensure-host-deps.sh" "${ROOT}/scripts/deptest.sh"
PROFILE=userspace "${ROOT}/scripts/ensure-host-deps.sh"

# 2) Clone ISD if missing
if [ ! -f "${ISD_ROOT}/Makefile" ]; then
	if ! command -v git >/dev/null 2>&1; then
		echo "✗ git not found (should have been installed by host deps)" >&2
		exit 1
	fi
	parent="$(dirname "${ISD_ROOT}")"
	if [ ! -w "${parent}" ]; then
		echo "✗ cannot write sibling directory ${parent}" >&2
		echo "  Clone ISD manually or set IR0_ISD_ROOT to a writable path." >&2
		exit 1
	fi
	mkdir -p "${parent}"
	echo "CLONE        ${ISD_URL}"
	git clone --depth 1 "${ISD_URL}" "${ISD_ROOT}"
	if [ ! -f "${ISD_ROOT}/Makefile" ]; then
		echo "✗ clone finished but ${ISD_ROOT}/Makefile missing" >&2
		exit 1
	fi
else
	echo "CLONE        ISD already present"
fi

export IR0_ISD_ROOT="${ISD_ROOT}"
export IR0_USERSPACE_ROOT="${ISD_ROOT}"
export IR0_ROOT="${ROOT}"
export IR0_PRODUCT_PROFILE="${PROFILE}"
export PROFILE="${PROFILE}"

ISD_MAKE=(make -C "${ISD_ROOT}" IR0_ROOT="${ROOT}" ARCH="${ARCH}" PROFILE="${PROFILE}")

# 3) .isdconfig if missing
echo "CONFIG       isd-defconfig (no overwrite if present)"
chmod +x "${ISD_ROOT}/scripts/isdconfig.py" "${ISD_ROOT}/scripts/resolve-packages.sh" \
	"${ISD_ROOT}/scripts/stamp-run.sh" 2>/dev/null || true
"${ISD_MAKE[@]}" isd-defconfig

# 4) Fetch sources (incremental — fetch-package.sh skips existing)
echo "FETCH        package sources"
"${ISD_MAKE[@]}" fetch

# 5) UAPI
echo "UAPI         export / stamp"
"${ISD_MAKE[@]}" headers

# 6) Packages (stamp-based incremental)
echo "PKG          building resolved set for PROFILE=${PROFILE}"
"${ISD_MAKE[@]}" build

# 7) Rootfs + image
echo "ROOTFS       ${PROFILE}"
"${ISD_MAKE[@]}" rootfs-tree

DISK="${ISD_ROOT}/out/${ARCH}/images/${PROFILE}/disk.img"
echo "IMAGE        ${DISK}"
"${ISD_MAKE[@]}" image-minix

# 8) Kernel + boot ISO (Make rebuilds only if needed)
echo "KERNEL       kernel-x64-userspace.iso"
make kernel-x64-userspace.iso

ISO="${ROOT}/kernel-x64-userspace.iso"

cat <<EOF

=== first-boot complete ===
PROFILE      ${PROFILE}
ARCH         ${ARCH}
DISK         ${DISK}
ISO          ${ISO}

Next:
  make poweron PROFILE=${PROFILE}

Host deps: IR0_DEPS_INSTALL=ask|yes|never (default ask)
Extras:    make isdconfig PROFILE=${PROFILE}
Docs:      SETUP.md, Documentation/USERSPACE.md
EOF

if [ "$IN_WSL" -eq 1 ]; then
	cat <<'EOF'
WSL tips:
  - If QEMU GUI fails, try: make run-console PROFILE=minimal
  - Nested KVM is optional; TCG works without /dev/kvm
  - Keep the IR0 and ISD trees on the Linux filesystem (not /mnt/c) for speed
EOF
fi
