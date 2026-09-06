#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
#
# Inject Doom (Ken Thompson games path) onto a MINIX disk.img:
#   /usr/ken/games/doom       — interactive doomgeneric (static musl)
#   /usr/ken/games/doom1.wad  — shareware IWAD (optional if missing)
#   /usr/bin/doom             — hardlink/copy for PATH + which
#
# Usage: inject_ken_games_minix.sh [disk.img]
# Env:
#   IR0_KEN_DOOM_BIN   default: setup/pid1/fase55e_doom_interactive
#   REAL_WAD_PATH      IWAD source (copied as doom1.wad)
#   IR0_INSTALL_KEN_GAMES=0  skip

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DISK="${1:-${ROOT}/disk.img}"
INJECT="${ROOT}/scripts/inject_init_minix.py"
BIN="${IR0_KEN_DOOM_BIN:-${ROOT}/setup/pid1/fase55e_doom_interactive}"
WAD="${REAL_WAD_PATH:-}"

if [ "${IR0_INSTALL_KEN_GAMES:-1}" = "0" ]; then
	echo "  SKIP    ken/games (IR0_INSTALL_KEN_GAMES=0)"
	exit 0
fi

if [ ! -f "${DISK}" ]; then
	echo "✗ inject_ken_games: missing ${DISK}" >&2
	exit 1
fi

if [ ! -f "${BIN}" ]; then
	echo "  WARN    ken/games: missing ${BIN} — build with: make build-ktm-doom-interactive" >&2
	exit 0
fi

echo "  KEN     /usr/ken/games/doom ← ${BIN}"
python3 "${INJECT}" --mode 0755 "${DISK}" "${BIN}" usr/ken/games/doom
# PATH-visible name (BusyBox which / usr/bin)
python3 "${INJECT}" --mode 0755 "${DISK}" "${BIN}" usr/bin/doom

if [ -n "${WAD}" ] && [ -f "${WAD}" ]; then
	echo "  KEN     /usr/ken/games/doom1.wad ← ${WAD}"
	python3 "${INJECT}" --mode 0644 "${DISK}" "${WAD}" usr/ken/games/doom1.wad
	# Compat path still used by older smokes
	python3 "${INJECT}" --mode 0644 "${DISK}" "${WAD}" usr/share/doom/doom1.wad
else
	echo "  WARN    ken/games: no IWAD (set REAL_WAD_PATH=/path/to/doom1.wad)" >&2
fi

echo "✓ inject_ken_games_minix OK"
