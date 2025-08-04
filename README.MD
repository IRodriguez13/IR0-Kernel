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
- 💻 Fully custom bootloader (no GRUB)
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

## 🧪 Status

| Subsystem         | Status     | Notes                              |
|------------------|------------|-------------------------------------|
| Bootloader        | ✅ Ready    | Custom, no GRUB                    |
| Paging            | ✅ Stable   | 32-bit, 4KB pages                  |
| Interrupts (IDT)  | ✅ OK       | Supports hardware & page fault     |
| PIT Timer         | ⚠️ Basic    | No scheduling yet                  |
| Scheduler         | 🚧 Planned  | Timer wheel / fair scheduling      |
| HPET / LAPIC      | 🕒 Pending  | High-res timers (coming soon)     |
| Filesystem        | 🚧 Planned  | Custom EXT2-based w/ vector index |
| Syscalls & IPC    | 🚧 Future   | Planned for later phase            |
| GUI / Desktop     | 🕒 Optional | For IR0-Desktop profile            |

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

