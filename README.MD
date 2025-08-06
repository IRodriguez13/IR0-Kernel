# IR0 Kernel

**IR0** is a custom monolithic (but hybrid with Linux subs) kernel written from scratch in C and assembly, built to explore low-level OS development with a clean and modular architecture.

It aims to be minimal at its core, but flexible enough to scale into different use cases: desktop, server, or embedded systems (IoT). Think of it as a learning playground — but with real-world goals.

---

## 🔧 Features

- 🪵 VGA-based text output and custom logging system
- 🧠 Software paging (virtual memory support)
- ⚡ IDT + basic interrupt handling (hardware & page faults)
- ⏱️ PIT-based timer + interrupt dispatch
- 🚨 Kernel panic system
- 💻 Fully custom bootloader (no GRUB but used in x64)
- 🛠️ Designed to support modular targets (Desktop, Server, IoT)

---

## 📁 Project Structure

- /boot -> Bootloader & real mode ASM
- /kernel -> Main kernel logic (paging, init, panic)
- /idt -> Interrupt descriptor table setup
- /io -> Text output and logging (print, colors)
- /paging -> Paging structures and control
- /panic -> Panic handler and CPU halt
- /timer -> PIT driver and handler
- /linker.ld -> Custom linker script
- /setup -> For Custom builds

---

### 🔧 IR0 Subsystems Overview

| Subsystem           | Status               | Notes                                                                 |
|--------------------|----------------------|-----------------------------------------------------------------------|
| 🧼 **Bootloader**     | ✅ Ready              | Custom bootloader (no GRUB), switches to 32-bit protected mode        |
| 📦 **Paging**         | ✅ Stable             | 32-bit, 4KB pages (9 tables = 36MB); planned upgrade to 64-bit + demand paging |
| ⚡ **Interrupts (IDT)**| ✅ OK                 | IDT loaded, custom ISRs in ASM/C, page fault handler working          |
| ⏲ **PIT Timer**      | ⚠ Basic              | Used as temporary fallback timer; shows ticks                         |
| 🧭 **HPET / LAPIC / ACPI** | 🧪 Partial           | ACPI parser and HPET/LAPIC detection working; MMIO and activation pending |
| 🧠 **Scheduler**      | 🚧 Ready for upgrade  | Basic Round-Robin; will be replaced by Timer Wheel or Fair Scheduler using hardware timers |
| 📁 **Filesystem**     | 🚧 Planned            | Custom EXT2-based FS with vector indexing; will be integrated as a module |
| 🔀 **Syscalls & IPC** | 🚧 Future             | Scheduled for later phase (userland and task communication)           |
| 🖥 **GUI / Desktop**  | 🕒 Optional           | For `IR0_DESKTOP` builds; not a current priority                      |

---

### 🧠 Technical Notes

- **Current Scheduler:** A working Round-Robin system using PIT as fallback. The final version will use smarter scheduling based on HPET or LAPIC timer events.
- **Advanced Timers:** ACPI parser detects HPET and LAPIC correctly; MMIO mapping and full activation are pending.
- **Future Paging:** Migration to **64-bit with demand paging** is planned — required for Linux-compatibility layers and advanced virtualized/userland support.
- **Modular Design:** Each subsystem can be toggled at build-time depending on the target (`IR0_DESKTOP`, `IR0_SERVER_SSL`, `IR0_IOT`) via conditional compilation.

---

## 📦 Build Instructions

> Requirements: `nasm`, `gcc` (cross-compiler preferred), `ld`, `qemu`

```bash
make
qemu-system-i386 -kernel ir0.bin
```


### 💡 Future Plans
- Modular build targets:

- IR0-Desktop: GUI, drivers, custom FS

- IR0-Server: Network stack, container runtime

- IR0-IoT: Minimal core, real-time schedulers

- Vector-based file indexing

- 64-bit transition

- Support for Docker/Kubernetes via hybrid syscall layer with Linux

## 🤝 Contributing
This project is mostly for learning and experimentation, but PRs, ideas or suggestions are welcome :--). You can open issues or forks to propose features or improvements.

