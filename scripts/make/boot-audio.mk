# SPDX-License-Identifier: GPL-3.0-only
# Boot audio QEMU wiring + SB16 probe smoke (included from top Makefile).
#
# QEMU 8+ requires an audiodev before -device sb16/adlib.
# Override QEMU_AUDIO_* in the top Makefile before including this file if needed.

# Prefer host playback (pa). Smokes should pass QEMU_AUDIO_BACKEND=none or use *_SILENT.
ifndef QEMU_AUDIO_BACKEND
QEMU_AUDIO_BACKEND = pa
endif
ifndef QEMU_AUDIO_DEV
QEMU_AUDIO_DEV = -audiodev $(QEMU_AUDIO_BACKEND),id=snd0
endif
ifndef QEMU_AUDIO_SB16
QEMU_AUDIO_SB16 = $(QEMU_AUDIO_DEV) -device sb16,audiodev=snd0
endif
ifndef QEMU_AUDIO_ADLIB
QEMU_AUDIO_ADLIB = -device adlib,audiodev=snd0
endif
ifndef QEMU_AUDIO_DEV_SILENT
QEMU_AUDIO_DEV_SILENT = -audiodev none,id=snd0
endif
ifndef QEMU_AUDIO_SB16_SILENT
QEMU_AUDIO_SB16_SILENT = $(QEMU_AUDIO_DEV_SILENT) -device sb16,audiodev=snd0
endif
ifndef QEMU_AUDIO_ALL
ifneq ($(CONFIG_ENABLE_SOUND),n)
QEMU_AUDIO_ALL = $(QEMU_AUDIO_SB16) $(QEMU_AUDIO_ADLIB)
else
QEMU_AUDIO_ALL =
endif
endif

.PHONY: smoke-sb16-probe
SB16_PROBE_LOG ?= /tmp/ir0-sb16-probe.log
# Stock userspace ISO + QEMU SB16 (+ Adlib). Requires DSP detect / SB16_DSP_OK.
# Adlib may still report ABSENT on some QEMU builds — SB16 is the gate.
smoke-sb16-probe: kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@echo "  SMOKE   SB16 probe (QEMU sb16+adlib)..."
	@DISK=$$(mktemp /tmp/ir0-sb16-probe.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	rm -f $(SB16_PROBE_LOG); \
	$(SMOKE_QEMU_RUN) --log $(SB16_PROBE_LOG) --timeout 90 --stale-sec 60 \
		--done SB16_SELFTEST_FIRED -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		$(QEMU_AUDIO_SB16_SILENT) -device adlib,audiodev=snd0 \
		-serial stdio -display none -m 128M -no-reboot -net none; \
	rm -f $$DISK; \
	if grep -q "SB16_DSP_OK" $(SB16_PROBE_LOG) && \
	    grep -q "SB16_SELFTEST_FIRED" $(SB16_PROBE_LOG) && \
	    grep -q "DSP Version" $(SB16_PROBE_LOG) && \
	    ! grep -q "DSP not detected" $(SB16_PROBE_LOG); then \
		echo "✓ smoke-sb16-probe passed"; \
		grep -E '\[BOOT\]|\[SB16\]|SB16_DSP|SB16_IRQ|Adlib|Driver .Sound' $(SB16_PROBE_LOG) | head -40; \
		if grep -q "SB16_IRQ_OK" $(SB16_PROBE_LOG); then \
			echo "  note: SB16 IRQ5 ack path OK"; \
		else \
			echo "  note: SB16_IRQ_OK absent (check IRQ5 in interactive Doom)"; \
		fi; \
		if grep -q "Adlib OPL2' initialized successfully" $(SB16_PROBE_LOG) || \
		   grep -q "Adlib.*initialized successfully" $(SB16_PROBE_LOG); then \
			echo "  note: Adlib present"; \
		else \
			echo "  note: Adlib ABSENT (OK — SB16 is the gate)"; \
		fi; \
	else \
		echo "✗ smoke-sb16-probe FAILED"; \
		grep -E '\[BOOT\]|\[SB16\]|Adlib|SOUND|panic|DSP' $(SB16_PROBE_LOG) | tail -50; exit 1; \
	fi
