#!/usr/bin/env python3
"""
IR0 architecture guardrails.

Checks:
1) No direct <drivers/...> includes inside fs/* and kernel/syscalls.c.
2) Required facade headers for subsystem decoupling exist.
3) Portable trees (fs/kernel/mm/net) must not include interrupt controller headers
   (#include <interrupt/arch/...>);
4) Files under fs/ must not use #include <arch/...>; use includes/ir0/* facades instead.
5) fs/, mm/, net/, drivers/: no #include <kernel/*.h> (no whitelist).
6) mm/, net/, sched/: no #include <arch/...>.
7) drivers/: no raw #include of drivers/storage/block_dev.h — use ir0/block_dev.h.
8) Paths outside drivers/bluetooth/ must not #include bluetooth/...
9) drivers/: no #include <arch/...> (use ir0/arch_port.h).
10) kernel/: no #include <drivers/...> (whole tree).
11) kernel/: no #include <arch/common/arch_portable.h> (use ir0/arch_port.h).
12) fs/: no #include <mm/...> (use ir0/mm_port.h or narrower facades).
13) Portable trees must not embed ISA asm (pause/hlt/cli/port IO/CR/DR/…);
    use simple facades in includes/ir0/cpu.h (cpu_relax, smp_mb, inb, …).
    Allowlist: syscall_x86_64.h / syscall_arm64.h only. Empty barrier OK.
14) Portable trees must not hardcode userspace RIPs (0x402B*) or read CR2
    / emit iretq/sysret mnemonics outside arch/ (use arch_page_fault /
    arch_fork facades).
15) includes/ir0/*.h must not #include <drivers/...> (facade seal).
16) includes/ir0/*.h must not #include <arch/...> or <sched/...> (facade seal).
17) process_t.page_directory must not be touched outside process.h /
    mm_struct.c — use process_pgd() / process_set_pgd() (mm->page_directory OK).
18) kernel/lib I1–I2: selected syscall/MM/IPC helpers must not remain as .c under includes/ir0/.
19) Portable task setup must not construct x86 RFLAGS directly; use task_ops.
"""

from pathlib import Path
import json
import sys
import re


ROOT = Path(__file__).resolve().parent.parent

FORBIDDEN_PATHS = [
    ROOT / "fs",
    ROOT / "kernel" / "syscalls.c",
]

REQUIRED_FACADES = [
    ROOT / "includes" / "ir0" / "driver.h",
    ROOT / "includes" / "ir0" / "driver_bootstrap.h",
    ROOT / "includes" / "ir0" / "block_dev.h",
    ROOT / "includes" / "ir0" / "partition.h",
    ROOT / "includes" / "ir0" / "arch_port.h",
    ROOT / "includes" / "ir0" / "mm_port.h",
    ROOT / "includes" / "ir0" / "clock.h",
    ROOT / "includes" / "ir0" / "rtc.h",
    ROOT / "includes" / "ir0" / "serial_io.h",
    ROOT / "includes" / "ir0" / "audio_backend.h",
    ROOT / "includes" / "ir0" / "console_backend.h",
    ROOT / "includes" / "ir0" / "video_backend.h",
    ROOT / "includes" / "ir0" / "input_backend.h",
    ROOT / "includes" / "ir0" / "net.h",
    ROOT / "includes" / "ir0" / "bluetooth.h",
    ROOT / "includes" / "ir0" / "usb_host.h",
    ROOT / "includes" / "ir0" / "resource_registry.h",
    ROOT / "includes" / "ir0" / "init_drv.h",
    ROOT / "includes" / "ir0" / "sched.h",
    ROOT / "includes" / "ir0" / "scheduler_api.h",
    ROOT / "includes" / "ir0" / "pseudo_fs.h",
    ROOT / "includes" / "ir0" / "credentials.h",
    ROOT / "includes" / "ir0" / "ktm" / "ktm.h",
    ROOT / "includes" / "ir0" / "irq.h",
    ROOT / "includes" / "ir0" / "arch_switch.h",
    ROOT / "includes" / "ir0" / "arch_mm.h",
    ROOT / "includes" / "ir0" / "arch_signal.h",
    ROOT / "includes" / "ir0" / "arch_elf.h",
    ROOT / "includes" / "ir0" / "arch_syscall_frame.h",
]

REQUIRED_ARM64_SCAFFOLD = [
    ROOT / "arch" / "arm64" / "linker.ld",
    ROOT / "arch" / "arm64" / "sources" / "boot_stub.c",
    ROOT / "arch" / "arm64" / "sources" / "mmu_early.c",
    ROOT / "arch" / "arm64" / "sources" / "exc_early.c",
    ROOT / "arch" / "arm64" / "sources" / "slice_hello.c",
    ROOT / "arch" / "arm64" / "sources" / "pl011.c",
    ROOT / "arch" / "arm64" / "sources" / "serial_io_arm64.c",
    ROOT / "arch" / "arm64" / "sources" / "mm_ops.c",
    ROOT / "arch" / "arm64" / "sources" / "gic_v2.c",
    ROOT / "arch" / "arm64" / "sources" / "syscall_early.c",
    ROOT / "arch" / "arm64" / "sources" / "timer.c",
    ROOT / "arch" / "arm64" / "sources" / "portable_string.c",
    ROOT / "arch" / "arm64" / "sources" / "vectors.S",
    ROOT / "arch" / "arm64" / "sources" / "arch_early.c",
    ROOT / "arch" / "arm64" / "sources" / "interrupts.c",
    ROOT / "arch" / "arm64" / "sources" / "syscall_stub.c",
]

DRIVER_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]drivers/')
FACADE_ARCH_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]arch/')
FACADE_SCHED_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]sched/')

# interrupt/* is for hardware backends; portable code uses arch_portable/facades.
INTERRUPT_ARCH_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]interrupt/arch/')

# fs/ pseudo-VFS layers must route CPU/HW probes through ir0/*.h wrappers.
FS_DIRECT_ARCH_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]arch/')

# mm/ and net/ must not pull arch headers directly — use ir0/arch_port.h, etc.
PORTABLE_MM_NET_ARCH_INCLUDE_RE = re.compile(
    r'^\s*#\s*include\s*[<"]arch/'
)

KERNEL_HEADER_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]kernel/')

DRIVER_BLOCK_DEV_RAW_INCLUDE_RE = re.compile(
    r'^\s*#\s*include\s*[<"]drivers/storage/block_dev\.h[>"]'
)

BLUETOOTH_SUBDIR_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]bluetooth/')

DRIVERS_ARCH_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]arch/')

KERNEL_DRIVER_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]drivers/')

KERNEL_ARCH_PORTABLE_DIRECT_RE = re.compile(
    r'^\s*#\s*include\s*[<"]arch/common/arch_portable\.h[>"]'
)

FS_MM_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]mm/')

DEVFS_USERCOPY_RE = re.compile(r"\bcopy_(to|from)_user\s*\(")
DEVFS_USERCOPY_WHITELIST = {
    "dev_audio_ioctl",
    "dev_console_ioctl",
    "dev_events0_ioctl",
    "dev_fb0_ioctl",
    "dev_mouse_ioctl",
    "dev_net_ioctl",
    "dev_pty_ioctl",
}

DIRS_BLUETOOTH_INCLUDE_SCAN = [
    ROOT / "arch",
    ROOT / "drivers",
    ROOT / "fs",
    ROOT / "includes",
    ROOT / "interrupt",
    ROOT / "kernel",
    ROOT / "mm",
    ROOT / "net",
]

PORTABLE_DIRS_NO_KERNEL_HEADERS = [
    ROOT / "fs",
    ROOT / "net",
    ROOT / "mm",
    ROOT / "drivers",
]

PORTABLE_DIRS_MM_NET_NO_ARCH = [
    ROOT / "mm",
    ROOT / "net",
    ROOT / "sched",
]

PORTABLE_DIRS_NO_INTERRUPT_ARCH = [
    ROOT / "fs",
    ROOT / "kernel",
    ROOT / "mm",
    ROOT / "net",
    ROOT / "drivers",
    ROOT / "sched",
]


def iter_c_files(base: Path):
    if base.is_file():
        yield base
        return
    for p in base.rglob("*"):
        if p.suffix in (".c", ".h"):
            yield p


def check_forbidden_includes():
    errors = []
    for target in FORBIDDEN_PATHS:
        for fpath in iter_c_files(target):
            try:
                lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            for idx, line in enumerate(lines, start=1):
                if DRIVER_INCLUDE_RE.search(line):
                    rel = fpath.relative_to(ROOT)
                    errors.append(f"[forbidden-include] {rel}:{idx}: {line.strip()}")
    return errors


def check_facades():
    errors = []
    for facade in REQUIRED_FACADES:
        if not facade.exists():
            rel = facade.relative_to(ROOT)
            errors.append(f"[missing-facade] {rel}")
    return errors


def check_arm64_scaffold():
    errors = []
    for p in REQUIRED_ARM64_SCAFFOLD:
        if not p.exists():
            rel = p.relative_to(ROOT)
            errors.append(f"[missing-arm64-scaffold] {rel}")
    return errors


def check_interrupt_arch_portable():
    errors = []
    for base in PORTABLE_DIRS_NO_INTERRUPT_ARCH:
        for fpath in iter_c_files(base):
            try:
                lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            for idx, line in enumerate(lines, start=1):
                if INTERRUPT_ARCH_INCLUDE_RE.search(line):
                    rel = fpath.relative_to(ROOT)
                    errors.append(
                        f"[portable-no-interrupt-arch] {rel}:{idx}: {line.strip()}"
                    )
    return errors


def check_fs_no_direct_arch():
    errors = []
    base = ROOT / "fs"
    for fpath in iter_c_files(base):
        try:
            lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
        except Exception as exc:
            errors.append(f"[read-error] {fpath}: {exc}")
            continue
        for idx, line in enumerate(lines, start=1):
            if FS_DIRECT_ARCH_INCLUDE_RE.search(line):
                rel = fpath.relative_to(ROOT)
                errors.append(f"[fs-no-direct-arch] {rel}:{idx}: {line.strip()}")
    return errors


def check_mm_net_no_arch_includes():
    errors = []
    for base in PORTABLE_DIRS_MM_NET_NO_ARCH:
        for fpath in iter_c_files(base):
            try:
                lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            for idx, line in enumerate(lines, start=1):
                if PORTABLE_MM_NET_ARCH_INCLUDE_RE.search(line):
                    rel = fpath.relative_to(ROOT)
                    tag = (
                        "[sched-no-arch-include]"
                        if base.name == "sched"
                        else "[mm-net-no-arch-include]"
                    )
                    errors.append(f"{tag} {rel}:{idx}: {line.strip()}")
    return errors


def check_facade_no_drivers_include():
    """ir0 facade *headers* must not pull drivers/arch/sched; .c adapters may."""
    errors = []
    base = ROOT / "includes" / "ir0"
    if not base.is_dir():
        return errors
    for fpath in base.rglob("*.h"):
        try:
            lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
        except Exception as exc:
            errors.append(f"[read-error] {fpath}: {exc}")
            continue
        for idx, line in enumerate(lines, start=1):
            rel = fpath.relative_to(ROOT)
            if DRIVER_INCLUDE_RE.search(line):
                errors.append(
                    f"[facade-no-drivers-include] {rel}:{idx}: {line.strip()}"
                )
            if FACADE_ARCH_INCLUDE_RE.search(line):
                errors.append(
                    f"[facade-no-arch-include] {rel}:{idx}: {line.strip()}"
                )
            if FACADE_SCHED_INCLUDE_RE.search(line):
                errors.append(
                    f"[facade-no-sched-include] {rel}:{idx}: {line.strip()}"
                )
    return errors


def check_portable_trees_no_kernel_headers():
    errors = []
    for base in PORTABLE_DIRS_NO_KERNEL_HEADERS:
        for fpath in iter_c_files(base):
            try:
                lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            for idx, line in enumerate(lines, start=1):
                if KERNEL_HEADER_INCLUDE_RE.search(line):
                    rel = fpath.relative_to(ROOT)
                    errors.append(
                        f"[portable-no-kernel-header] {rel}:{idx}: {line.strip()}"
                    )
    return errors


def check_drivers_ir0_block_dev_only():
    errors = []
    base = ROOT / "drivers"
    for fpath in iter_c_files(base):
        try:
            lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
        except Exception as exc:
            errors.append(f"[read-error] {fpath}: {exc}")
            continue
        for idx, line in enumerate(lines, start=1):
            if DRIVER_BLOCK_DEV_RAW_INCLUDE_RE.search(line):
                rel = fpath.relative_to(ROOT)
                errors.append(
                    f"[driver-block-dev-facade] {rel}:{idx}: {line.strip()}"
                )
    return errors


def check_drivers_no_arch_includes():
    errors = []
    base = ROOT / "drivers"
    for fpath in iter_c_files(base):
        try:
            lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
        except Exception as exc:
            errors.append(f"[read-error] {fpath}: {exc}")
            continue
        for idx, line in enumerate(lines, start=1):
            if DRIVERS_ARCH_INCLUDE_RE.search(line):
                rel = fpath.relative_to(ROOT)
                errors.append(f"[drivers-no-arch] {rel}:{idx}: {line.strip()}")
    return errors


def check_kernel_no_driver_includes():
    errors = []
    base = ROOT / "kernel"
    for fpath in iter_c_files(base):
        try:
            lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
        except Exception as exc:
            errors.append(f"[read-error] {fpath}: {exc}")
            continue
        for idx, line in enumerate(lines, start=1):
            if KERNEL_DRIVER_INCLUDE_RE.search(line):
                rel = fpath.relative_to(ROOT)
                errors.append(
                    f"[kernel-no-driver-include] {rel}:{idx}: {line.strip()}"
                )
    return errors


def check_kernel_no_direct_arch_portable():
    errors = []
    base = ROOT / "kernel"
    for fpath in iter_c_files(base):
        try:
            lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
        except Exception as exc:
            errors.append(f"[read-error] {fpath}: {exc}")
            continue
        for idx, line in enumerate(lines, start=1):
            if KERNEL_ARCH_PORTABLE_DIRECT_RE.search(line):
                rel = fpath.relative_to(ROOT)
                errors.append(
                    f"[kernel-use-arch-port-facade] {rel}:{idx}: {line.strip()}"
                )
    return errors


def check_fs_no_mm_includes():
    errors = []
    base = ROOT / "fs"
    for fpath in iter_c_files(base):
        try:
            lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
        except Exception as exc:
            errors.append(f"[read-error] {fpath}: {exc}")
            continue
        for idx, line in enumerate(lines, start=1):
            if FS_MM_INCLUDE_RE.search(line):
                rel = fpath.relative_to(ROOT)
                errors.append(f"[fs-no-mm-include] {rel}:{idx}: {line.strip()}")
    return errors


def check_bluetooth_subdir_include_policy():
    errors = []
    bt_root = ROOT / "drivers" / "bluetooth"

    for scan_root in DIRS_BLUETOOTH_INCLUDE_SCAN:
        if not scan_root.exists():
            continue
        for fpath in iter_c_files(scan_root):
            allowed_bluetooth_vendor = False
            try:
                fpath.relative_to(bt_root)
                allowed_bluetooth_vendor = True
            except ValueError:
                allowed_bluetooth_vendor = False

            # Allow subsystem-local includes inside drivers/bluetooth only.
            if allowed_bluetooth_vendor:
                continue

            try:
                lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue

            for idx, line in enumerate(lines, start=1):
                if BLUETOOTH_SUBDIR_INCLUDE_RE.search(line):
                    rel = fpath.relative_to(ROOT)
                    errors.append(
                        f"[bluetooth-include-scope] {rel}:{idx}: {line.strip()}"
                    )
    return errors


def check_devfs_usercopy_contract():
    errors = []
    devfs_path = ROOT / "fs" / "devfs.c"
    if not devfs_path.exists():
        return errors

    try:
        lines = devfs_path.read_text(encoding="utf-8", errors="replace").splitlines()
    except Exception as exc:
        errors.append(f"[read-error] {devfs_path}: {exc}")
        return errors

    current_fn = None
    fn_re = re.compile(r'^\s*(?:static\s+)?(?:inline\s+)?[A-Za-z_][A-Za-z0-9_\s\*]*\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(')

    for idx, line in enumerate(lines, start=1):
        # Skip `return foo(` — not a function definition (false positive).
        stripped = line.lstrip()
        m = fn_re.match(line)
        if m and not stripped.startswith("return"):
            name = m.group(1)
            if name not in ("if", "while", "for", "switch", "return", "sizeof"):
                current_fn = name
        if DEVFS_USERCOPY_RE.search(line):
            if current_fn not in DEVFS_USERCOPY_WHITELIST:
                rel = devfs_path.relative_to(ROOT)
                errors.append(
                    f"[devfs-io-contract-usercopy] {rel}:{idx}: {line.strip()} (fn={current_fn})"
                )
    return errors


def check_usercopy_no_raw_user_touch():
    """
    Forbid CR3-switch + raw memcpy/memset into (void *) user VAs, and
    sys_uname-style memset/strncpy into the syscall buffer argument.
    """
    errors = []
    fn_re = re.compile(
        r"^\s*(?:static\s+)?(?:inline\s+)?[A-Za-z_][A-Za-z0-9_\s\*]*\s+"
        r"([A-Za-z_][A-Za-z0-9_]*)\s*\("
    )
    cr3_memcpy_re = re.compile(r"memcpy\s*\(\s*\(void\s*\*\)")
    cr3_memset_re = re.compile(r"memset\s*\(\s*\(void\s*\*\)")
    load_cr3_re = re.compile(r"load_page_directory\s*\(")
    uname_memset_re = re.compile(r"memset\s*\(\s*buf\s*,")
    uname_strncpy_re = re.compile(r"strncpy\s*\(\s*buf\s*->")

    targets = [ROOT / "kernel" / "lib" / "signals.c"]
    syscalls = ROOT / "kernel" / "syscalls"
    if syscalls.is_dir():
        targets.extend(sorted(syscalls.glob("*.c")))

    for fpath in targets:
        if not fpath.is_file():
            continue
        try:
            lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
        except Exception as exc:
            errors.append(f"[read-error] {fpath}: {exc}")
            continue

        current_fn = None
        saw_load_cr3 = False
        rel = fpath.relative_to(ROOT)

        for idx, line in enumerate(lines, start=1):
            stripped = line.lstrip()
            if stripped.startswith("//") or stripped.startswith("/*"):
                continue
            m = fn_re.match(line)
            if m and not stripped.startswith("return"):
                name = m.group(1)
                if name not in ("if", "while", "for", "switch", "return", "sizeof"):
                    current_fn = name
                    saw_load_cr3 = False

            if load_cr3_re.search(line):
                saw_load_cr3 = True

            if saw_load_cr3 and (
                cr3_memcpy_re.search(line) or cr3_memset_re.search(line)
            ):
                errors.append(
                    f"[usercopy-no-cr3-memcpy] {rel}:{idx}: "
                    f"load_page_directory + raw mem* in {current_fn}; "
                    f"use copy_to_user / *_region_in_directory"
                )

            if current_fn == "sys_uname" and (
                uname_memset_re.search(line) or uname_strncpy_re.search(line)
            ):
                errors.append(
                    f"[usercopy-sys-uname] {rel}:{idx}: "
                    f"write utsname via kernel bounce + copy_to_user"
                )

            # signals.c: any memcpy((void *) is the known KERNEL_UACCESS pattern
            if fpath.name == "signals.c" and cr3_memcpy_re.search(line):
                errors.append(
                    f"[usercopy-signals] {rel}:{idx}: "
                    f"memcpy((void *) forbidden; use copy_to_user_region_in_directory"
                )

    return errors


def check_ktm_core_no_fase():
    """KTM v1 core must not embed legacy FASE diagnostics."""
    errors = []
    roots = [
        ROOT / "includes" / "ir0" / "ktm",
        ROOT / "ktm" / "event_ring.c",
        ROOT / "ktm" / "transport_serial.c",
        ROOT / "ktm" / "registry.c",
        ROOT / "ktm" / "snapshot.c",
        ROOT / "ktm" / "assert.c",
        ROOT / "ktm" / "checkpoint.c",
        ROOT / "ktm" / "fault.c",
        ROOT / "ktm" / "scenario.c",
        ROOT / "ktm" / "invariant_global.c",
        ROOT / "ktm" / "scenarios",
        ROOT / "ktm" / "userdev.c",
    ]
    fase_re = re.compile(r"FASE[0-9]|\[FASE")
    for base in roots:
        for fpath in iter_c_files(base):
            try:
                text = fpath.read_text(encoding="utf-8", errors="replace")
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            if fase_re.search(text):
                rel = fpath.relative_to(ROOT)
                errors.append(f"[ktm-no-fase] {rel}: FASE markers forbidden in KTM core")
    return errors


def check_ktm_no_fase_serial():
    """Forbid any [FASE serial markers in kernel trees (KTM is sole source of truth)."""
    errors = []
    fase_re = re.compile(r"\[FASE")
    scan_roots = [
        ROOT / "kernel",
        ROOT / "mm",
        ROOT / "fs",
        ROOT / "drivers",
        ROOT / "includes" / "ir0",
        ROOT / "ktm",
        ROOT / "arch",
        ROOT / "sched",
    ]
    for base in scan_roots:
        if not base.exists():
            continue
        for fpath in iter_c_files(base):
            rel = str(fpath.relative_to(ROOT)).replace("\\", "/")
            try:
                text = fpath.read_text(encoding="utf-8", errors="replace")
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            if fase_re.search(text):
                errors.append(
                    f"[ktm-no-fase] {rel}: [FASE serial forbidden; use KTM checkpoints/events"
                )
    return errors


def check_klog_serial_print_allowlist():
    """
    Human logging must go through KTM hub (ktm/klog.c / kprintf / klog_*).
    serial_print is only allowed in the sink, serial drivers, KTM protocol
    transport, and remaining gated diag TUs (shrinking allowlist).
    """
    errors = []
    allow = {
        "ktm/klog.c",
        "ktm/transport_serial.c",
        "drivers/serial/serial.c",
        "drivers/serial/serial.h",
        "includes/ir0/serial_io.h",
        "arch/arm64/sources/serial_io_arm64.c",
        "arch/arm64/sources/min_link_stubs.c",
        "tests/host/host_serial_stub.c",
        # Freestanding early boot (IR0_FREESTANDING_BOOT) before klog hub exists.
        "arch/common/boot_log.c",
    }
    serial_re = re.compile(r"\bserial_print\s*\(")
    scan_roots = [
        ROOT / "kernel",
        ROOT / "mm",
        ROOT / "fs",
        ROOT / "drivers",
        ROOT / "includes" / "ir0",
        ROOT / "ktm",
        ROOT / "arch",
        ROOT / "sched",
        ROOT / "interrupt",
    ]
    for base in scan_roots:
        if not base.exists():
            continue
        for fpath in iter_c_files(base):
            rel = str(fpath.relative_to(ROOT)).replace("\\", "/")
            if rel in allow:
                continue
            if "/third-party/" in rel:
                continue
            try:
                text = fpath.read_text(encoding="utf-8", errors="replace")
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            for idx, line in enumerate(text.splitlines(), 1):
                if _line_is_comment_only(line):
                    continue
                if serial_re.search(line):
                    errors.append(
                        f"[klog-hub] {rel}:{idx}: use kprintf/klog_* "
                        f"(serial_print only in ktm/klog.c + serial/KTM protocol)"
                    )
    return errors


def check_ktm_angle_includes():
    """KTM sources must use <ktm_…> / <ir0/ktm/…>, not relative or quoted ktm paths."""
    errors = []
    bad_re = re.compile(
        r'^\s*#\s*include\s+("(\.\./)+.*ktm[^"]*"|'
        r'"ktm_internal\.h"|'
        r'"\.\./ktm_internal\.h"|'
        r'<(\.\./)+.*ktm[^>]*>)'
    )
    # Also catch facade relative include of ktm/include
    facade_rel_re = re.compile(
        r'^\s*#\s*include\s+"(\.\./)+ktm/'
    )
    scan = [
        ROOT / "ktm",
        ROOT / "includes" / "ir0" / "ktm.h",
        ROOT / "includes" / "ir0" / "ktm",
    ]
    for base in scan:
        for fpath in iter_c_files(base):
            try:
                lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            rel = fpath.relative_to(ROOT)
            for idx, line in enumerate(lines, 1):
                if bad_re.search(line) or facade_rel_re.search(line):
                    errors.append(
                        f"[ktm-include] {rel}:{idx}: use <ktm_…> or <ir0/ktm/…> "
                        f"(no relative/quoted ktm paths): {line.strip()}"
                    )
    return errors


def check_kernel_no_relative_includes():
    """kernel/syscalls and kernel/process must not use #include \"../…\"."""
    errors = []
    bad_re = re.compile(r'^\s*#\s*include\s+"\.\./')
    for base in (ROOT / "kernel" / "syscalls", ROOT / "kernel" / "process"):
        for fpath in iter_c_files(base):
            try:
                lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            rel = fpath.relative_to(ROOT)
            for idx, line in enumerate(lines, 1):
                if bad_re.search(line):
                    errors.append(
                        f"[kernel-include] {rel}:{idx}: use <kernel/…> "
                        f"(no relative includes): {line.strip()}"
                    )
    return errors


# Portable C must not embed ISA asm — facades only (simple names in ir0/cpu.h).
# Allow: empty compiler barrier asm volatile("" ::: "memory")
# Allowlist: per-ISA userspace syscall stubs (selected by build, not portable C).
PORTABLE_ISA_ASM_RE = re.compile(
    r"(?:__asm__|asm)\s*(?:volatile)?\s*\([^;]*"
    r"(?:pause|hlt|cli|sti|rdtsc|mfence|lfence|sfence|cpuid|rdmsr|wrmsr|"
    r"invlpg|pushfq|lidt|wfi|yield|dmb|dsb|isb|"
    r"in[bwl]\b|out[bwl]\b|"
    r"%%cr[0-4]|%%dr[0-7]|%%rax|movq\s+%%|"
    r"svc\s+#|syscall\b|sysret|sysenter|sysexit|"
    r"mrs\b|msr\b)",
    re.IGNORECASE,
)
PORTABLE_ISA_TREES = (
    ROOT / "kernel",
    ROOT / "net",
    ROOT / "includes" / "ir0",
    ROOT / "mm",
    ROOT / "sched",
    ROOT / "drivers",
    ROOT / "fs",
)
PORTABLE_ISA_ALLOWLIST = {
    ROOT / "includes" / "ir0" / "syscall_x86_64.h",
    ROOT / "includes" / "ir0" / "syscall_arm64.h",
}


def _line_is_comment_only(line: str) -> bool:
    s = line.strip()
    return (
        not s
        or s.startswith("//")
        or s.startswith("/*")
        or s.startswith("*")
        or s.startswith("#")
    )


def _is_empty_compiler_barrier(line: str) -> bool:
    """Permit asm volatile(\"\" ::: \"memory\") — not ISA-specific."""
    s = re.sub(r"\s+", "", line)
    return bool(
        re.search(
            r'(?:__asm__|asm)(?:volatile)?\(""(?:::+"memory")?\);?',
            s,
            re.IGNORECASE,
        )
    )


def check_portable_no_isa_asm():
    """Fail if portable trees embed ISA mnemonics; use cpu_relax/smp_mb/…"""
    errors = []
    for base in PORTABLE_ISA_TREES:
        if not base.is_dir():
            continue
        for fpath in list(iter_c_files(base)) + list(base.rglob("*.h")):
            if fpath in PORTABLE_ISA_ALLOWLIST:
                continue
            try:
                rel = fpath.relative_to(ROOT)
            except ValueError:
                continue
            rel_s = str(rel).replace("\\", "/")
            if "arch/" in rel_s:
                continue
            if fpath.suffix not in (".c", ".h"):
                continue
            try:
                lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            for idx, line in enumerate(lines, 1):
                if _line_is_comment_only(line):
                    continue
                if _is_empty_compiler_barrier(line):
                    continue
                if PORTABLE_ISA_ASM_RE.search(line):
                    errors.append(
                        f"[portable-no-isa-asm] {rel}:{idx}: use simple "
                        f"facades (cpu_relax/smp_mb/inb/timer_read/…); "
                        f"no ISA asm: {line.strip()}"
                    )
    return errors


# Hardcoded musl/BusyBox RIPs from historical fork bugs — forbidden in product.
PORTABLE_HARDCODED_RIP_RE = re.compile(r"\b0x402[Bb][0-9A-Fa-f]+\b")
# Direct CR2 / iretq / sysret in executable code (not comments).
PORTABLE_CR2_RE = re.compile(r"\b(?:read_cr2|__read_cr2|get_cr2)\s*\(|\bmov\s+.*,\s*cr2\b", re.I)
PORTABLE_IRETQ_SYSRET_ASM_RE = re.compile(
    r'(?:__asm__|asm)\b[^;]*\b(?:iretq|sysret)\b', re.I
)
PORTABLE_X86_STATUS_RE = re.compile(
    r"\b(?:RFLAGS_IF|ir0_rflags_sanitize_user)\b"
)
PORTABLE_X86_STATUS_ALLOWLIST = {
    ROOT / "kernel" / "lib" / "debug_trap.c",
    ROOT / "includes" / "ir0" / "debug_trap.h",
}


def check_portable_no_isa_leak_literals():
    """Ban hardcoded userspace RIPs and CR2/iretq/sysret outside arch/."""
    errors = []
    trees = list(PORTABLE_ISA_TREES) + [ROOT / "sched", ROOT / "includes" / "ir0"]
    for base in trees:
        if not base.is_dir():
            continue
        for fpath in list(iter_c_files(base)) + list(base.rglob("*.h")):
            try:
                rel = fpath.relative_to(ROOT)
            except ValueError:
                continue
            rel_s = str(rel).replace("\\", "/")
            if rel_s.startswith("arch/"):
                continue
            if fpath.suffix not in (".c", ".h"):
                continue
            try:
                lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            for idx, line in enumerate(lines, 1):
                if _line_is_comment_only(line):
                    continue
                if PORTABLE_HARDCODED_RIP_RE.search(line):
                    errors.append(
                        f"[portable-no-hardcoded-rip] {rel}:{idx}: "
                        f"workload RIP forbidden; use KTM/live IP: {line.strip()}"
                    )
                if PORTABLE_CR2_RE.search(line):
                    errors.append(
                        f"[portable-no-cr2] {rel}:{idx}: use "
                        f"page_fault_decode(); no CR2 in portable code: "
                        f"{line.strip()}"
                    )
                if PORTABLE_IRETQ_SYSRET_ASM_RE.search(line):
                    errors.append(
                        f"[portable-no-iretq-sysret] {rel}:{idx}: iretq/sysret "
                        f"belong in arch/; use facades: {line.strip()}"
                    )
                if (fpath not in PORTABLE_X86_STATUS_ALLOWLIST and
                        PORTABLE_X86_STATUS_RE.search(line)):
                    errors.append(
                        f"[portable-no-x86-status] {rel}:{idx}: use the "
                        f"task_ops status facade; no x86 RFLAGS in portable "
                        f"code: {line.strip()}"
                    )
    return errors


# Direct GPR / control-register fields on task.arch — portable code must use
# includes/ir0/arch_task.h accessors (or arch_task_ops bulk helpers).
PORTABLE_TASK_ARCH_FIELD_RE = re.compile(
    r"(?:\.|->)arch\.(rax|rbx|rcx|rdx|rsi|rdi|rbp|rsp|r8|r9|r10|r11|r12|r13|r14|r15|"
    r"rip|rflags|cs|ss|ds|es|fs|gs|cr0|cr2|cr3|cr4|dr0|dr1|dr2|dr3|dr6|dr7|"
    r"x0|x1|x2|ttbr0_el1|elr_el1|sp_el0|spsr_el1)\b"
)

PORTABLE_TASK_ARCH_ALLOWLIST = {
    ROOT / "includes" / "ir0" / "arch_task.h",
    ROOT / "includes" / "ir0" / "arch_task_context_x86_64.h",
    ROOT / "includes" / "ir0" / "arch_task_context_arm64.h",
    ROOT / "includes" / "ir0" / "arch_task_ops.h",
}


def check_asm_offsets_sync():
    """C asm_offsets.h constants must match NASM asm_offsets.inc."""
    errors = []
    h_path = ROOT / "includes" / "ir0" / "asm_offsets.h"
    i_path = ROOT / "includes" / "ir0" / "asm_offsets.inc"
    if not h_path.is_file() or not i_path.is_file():
        errors.append("[asm-offsets] missing asm_offsets.h or asm_offsets.inc")
        return errors
    h = h_path.read_text(encoding="utf-8", errors="replace")
    inc = i_path.read_text(encoding="utf-8", errors="replace")
    pairs = [
        ("IR0_PROC_FS_BASE_OFFSET", "PROC_FS_BASE_OFFSET"),
        ("IR0_TASK_ARCH_RIP_OFFSET", "TASK_ARCH_RIP_OFFSET"),
        ("IR0_TASK_ARCH_CR3_OFFSET", "TASK_ARCH_CR3_OFFSET"),
        ("IR0_TASK_ARCH_SS_OFFSET", "TASK_ARCH_SS_OFFSET"),
    ]
    for c_name, asm_name in pairs:
        hm = re.search(rf"#define\s+{c_name}\s+(0x[0-9A-Fa-f]+|\d+)", h)
        im = re.search(rf"%define\s+{asm_name}\s+(0x[0-9A-Fa-f]+|\d+)", inc)
        if not hm or not im:
            errors.append(f"[asm-offsets] missing {c_name} / {asm_name}")
            continue
        if int(hm.group(1), 0) != int(im.group(1), 0):
            errors.append(
                f"[asm-offsets] mismatch {c_name}={hm.group(1)} vs "
                f"{asm_name}={im.group(1)}"
            )
    return errors


def check_portable_no_task_arch_fields():
    """Fail if portable trees touch task.arch.<isa-field> directly."""
    errors = []
    trees = list(PORTABLE_ISA_TREES) + [ROOT / "sched", ROOT / "includes" / "ir0"]
    for base in trees:
        if not base.is_dir():
            continue
        for fpath in list(iter_c_files(base)) + list(base.rglob("*.h")):
            if fpath in PORTABLE_TASK_ARCH_ALLOWLIST:
                continue
            try:
                rel = fpath.relative_to(ROOT)
            except ValueError:
                continue
            rel_s = str(rel).replace("\\", "/")
            if rel_s.startswith("arch/"):
                continue
            # ISA dispatchers under sched/switch may still need allowlist if any
            if rel_s.startswith("sched/switch/switch_"):
                continue
            if fpath.suffix not in (".c", ".h"):
                continue
            try:
                lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            for idx, line in enumerate(lines, 1):
                if _line_is_comment_only(line):
                    continue
                if PORTABLE_TASK_ARCH_FIELD_RE.search(line):
                    errors.append(
                        f"[portable-no-task-arch-field] {rel}:{idx}: use "
                        f"task_get_*/task_set_* or arch_task_ops; "
                        f"no direct task.arch field: {line.strip()}"
                    )
    return errors


# process_t.page_directory is a private mirror; mm->page_directory is canonical.
PROCESS_PGD_FIELD_RE = re.compile(
    r"(?<![.\w])(?!mm\b)([A-Za-z_][A-Za-z0-9_]*)->page_directory\b"
)

PROCESS_PGD_ALLOWLIST = {
    ROOT / "kernel" / "process.h",
    ROOT / "kernel" / "process" / "mm_struct.c",
    ROOT / "includes" / "ir0" / "mm_struct.h",
}


def check_process_pgd_accessor():
    """Fail if code touches process->page_directory outside mm bind helpers."""
    errors = []
    trees = [
        ROOT / "kernel",
        ROOT / "mm",
        ROOT / "fs",
        ROOT / "net",
        ROOT / "sched",
        ROOT / "includes" / "ir0",
        ROOT / "ktm",
        ROOT / "interrupt",
        ROOT / "drivers",
    ]
    for base in trees:
        if not base.is_dir():
            continue
        for fpath in list(iter_c_files(base)) + list(base.rglob("*.h")):
            if fpath in PROCESS_PGD_ALLOWLIST:
                continue
            if fpath.suffix not in (".c", ".h"):
                continue
            try:
                rel = fpath.relative_to(ROOT)
            except ValueError:
                continue
            try:
                lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            for idx, line in enumerate(lines, 1):
                if _line_is_comment_only(line):
                    continue
                if PROCESS_PGD_FIELD_RE.search(line):
                    errors.append(
                        f"[process-pgd-accessor] {rel}:{idx}: use "
                        f"process_pgd()/process_set_pgd(); "
                        f"no direct process->page_directory: {line.strip()}"
                    )
    return errors


PROCESS_SAVED_CONTEXT_RE = re.compile(
    r"(?<![.\w])([A-Za-z_][A-Za-z0-9_]*)->saved_context\b"
)

PROCESS_SAVED_CONTEXT_ALLOWLIST = {
    ROOT / "kernel" / "process.h",
    ROOT / "kernel" / "process" / "saved_context.c",
    ROOT / "kernel" / "process" / "create.c",
    ROOT / "kernel" / "process" / "domains.c",
}


def check_process_saved_context_accessor():
    """Fail if code touches process->saved_context outside signal/process paths."""
    errors = []
    trees = [
        ROOT / "kernel",
        ROOT / "mm",
        ROOT / "fs",
        ROOT / "net",
        ROOT / "sched",
        ROOT / "includes" / "ir0",
        ROOT / "ktm",
        ROOT / "interrupt",
        ROOT / "drivers",
    ]
    for base in trees:
        if not base.is_dir():
            continue
        for fpath in list(iter_c_files(base)) + list(base.rglob("*.h")):
            if fpath in PROCESS_SAVED_CONTEXT_ALLOWLIST:
                continue
            if fpath.suffix not in (".c", ".h"):
                continue
            try:
                rel = fpath.relative_to(ROOT)
            except ValueError:
                continue
            try:
                lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            for idx, line in enumerate(lines, 1):
                if _line_is_comment_only(line):
                    continue
                if PROCESS_SAVED_CONTEXT_RE.search(line):
                    errors.append(
                        f"[process-saved-context-accessor] {rel}:{idx}: "
                        f"no direct process->saved_context "
                        f"(use signal/process helpers): {line.strip()}"
                    )
    return errors


PROCESS_SIGNAL_ENTER_RE = re.compile(
    r"(?<![.\w])([A-Za-z_][A-Za-z0-9_]*)->signal_enter_pending\b"
)

PROCESS_SIGNAL_ENTER_ALLOWLIST = {
    ROOT / "kernel" / "process.h",
    ROOT / "kernel" / "process" / "signal_enter.c",
    ROOT / "kernel" / "process" / "create.c",
    ROOT / "kernel" / "process" / "domains.c",
}


def check_process_signal_enter_accessor():
    """Fail if code touches process->signal_enter_pending outside signal paths."""
    errors = []
    trees = [
        ROOT / "kernel",
        ROOT / "mm",
        ROOT / "fs",
        ROOT / "net",
        ROOT / "sched",
        ROOT / "includes" / "ir0",
        ROOT / "ktm",
        ROOT / "interrupt",
        ROOT / "drivers",
    ]
    for base in trees:
        if not base.is_dir():
            continue
        for fpath in list(iter_c_files(base)) + list(base.rglob("*.h")):
            if fpath in PROCESS_SIGNAL_ENTER_ALLOWLIST:
                continue
            if fpath.suffix not in (".c", ".h"):
                continue
            try:
                rel = fpath.relative_to(ROOT)
            except ValueError:
                continue
            try:
                lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            for idx, line in enumerate(lines, 1):
                if _line_is_comment_only(line):
                    continue
                if PROCESS_SIGNAL_ENTER_RE.search(line):
                    errors.append(
                        f"[process-signal-enter-accessor] {rel}:{idx}: "
                        f"no direct process->signal_enter_pending "
                        f"(use process_signal_enter_pending_*): {line.strip()}"
                    )
    return errors


PROCESS_SIGNAL_DEFER_RE = re.compile(
    r"(?<![.\w])([A-Za-z_][A-Za-z0-9_]*)->signal_defer_catchable\b"
)

PROCESS_SIGNAL_DEFER_ALLOWLIST = {
    ROOT / "kernel" / "process.h",
    ROOT / "kernel" / "process" / "signal_enter.c",
    ROOT / "kernel" / "process" / "create.c",
    ROOT / "kernel" / "process" / "domains.c",
    ROOT / "kernel" / "process" / "exit.c",
}


def check_process_signal_defer_accessor():
    """Fail if code touches process->signal_defer_catchable outside signal paths."""
    errors = []
    trees = [
        ROOT / "kernel",
        ROOT / "mm",
        ROOT / "fs",
        ROOT / "net",
        ROOT / "sched",
        ROOT / "includes" / "ir0",
        ROOT / "ktm",
        ROOT / "interrupt",
        ROOT / "drivers",
    ]
    for base in trees:
        if not base.is_dir():
            continue
        for fpath in list(iter_c_files(base)) + list(base.rglob("*.h")):
            if fpath in PROCESS_SIGNAL_DEFER_ALLOWLIST:
                continue
            if fpath.suffix not in (".c", ".h"):
                continue
            try:
                rel = fpath.relative_to(ROOT)
            except ValueError:
                continue
            try:
                lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            for idx, line in enumerate(lines, 1):
                if _line_is_comment_only(line):
                    continue
                if PROCESS_SIGNAL_DEFER_RE.search(line):
                    errors.append(
                        f"[process-signal-defer-accessor] {rel}:{idx}: "
                        f"no direct process->signal_defer_catchable "
                        f"(use process_signal_defer_catchable_*): {line.strip()}"
                    )
    return errors


PROCESS_WAIT_FIELD_RE = re.compile(
    r"(?<![.\w])([A-Za-z_][A-Za-z0-9_]*)->"
    r"(wait_blocked|wait_target_pid|wait_options|wait_status_ptr|"
    r"wait_resume_child_pid)\b"
)

PROCESS_WAIT_FIELD_ALLOWLIST = {
    ROOT / "kernel" / "process.h",
    ROOT / "kernel" / "process" / "wait_state.c",
}


def check_process_wait_state_accessor():
    """Fail if portable code touches process wait4 block fields directly."""
    errors = []
    trees = [
        ROOT / "kernel",
        ROOT / "mm",
        ROOT / "fs",
        ROOT / "net",
        ROOT / "sched",
        ROOT / "includes" / "ir0",
        ROOT / "ktm",
        ROOT / "interrupt",
        ROOT / "drivers",
    ]
    for base in trees:
        if not base.is_dir():
            continue
        for fpath in list(iter_c_files(base)) + list(base.rglob("*.h")):
            if fpath in PROCESS_WAIT_FIELD_ALLOWLIST:
                continue
            if fpath.suffix not in (".c", ".h"):
                continue
            try:
                rel = fpath.relative_to(ROOT)
            except ValueError:
                continue
            try:
                lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            for idx, line in enumerate(lines, 1):
                if _line_is_comment_only(line):
                    continue
                if PROCESS_WAIT_FIELD_RE.search(line):
                    errors.append(
                        f"[process-wait-state-accessor] {rel}:{idx}: "
                        f"use process_wait_* helpers; "
                        f"no direct wait field: {line.strip()}"
                    )
    return errors


# syscall_user_frame_t fields — portable code uses process_syscall_* accessors.
SYSCALL_FRAME_FIELD_RE = re.compile(
    r"syscall_frame\.(rip|rsp|rflags|rdi|rsi|rdx|r10|r8|r9|rbx|rbp|r12|r13|r14|r15|"
    r"elr|spsr|sp|x0|x1|x2|x3|x4|x5)\b"
)

SYSCALL_FRAME_ALLOWLIST = {
    ROOT / "includes" / "ir0" / "arch_syscall_frame_x86_64.h",
    ROOT / "includes" / "ir0" / "arch_syscall_frame_arm64.h",
}


def check_syscall_frame_accessor():
    """Fail if portable code opens syscall_frame.* fields directly."""
    errors = []
    trees = [
        ROOT / "kernel",
        ROOT / "mm",
        ROOT / "fs",
        ROOT / "net",
        ROOT / "sched",
        ROOT / "includes" / "ir0",
        ROOT / "ktm",
    ]
    for base in trees:
        if not base.is_dir():
            continue
        for fpath in list(iter_c_files(base)) + list(base.rglob("*.h")):
            if fpath in SYSCALL_FRAME_ALLOWLIST:
                continue
            try:
                rel = fpath.relative_to(ROOT)
            except ValueError:
                continue
            rel_s = str(rel).replace("\\", "/")
            if rel_s.startswith("arch/"):
                continue
            if fpath.suffix not in (".c", ".h"):
                continue
            try:
                lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            for idx, line in enumerate(lines, 1):
                if _line_is_comment_only(line):
                    continue
                if SYSCALL_FRAME_FIELD_RE.search(line):
                    errors.append(
                        f"[syscall-frame-accessor] {rel}:{idx}: use "
                        f"process_syscall_ip/sp/arg/set_*; "
                        f"no direct syscall_frame field: {line.strip()}"
                    )
    return errors


def check_subsystems_json_paths():
    """Fail if subsystems.json names sources or subsystems that do not exist.

    The Kconfig generator only warns on a stale path and then emits its object
    anyway, so rot here surfaced as a link error or as a menu entry that builds
    nothing. Seven paths had already gone stale before this check existed.
    """
    errors = []
    manifest = ROOT / "scripts" / "kconfig" / "subsystems.json"
    if not manifest.is_file():
        return errors
    try:
        data = json.loads(manifest.read_text(encoding="utf-8"))
    except Exception as exc:
        return [f"[subsystems-json] {manifest}: unreadable: {exc}"]

    subsystems = data.get("subsystems", {})
    for sid, entry in subsystems.items():
        for arch, files in (entry.get("files") or {}).items():
            for rel in files:
                if not (ROOT / rel).is_file():
                    errors.append(
                        f"[subsystems-json] {sid}[{arch}]: missing source {rel}"
                    )
        for dep in entry.get("dependencies") or []:
            if dep not in subsystems:
                errors.append(
                    f"[subsystems-json] {sid}: depends on undefined subsystem {dep}"
                )

    # Profiles and the layer diagram may not offer a subsystem that is gone.
    for pid, profile in (data.get("profiles") or {}).items():
        for sid in profile.get("subsystems") or []:
            if sid not in subsystems:
                errors.append(
                    f"[subsystems-json] profile {pid}: undefined subsystem {sid}"
                )
    for layer in (data.get("architecture") or {}).get("layers") or []:
        for sid in layer.get("subsystems") or []:
            if sid not in subsystems:
                errors.append(
                    f"[subsystems-json] layer {layer.get('name')}: "
                    f"undefined subsystem {sid}"
                )
    return errors


KERNEL_LIB_I1_SOURCES = (
    "open_flags.c",
    "stat_user.c",
    "path_user.c",
    "utimens.c",
    "exec_read_trace.c",
    "mm_port.c",
    "pipe.c",
    "copy_user.c",
    "oops.c",
    "signals.c",
    "named_fifo.c",
)


def check_includes_ir0_no_c_sources():
    """includes/ir0/ must not host .c implementations (kernel/lib/ only)."""
    errors = []
    inc = ROOT / "includes" / "ir0"
    if not inc.is_dir():
        return errors
    for fpath in inc.rglob("*.c"):
        errors.append(
            f"[includes-migration] {fpath.relative_to(ROOT)}: "
            f"move implementation to kernel/lib/ or subsystem tree"
        )
    return errors


ARCH_PUBLIC_CALL_RE = re.compile(r"\barch_[a-z_][a-z0-9_]*\s*\(")
PORTABLE_ARCH_CALL_ALLOW = {
    "sched/switch/arch_context_switch.c": {
        "arch_switch_to",
    },
}

ARCH_SWITCH_INCLUDE_ALLOW = {
    "sched/switch/arch_context_switch.c",
    "arch/x86-64/sources/arch_switch.c",
    "arch/arm64/sources/arch_switch.c",
}

PORTABLE_NO_ARCH_CALL_TREES = [
    ROOT / "mm",
    ROOT / "kernel",
    ROOT / "fs",
    ROOT / "net",
    ROOT / "sched",
    ROOT / "interrupt",
]


def check_portable_no_arch_prefix_calls():
    """Portable code must not call arch_*(); use simple facades (PORT-2..4)."""
    errors = []
    for base in PORTABLE_NO_ARCH_CALL_TREES:
        if not base.is_dir():
            continue
        for fpath in iter_c_files(base):
            try:
                rel = fpath.relative_to(ROOT)
                rel_s = str(rel).replace("\\", "/")
            except ValueError:
                continue
            allow = PORTABLE_ARCH_CALL_ALLOW.get(rel_s, set())
            try:
                lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            for idx, line in enumerate(lines, 1):
                if _line_is_comment_only(line):
                    continue
                for m in ARCH_PUBLIC_CALL_RE.finditer(line):
                    name = m.group(0).split("(")[0].strip()
                    if name in allow:
                        continue
                    errors.append(
                        f"[portable-no-arch-call] {rel_s}:{idx}: "
                        f"use simple facade, not {name}()"
                    )
    return errors


ARCH_SWITCH_INCLUDE_RE = re.compile(
    r'#\s*include\s*[<"]ir0/arch_switch\.h[>"]'
)


def check_portable_no_arch_switch_include():
    """arch_switch.h is ISA-private; portable code uses context.h / switch_to()."""
    errors = []
    for base in PORTABLE_NO_ARCH_CALL_TREES:
        if not base.is_dir():
            continue
        for fpath in iter_c_files(base):
            try:
                rel = fpath.relative_to(ROOT)
                rel_s = str(rel).replace("\\", "/")
            except ValueError:
                continue
            if rel_s in ARCH_SWITCH_INCLUDE_ALLOW:
                continue
            try:
                lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            for idx, line in enumerate(lines, 1):
                if _line_is_comment_only(line):
                    continue
                if ARCH_SWITCH_INCLUDE_RE.search(line):
                    errors.append(
                        f"[portable-no-arch-switch-include] {rel_s}:{idx}: "
                        f"use <ir0/context.h> / switch_to(), not arch_switch.h"
                    )
    return errors


def check_kernel_lib_i1_migration():
    """kernel/lib holds migrated facade implementations; none left under includes/ir0/."""
    errors = []
    lib_dir = ROOT / "kernel" / "lib"
    inc_dir = ROOT / "includes" / "ir0"
    if not lib_dir.is_dir():
        errors.append("[includes-migration] missing kernel/lib/")
        return errors
    for fpath in sorted(lib_dir.glob("*.c")):
        name = fpath.name
        if (inc_dir / name).is_file():
            errors.append(
                f"[includes-migration] duplicate {inc_dir / name}; "
                f"remove includes/ir0 copy"
            )
    return errors


ARCH_MM_LEGACY_RE = re.compile(
    r"\barch_mm_(?:user_root_slots|root_slots|copy_kernel_half)\s*\("
)
ARCH_PF_LEGACY_RE = re.compile(
    r"\barch_page_fault_(?:decode|info)\b|\bstruct arch_page_fault_info\b"
)
PORTABLE_PORT1_TREES = [
    ROOT / "mm",
    ROOT / "kernel" / "process",
    ROOT / "fs",
    ROOT / "net",
    ROOT / "sched",
]


def check_portable_port1_no_legacy_arch_mm():
    """PORT-1: portable code uses mm_* / page_fault_* facades, not arch_mm_* / arch_page_fault_*."""
    errors = []
    for base in PORTABLE_PORT1_TREES:
        if not base.is_dir():
            continue
        for fpath in iter_c_files(base):
            try:
                rel = fpath.relative_to(ROOT)
            except ValueError:
                continue
            try:
                text = fpath.read_text(encoding="utf-8", errors="replace")
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            if ARCH_MM_LEGACY_RE.search(text):
                errors.append(
                    f"[portable-port1-mm] {rel}: use mm_user_root_slots / "
                    f"mm_copy_kernel_half (arch_mm.h)"
                )
            if ARCH_PF_LEGACY_RE.search(text):
                errors.append(
                    f"[portable-port1-pf] {rel}: use page_fault_decode / "
                    f"struct page_fault_info (arch_page_fault.h)"
                )
    return errors


def check_scheduler_user_return_boundary():
    """Keep raw IRQ frames and signal delivery out of portable scheduling."""
    errors = []
    sched_header = ROOT / "includes" / "ir0" / "sched.h"
    sched_tree = ROOT / "sched"
    irq_dispatch = ROOT / "interrupt" / "arch" / "isr_handlers.c"

    header_text = sched_header.read_text(encoding="utf-8", errors="replace")
    for forbidden in (
        "sched_irq_preempt_from_frame",
        "sched_context_switch_skip_prev_save",
        "sched_context_switch_take_skip_prev_save",
    ):
        if forbidden in header_text:
            errors.append(
                f"{sched_header.relative_to(ROOT)}: raw IRQ/context API "
                f"'{forbidden}' must remain ISA-private"
            )

    for path in sched_tree.rglob("*"):
        if path.suffix not in {".c", ".h"}:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        if re.search(r"\bhandle_signals\s*\(", text):
            errors.append(
                f"{path.relative_to(ROOT)}: scheduler must not deliver signals; "
                "use the exit-to-user facade"
            )

    irq_text = irq_dispatch.read_text(encoding="utf-8", errors="replace")
    if "#include <ir0/sched.h>" in irq_text:
        errors.append(
            f"{irq_dispatch.relative_to(ROOT)}: IRQ backend must not include "
            "the scheduler facade"
        )
    if re.search(r"\bsched_[A-Za-z0-9_]*\s*\(", irq_text):
        errors.append(
            f"{irq_dispatch.relative_to(ROOT)}: IRQ backend must not invoke "
            "portable scheduler entry points"
        )

    return errors


def main():
    errors = []
    errors.extend(check_forbidden_includes())
    errors.extend(check_facades())
    errors.extend(check_facade_no_drivers_include())
    errors.extend(check_arm64_scaffold())
    errors.extend(check_interrupt_arch_portable())
    errors.extend(check_fs_no_direct_arch())
    errors.extend(check_mm_net_no_arch_includes())
    errors.extend(check_portable_trees_no_kernel_headers())
    errors.extend(check_drivers_ir0_block_dev_only())
    errors.extend(check_drivers_no_arch_includes())
    errors.extend(check_kernel_no_driver_includes())
    errors.extend(check_kernel_no_direct_arch_portable())
    errors.extend(check_fs_no_mm_includes())
    errors.extend(check_bluetooth_subdir_include_policy())
    errors.extend(check_devfs_usercopy_contract())
    errors.extend(check_usercopy_no_raw_user_touch())
    errors.extend(check_ktm_core_no_fase())
    errors.extend(check_ktm_no_fase_serial())
    errors.extend(check_klog_serial_print_allowlist())
    errors.extend(check_ktm_angle_includes())
    errors.extend(check_kernel_no_relative_includes())
    errors.extend(check_portable_no_isa_asm())
    errors.extend(check_portable_no_isa_leak_literals())
    errors.extend(check_portable_no_task_arch_fields())
    errors.extend(check_process_pgd_accessor())
    errors.extend(check_process_saved_context_accessor())
    errors.extend(check_process_signal_enter_accessor())
    errors.extend(check_process_signal_defer_accessor())
    errors.extend(check_process_wait_state_accessor())
    errors.extend(check_syscall_frame_accessor())
    errors.extend(check_asm_offsets_sync())
    errors.extend(check_subsystems_json_paths())
    errors.extend(check_kernel_lib_i1_migration())
    errors.extend(check_includes_ir0_no_c_sources())
    errors.extend(check_portable_port1_no_legacy_arch_mm())
    errors.extend(check_portable_no_arch_prefix_calls())
    errors.extend(check_portable_no_arch_switch_include())
    errors.extend(check_scheduler_user_return_boundary())

    if errors:
        print("[arch-guard] FAILED")
        for err in errors:
            print(" -", err)
        return 1

    print("[arch-guard] OK")
    print("DEVFS_IO_CONTRACT_OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
