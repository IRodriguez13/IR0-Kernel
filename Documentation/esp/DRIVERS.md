# Subsistema de Drivers de IR0

IR0 usa un registry central y un bootstrap unificado para drivers core y opcionales.

## Registry y Bootstrap

- API de registry: `includes/ir0/driver.h`, implementacion en `kernel/driver_registry.c`.
- Orquestacion de bootstrap: `drivers/init_drv.c` y `drivers/driver_bootstrap.c`.
- `init_all_drivers()` es el punto unico de entrada para init por etapas.
- Los drivers de boot configurables quedan gateados por `CONFIG_INIT_*`.

## Familias de Drivers

- Entrada: PS/2 controller, teclado, mouse.
- Timers: PIT, RTC, HPET, LAPIC, clock abstraction.
- Storage: ATA core y ATA block.
- Red: RTL8139 integrado con stack de red.
- Audio: Sound Blaster, Adlib, PC speaker.
- Video/consola: typewriter, console backend, VBE.
- Serial: UART para log/control.
- Bluetooth: path HCI y soporte asociado.

## Integracion hacia Usuario

- Los dispositivos se exponen por nodos en `/dev`.
- El estado de drivers se expone por `/proc/drivers`.
- La inicializacion y fallos quedan logueados en runtime.

## Puntos Fuertes

- Ciclo de vida de drivers mas claro por bootstrap unificado.
- El gating por menuconfig evita arranque accidental de hardware deshabilitado.
- El registry mejora la observabilidad en bring-up y debugging.

## Puntos Debiles

- No hay modelo full de hotplug.
- La politica prioriza estabilidad de bring-up sobre aislamiento estricto.
- El perfil de hardware sigue mas orientado a dispositivos legacy.

## Audio (SB16 / Adlib)

- Probe SB16 OK emite `SB16_DSP_OK`. **Reproducción (2026-09-01):** buffers ping-pong estáticos, DMA antes de `PLAY`, sin UAF post-`write`. IRQ PIC **5** → `sb16_irq_handler()`.
- QEMU interactivo: `-audiodev pa,id=snd0` (host oye PCM). Smokes: backend `none`. Override: `QEMU_AUDIO_BACKEND=alsa|sdl|none`.
- Smoke: `make smoke-sb16-probe` (`scripts/make/boot-audio.mk`). Adlib puede quedar ABSENT (no falla el gate).
- Detalle canónico: [`../DRIVERS.md`](../DRIVERS.md).
