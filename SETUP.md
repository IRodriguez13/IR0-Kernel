# IR0 Setup and Bootstrap

This document covers reproducible build, ISO creation, QEMU execution, and minimal userspace bring-up. It intentionally excludes smoke harnesses, regression targets, and internal phase naming.

For architecture and capability boundaries, see [README.md](README.md).

## Prerequisites

### Required for `make ir0` / `PROFILE=desktop` (kernel ISO + QEMU)

| Tool | Purpose |
|------|---------|
| `gcc`, `g++`, `ld` | Kernel C/C++ compile and ELF link |
| `nasm` | x86-64 assembly |
| `make` | Build orchestration |
| `python3` (+ curses) | menuconfig / Kconfig helpers |
| `grub-mkrescue` + `xorriso` | ISO generation (`kernel-x64.iso`) |
| `qemu-system-x86_64` | Execution |

Debian/Ubuntu example:

```bash
sudo apt install build-essential nasm qemu-system-x86 grub-pc-bin xorriso python3
```

Rust (`rustc` / `cargo` / nightly `rust-src` via [rustup](https://rustup.rs)) is **optional** for the default kernel. It is required only when `.config` has `CONFIG_ENABLE_EXAMPLE_DRIVERS=y` (off in `make defconfig`).

### Required for static userspace / `make first-boot` (`PROFILE=userspace`)

| Tool | Purpose |
|------|---------|
| Everything in desktop above | ISO + QEMU |
| `x86_64-linux-musl-gcc` or `musl-gcc` | Static musl binaries (BusyBox, runit, …) |
| `git`, `curl`, `patch`, `sha256sum`, `file`, `flock` | Clone sibling, fetch/verify/patch package sources |
| `yacc` (from `bison`) | Build opendoas (`parse.y`) |

```bash
sudo apt install musl-tools git curl patch file util-linux bison
# or set MUSL_CC=/path/to/x86_64-linux-musl-gcc
# Arch: pacman -S musl bison   (musl provides musl-gcc; there is no musl-gcc package)
```

`make first-boot` runs `ensure-host-deps` (`IR0_DEPS_INSTALL=ask|yes|never`), creates `.config` via `defconfig` if missing, clones sibling **ISD**, builds rootfs + ISO. Optional Doom inject: `IR0_INSTALL_KEN_GAMES=1 REAL_WAD_PATH=/path/to/doom1.wad make first-boot`.

### Required for ARM hub/watch (`PROFILE=hub` / `watch`)

| Tool | Purpose |
|------|---------|
| `aarch64-linux-gnu-gcc` / `ld` | Freestanding ARM64 images |
| `qemu-system-aarch64` | ARM smokes (`raspi4b` optional — smoke SKIPs if missing) |

```bash
sudo apt install gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu qemu-system-arm
# aarch64 musl cross (optional hello / future userspace):
./scripts/setup_musl_aarch64.sh
# or: make setup-musl-aarch64 && make musl-aarch64-hello
```

Verify host tools for the profile you will build (`check-env` is an alias of `deptest`):

```bash
make check-env                              # desktop-x86_64
make check-env PROFILE=desktop-x86_64
make check-env PROFILE=userspace
make check-env PROFILE=hub-rpi4
make check-env PROFILE=all
```

Errors distinguish **required**, **optional**, **unsupported_version**, and
**present_but_unusable**, with apt/pacman/dnf install hints. Exit 0 only when
every required check for that profile passed.
If `.config` is missing: `make defconfig` (or `make ir0_defconfig PROFILE=desktop`).

### Man pages (subsystem docs)

```bash
make sync-mandocs          # install → ~/.local/share/man (no sudo)
make man TOPIC=onboarding  # first clone → first bug
make man TOPIC=boot
make man TOPIC=multi-arch
make run-bootlog           # optional: CONFIG_BOOT_LOG_HOSTSHARE → build/hostshare/ir0-boot.log
make help-bootlog
```

Interactive / one language: `make mandocs`, `make mandocs-en`, `make mandocs-es`.
See also `make help-docs`.

**Boot log on the host (opt-in):** Linux-style *consultable* ring dump, not a
verbose ACPI/PCI wall. Requires QEMU `-virtfs` (wired by `run-bootlog` /
`smoke-boot-log-hostshare`). Normal `make run` does not need it.

## Sibling ISD distribution (required for product rootfs)

Product PID1 (runit), BusyBox, login/doas, rootfs, and `disk.img` live in
**ISD**. **Recommended first-time path:**

```bash
make first-boot PROFILE=minimal   # host deps (ask), clone ../ISD, image + ISO
make run PROFILE=minimal          # QEMU → BusyBox ash (GTK)
```

If a required host tool is missing, `first-boot` lists it and asks
`Install missing host dependencies? [y/N]` (uses `sudo` for the package manager
only; password prompt is from sudo — never stored). Decline with `n` and follow
this file, or set `IR0_DEPS_INSTALL=never` / `yes` for non-interactive use.

Supported host package managers: **apt** (Debian/Ubuntu/Mint), **pacman** (Arch),
**dnf** (Fedora), **zypper** (openSUSE). Product path requires an **x86_64** host.

### WSL (Windows Subsystem for Linux)

WSL2 is supported for build + QEMU:

| Topic | Guidance |
|-------|----------|
| Distro | Ubuntu/Debian x86_64 under WSL2 (not WSL1) |
| Tree location | Keep `IR0/` + `ISD/` on the Linux filesystem (`~/…`), not `/mnt/c` |
| KVM | Optional. Without `/dev/kvm`, QEMU uses TCG (slower, OK). Nested KVM needs Windows 11 + `nestedVirtualization=true` in `%UserProfile%\.wslconfig`, then `wsl --shutdown` |
| GUI | WSLg or an X server; if GTK fails: `make run-console PROFILE=minimal` |
| sudo | Same ask/yes/never flow; password is typed to sudo in the terminal |

Manual equivalent (sibling layout: `IR0/` next to `ISD/`):

```bash
git clone https://github.com/IRodriguez13/ISD.git ../ISD
# or: export IR0_ISD_ROOT=/path/to/ISD
export IR0_ROOT=$PWD
make check-isd
make -C ../ISD fetch headers build ARCH=x86_64 PROFILE=minimal
make -C ../ISD image-minix PROFILE=minimal
make kernel-x64-userspace.iso
make run PROFILE=minimal
```

`ARCH=x86_64` is the userspace triple; the kernel Makefile accepts it as an alias of `x86-64`.
Deprecated: `IR0_USERSPACE_ROOT`, `bootstrap-userspace`, `load-userspace-runit` on the
canonical path (still available for smokes via `IR0_LEGACY_USERSPACE=1`).

**Boot without `/sbin/init`:** kernel `panic("Failed to load /sbin/init")`.
**Init exits:** idle keeps the CPU; no userspace shell until reboot with a live PID1.

Full boundary: [`Documentation/USERSPACE.md`](Documentation/USERSPACE.md)
(Spanish: [`Documentation/esp/USERSPACE.md`](Documentation/esp/USERSPACE.md)).

## First-Time Configuration

Copy the default Kconfig snapshot:

```bash
make defconfig
```

This installs `setup/defconfig` as `.config` and regenerates `config.h`.

Default highlights:

- x86-64, MINIX root on `hda`, round-robin scheduler
- runit `/sbin/init` from sibling **ISD** (no in-kernel dbgshell)
- VBE framebuffer enabled (`CONFIG_ENABLE_VBE=y`)

Daily run targets build the userspace ISO and use the ISD-owned `disk.img`.
Clone layout and wiring: [`Documentation/USERSPACE.md`](Documentation/USERSPACE.md).

## Basic Kernel Build

```bash
make kernel-x64.bin    # linked ELF kernel
make kernel-x64.iso    # GRUB rescue ISO with kernel-x64.bin
```

Default aggregate target (ISO on x86-64):

```bash
make ir0
```

Artifacts:

- `kernel-x64.bin` — kernel ELF
- `kernel-x64.iso` — bootable ISO
- `disk.img` — created on demand by run targets (200 MB MINIX by default)

Clean:

```bash
make clean
```

## Running in QEMU

### Graphical run (runit + getty/ash)

```bash
make first-boot   # once: sibling + disk.img
make poweron      # reuse the installed machine on every boot
```

`first-boot` builds a reproducible ISD base image and creates a separate mutable
machine disk under the sibling `IR0-machines/` directory. `poweron` reuses that
disk without running the ISD image packer, so guest users, configuration and
files survive kernel or rootfs rebuilds. It boots existing artifacts without
recompiling; rebuild `kernel-x64-userspace.iso` explicitly after kernel changes.
Does **not** require TinyCC/GNU make unless
`IR0_WITH_DEVTOOLS=1`.

Use `IR0_MACHINE=name` to keep multiple installations of the same profile.
Reset is intentionally explicit and destructive:

```bash
make machine-info PROFILE=desktop
CONFIRM_RESET=yes make machine-reset PROFILE=desktop
make image-vmware PROFILE=desktop   # exports VMDK; attach the kernel ISO as CD
```

### Serial-only (no GUI)

```bash
make run-console
```

Equivalent to nographic mode with serial attached — useful when framebuffer is irrelevant.

### Serial debug to terminal

```bash
make run-debug
```

Guest serial and debug events go to the invoking terminal; QEMU monitor on telnet `127.0.0.1:1234`.

`make run-nodisk` and `make run-dbgshell` are retired: product boot requires
the runit rootfs.

### Network (optional, host setup required)

```bash
make run-tap    # needs root + configured bridge/TAP
make run-ping   # TAP + scripted ping check
```

Requires TUN/TAP and bridge configuration on the host (see comments in `Makefile` near `run-tap`).

## Serial Logging

Kernel event records go to COM1, the boot console, `/proc/kmsg`, and
`/dev/kmsg`. Records always carry sequence + boot phase; time remains `?.???`
until the monotonic clock is online.

| Target | Serial behavior |
|--------|-----------------|
| `make run-console` | `-serial stdio` (console) |
| `make run-debug` | `-serial stdio` + guest error/int logging |
| `make run` | Optional `qemu_debug.log` if writable in cwd |

For userspace bring-up, redirect serial to a file by running QEMU manually with `-serial file:boot.log` after following the disk layout steps below.

## Userspace Boot Path

Real ring-3 init requires:

1. Userspace-enabled kernel ISO
2. MINIX disk with `/sbin/init` and supporting binaries
3. QEMU with ISO + disk

### 1. Build userspace kernel ISO

```bash
make kernel-x64-userspace.iso
```

This produces a kernel built with userspace init boot (`USERSPACE_INIT_BUILD=1`) packaged as `kernel-x64-userspace.iso`. The default `kernel-x64.bin` in the repo root is restored after this step.

### 2. Create and populate disk image

Create a blank MINIX image:

```bash
python3 scripts/inject_init_minix.py --format disk.img
```

For larger rootfs layouts:

```bash
python3 scripts/inject_init_minix.py --format-large disk.img
```

Inject files (destination path is relative to root, no leading slash):

```bash
python3 scripts/inject_init_minix.py disk.img LOCAL_FILE path/on/disk
```

## Minimal BusyBox Userspace

BusyBox is **not** shipped as a committed binary; build from vendored sources:

```bash
make build-busybox-fase50-min
```

Output: `setup/pid1/fase50_busybox_real` (static musl ELF). Configuration fragment: `setup/busybox/fase58_busybox.config`.

Build the canonical PID1 (runit):

```bash
make build-runit
```

Output: `setup/runit/bin/runit-init` (and companions under `setup/runit/bin/`).

Example rootfs layout for interactive `ash` (or use `make load-userspace-runit`):

```bash
DISK=disk.img
python3 scripts/inject_init_minix.py --format-large "$DISK"
python3 scripts/inject_init_minix.py "$DISK" setup/runit/bin/runit-init sbin/init
python3 scripts/inject_init_minix.py "$DISK" setup/pid1/fase50_busybox_real bin/busybox
python3 scripts/inject_init_minix.py "$DISK" setup/pid1/fase50_busybox_real bin/sh
python3 scripts/verify_minix_rootfs.py "$DISK" /sbin/init /bin/sh /bin/busybox
```

Run:

```bash
qemu-system-x86_64 \
  -cdrom kernel-x64-userspace.iso \
  -drive file="$DISK",format=raw,if=ide,index=0 \
  -m 256M -no-reboot -net none \
  -display gtk -serial stdio
```

runit stage 2 brings up console services; product path uses BusyBox `ash` on
`/dev/console` (GUI helper: `make run-fase58e-ash-gui`). Type in the QEMU window
(keyboard focus required).

Extended applet set (optional):

```bash
make build-busybox-fase58-full   # uses setup/busybox/fase58_full.config
```

## Doom (optional)

Build the userspace doomgeneric binary:

```bash
make build-fase55e-doom-interactive
```

Provide a legal IWAD (e.g. `DOOM1.WAD`). Set path when injecting:

```bash
export REAL_WAD_PATH=/path/to/DOOM1.WAD
python3 scripts/inject_init_minix.py "$DISK" setup/pid1/fase55e_doom_interactive bin/doomgeneric
python3 scripts/inject_init_minix.py "$DISK" "$REAL_WAD_PATH" usr/share/doom/doom1.wad
```

From `ash`:

```text
doomgeneric /usr/share/doom/doom1.wad
```

Requires `/dev/fb0` and input devices as implemented in devfs. Without a WAD, skip these inject steps.

## TinyCC (optional)

```bash
make build-tcc-fase52
```

Clones/builds TinyCC via `setup/tcc/build-fase52.sh` (default output under `/tmp/tinycc-fase52`). Used for in-guest compilation experiments; not required for basic shell bring-up.

## Essential Make Targets

| Target | Description |
|--------|-------------|
| `defconfig` | Install default `.config` |
| `deptest` | Check build dependencies |
| `kernel-x64.bin` | Build kernel ELF |
| `kernel-x64.iso` | Build boot ISO |
| `kernel-x64-userspace.iso` | ISO booting `/sbin/init` path |
| `disk.img` / `create-disk` | Create MINIX disk image |
| `run` | QEMU GUI + disk |
| `run-console` | QEMU nographic + serial |
| `run-debug` | QEMU GUI + serial debug |
| `build-runit` | Build runit PID1 (`/sbin/init`) |
| `load-userspace-runit` | Inject runit + BusyBox on MINIX disk |
| `build-busybox-fase50-min` | Build static BusyBox |
| `build-busybox-fase58-full` | BusyBox with extended applets |
| `build-fase55e-doom-interactive` | Build doomgeneric binary |
| `build-tcc-fase52` | Build TinyCC (optional) |
| `clean` | Remove build artifacts |

Configuration menu (optional):

```bash
make menuconfig
```

Requires Python 3 with tkinter for the graphical front-end.

## Console and Framebuffer Notes

With `CONFIG_ENABLE_VBE=y` (default):

- GRUB sets a linear framebuffer mode (default **1280×800×32** in `arch/x86-64/grub.cfg`).
- Early kernel text uses VGA at `0xB8000` or the FB path when VBE info is available.
- Logical console size is **80 columns × 25 rows**; glyphs are scaled **2×** on the framebuffer and centered (letterbox).
- Default character attribute is VT-style **0x07** (light gray on black).
- `console_renderer.c` interprets a **subset** of ANSI SGR sequences for foreground/background color.
- TTY echo and userspace writes share one cursor model through the console backend.
- Userspace full-screen clients use **`/dev/fb0`** (mmap) and **`/dev/events0`** for input where enabled.

No additional visual tuning is documented here; this section describes current behavior only.

## Troubleshooting

| Symptom | Likely cause |
|---------|----------------|
| `musl cross compiler not found` | Install `musl-tools` or set `MUSL_CC` |
| `grub-mkrescue` missing | Install `grub-pc-bin` / `grub2-common` |
| `/sbin/init` not found at boot | Disk missing init; use userspace ISO + inject steps |
| Blank GUI, serial OK | QEMU display focus; or booted kernel shell ISO instead of userspace ISO |
| BusyBox missing applets | Rebuild with `build-busybox-fase58-full` or adjust `setup/busybox/*.config` |

## See Also

- [README.md](README.md) — project state and architecture
- [Documentation/README.md](Documentation/README.md) — subsystem documentation index
- [Documentation/mandocs/en/INDEX.md](Documentation/mandocs/en/INDEX.md) — internals mandocs (`make mandocs-en`, then `man IR0-vfs`)
- [LICENSE](LICENSE) — GPL v3
