# IR0 Driver Subsystem

> **Last verified:** 2026-09-01  
> **Source of truth:** `drivers/init_drv.c`, `drivers/audio/sound_blaster.c`, `interrupt/arch/isr_handlers.c`, `scripts/make/boot-audio.mk`

IR0 uses a centralized registry and bootstrap path for core and optional drivers.

## Registry and Bootstrap

- Registry API: `includes/ir0/driver.h`, implementation in `kernel/driver_registry.c`.
- Bootstrap orchestration: `drivers/init_drv.c` and `drivers/driver_bootstrap.c`.
- `init_all_drivers()` is the single runtime entry point for staged init.
- Config-selected boot drivers are gated by `CONFIG_INIT_*` options.

## Main Driver Families

- Input: PS/2 controller, keyboard, mouse.
- Timers: PIT, RTC, HPET, LAPIC, clock abstraction.
- Storage: ATA core and ATA block path. Odd userspace buffers use a 512-byte
  bounce in `drivers/storage/ata.c`; MINIX fast-path skips when dst/src is odd
  (`fs/minix_fs.c`) to avoid alignment storms.
- Network: RTL8139 path used by network stack.
- Audio: Sound Blaster, Adlib, PC speaker.
- Video/console: typewriter, console backend, VBE path.
- Serial: UART logging/control path.
- Bluetooth: HCI and related support path.

## User-Facing Integration

- Devices surface through `/dev` nodes.
- Driver status surfaces through `/proc/drivers`.
- Boot emits `KLOG_EVENT_DRIVER_PROBE_RESULT` per registered driver and a
  `KLOG_EVENT_DRIVER_SUMMARY` line (`ready/absent/deferred/unsupported/failed`)
  plus smoke tag `DRIVER_SUMMARY_OK` — see [`KLOG.md`](KLOG.md).

## Strengths

- Driver lifecycle is now easier to reason about due to unified bootstrap.
- Menuconfig-gated init reduces accidental startup of disabled hardware paths.
- Registry introspection improves runtime debugging and bring-up visibility.

## Audio (SB16 / Adlib)

- Sources: `drivers/audio/sound_blaster.c`, `drivers/audio/adlib.c`.
- Facade: `includes/ir0/sound_blaster.h`; glue: `kernel/lib/audio_backend.c` → `/dev/audio` in `fs/devfs.c`.
- Successful SB16 DSP probe emits `klog_smoke("SB16_DSP_OK")` and logs DSP version.
- **Playback (2026-09-01):** `sb16_play_pcm()` copies PCM into static ping-pong buffers (8192 B max, identity-mapped). DMA is programmed **before** `SB16_DSP_PLAY_8BIT` (0x14). Buffers are never `kfree`'d while DMA may still reference them (fixes Doom-class UAF on `/dev/audio` writes).
- **IRQ:** PIC IRQ **5** → `sb16_irq_handler()` in `interrupt/arch/isr_handlers.c` (ack DSP data port, clears `sb16_hw_playing`). Registered with `resource_register_irq(5, "sb16")` on successful probe.
- QEMU 8+ needs an audiodev before the ISA device.
- Interactive (`make run` / Doom GUI): default **PulseAudio** —
  `-audiodev pa,id=snd0 -device sb16,audiodev=snd0` so the Linux host hears guest PCM.
  Override: `QEMU_AUDIO_BACKEND=alsa|sdl|pipewire|none`.
- Automated smokes use a silent backend (`QEMU_AUDIO_SB16_SILENT` / `none`) so CI
  does not require a mixer.
- Variables / smoke: `scripts/make/boot-audio.mk` → `make smoke-sb16-probe`.
  Gate is **SB16 DSP detect**; Adlib may still report ABSENT on some QEMU builds
  (logged as note, not a fail).
- `make run` attaches `QEMU_AUDIO_ALL` when `CONFIG_ENABLE_SOUND≠n`.

