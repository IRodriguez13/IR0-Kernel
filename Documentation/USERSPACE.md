# Coupling IR0 (kernel) ↔ ISD

> **Last verified:** 2026-09-02  
> **Source of truth:** this file, `scripts/make/isd.mk`, `scripts/bootstrap-isd.sh`, sibling [ISD](https://github.com/IRodriguez13/ISD), [SETUP.md](../SETUP.md),
> `ISD/services/runit_console_run.c`, `ISD/lib/ir0_auth.c`.  
> **Spanish:** [`esp/USERSPACE.md`](esp/USERSPACE.md)

## Why two repositories?

The kernel alone is not a Unix distro. Product PID1 (**runit**), **BusyBox**,
login/doas, packages, man pages, and the finished MINIX `disk.img` live in
**ISD** (IR0 Software Distribution). IR0 compiles the kernel, exports UAPI,
delegates the ISD build, and boots the ISD-owned image.

| Lives in **IR0** | Lives in **ISD** |
|------------------|------------------|
| Kernel, drivers, UAPI (`includes/uapi/`) | Packages, services, rootfs, profiles |
| Boot ISO / QEMU orchestration | `out/<arch>/images/<profile>/disk.img` |
| Host-deps / first-boot wrapper | `.isdconfig` extras, `stage-rootfs`, `pack-minix` |

**Hard rule:** IR0 must not inject BusyBox/runit/nano one-by-one on the
canonical product path. Legacy `load-userspace-runit` remains for smokes
(`IR0_LEGACY_USERSPACE=1`).

## Fastest path (first time)

Happy path on an **x86_64 Linux** host (Debian/Ubuntu/Arch/Fedora/openSUSE or
WSL2). Target: clone → two commands → QEMU with ash.

```bash
git clone https://github.com/IRodriguez13/IR0.git
cd IR0
make first-boot PROFILE=minimal
make run PROFILE=minimal
```

| PROFILE | Expectation in guest |
|---------|----------------------|
| `minimal` | Interactive firstboot (create user) then login |
| `development` | Lab autologin root (empty password allowed) |
| `desktop` | Like minimal + fuller BusyBox/applets (heavier pack) |

`first-boot` will:

1. Check host deps (`IR0_DEPS_INSTALL=ask|yes|never`, default **ask**)
2. Ask before any `sudo apt-get install` (never captures the password)
3. Clone `../ISD` if missing
4. Write `.isdconfig` only if absent
5. Fetch sources, export UAPI, build packages (stamp-incremental)
6. Stage rootfs + pack `disk.img` under ISD (`format-large` wipes MINIX inodes)
7. Build `kernel-x64-userspace.iso`

`make run` rebuilds the ISO if needed, then `ensure-isd-disk` (stamp-incremental
pack; first pack ~1–3 min — progress is printed). Then QEMU GTK.

Layout:

```text
parent/
├── IR0/
└── ISD/
```

| Variable | Default | Role |
|----------|---------|------|
| `IR0_ISD_ROOT` | `../ISD` | distribution tree |
| `IR0_ISD_URL` | `https://github.com/IRodriguez13/ISD.git` | clone URL |
| `PROFILE` | `minimal` (when passed on CLI) | ISD product profile |
| `IR0_PRODUCT_PROFILE` | alias of ISD profile | compat |
| `ISD_ARCH` | `x86_64` | userspace arch name |
| `IR0_USERSPACE_ROOT` / `_URL` | aliases of `IR0_ISD_*` | **deprecated** |

### Config layers

| File | Owner | Meaning |
|------|-------|---------|
| `IR0/.config` | kernel | Kconfig |
| `ISD/profiles/<p>/profile.conf` | ISD | login/root/fsck policy |
| `ISD/profiles/<p>/packages.txt` | ISD | mandatory packages |
| `ISD/.isdconfig` | ISD | optional packages + applets (`make isdconfig`) |

### Targets

| Target | Role |
|--------|------|
| `make first-boot PROFILE=…` | Full product bootstrap |
| `make isdconfig PROFILE=…` | Interactive extras (TTY: packages + applets e.g. top) |
| `make isd` / `isd-rootfs` / `isd-image` | Delegate to ISD |
| `make run PROFILE=…` | Boot ISD disk (stamp-incremental pack) |
| `make run-console PROFILE=…` | Same without GTK |
| `make bootstrap-userspace` | **Deprecated** → `first-boot` |
| `IR0_LEGACY_USERSPACE=1 make run` | Old inject path (smokes) |

### Login / getty notes (2026-09-02)

After firstboot, `etc/runit/sv/console/run` (`runit_console_run`) prompts for
username/password. Smoke markers such as `LOGIN_USER_READ` go to **`/dev/serial`**
only (not the human TTY). Emitting that tag on every empty/`EINTR`/`EOF` read used
to busy-loop the prompt and flood the serial log so it looked like
`Enter your Unix username: LOGIN_USER_READ` repeated. Fixed in ISD:

- `ir0_read_line` retries `EINTR`, treats `r==0` as EOF (not a blank name).
- Login loop tags `LOGIN_USER_READ` only after a non-empty username; backs off
  with `sleep(1)` on read failure.

**Unix reminders (not bugs):** `chown root` with mode `0755` still lets others
execute the file; restrict with `chmod`. `/tmp` as tmpfs is intentional RAM FS.

### Shebang scripts (`#!`)

`execve` / `exec_replace_current` in `kernel/elf_loader.c` recognize a leading
`#!` line before ELF validation: interpreter path, optional argument, then the
script path is appended to `argv`. Recursion depth is capped at 4; non-ELF files
without a valid shebang return `-ENOEXEC`. Example: `./script.sh` with
`#!/bin/sh` runs the interpreter on the script path.

### Incremental builds

ISD stamps under `out/<arch>/stamps/{toolchain,uapi,packages,rootfs,images}/`.
A second `make first-boot PROFILE=minimal` without input changes must not
re-run package `build.sh` scripts.

See ISD [`Documentation/PACKAGES.md`](https://github.com/IRodriguez13/ISD/blob/master/Documentation/PACKAGES.md).
