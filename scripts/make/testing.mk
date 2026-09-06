# SPDX-License-Identifier: GPL-3.0-only
# QA / smoke / KTM / test targets — included from the main Makefile.
# Legacy phase smokes: IR0_LEGACY_SMOKE=1 (setup/make/legacy-smokes.mk)
# Extended QA entry: scripts/ir0-qa.sh <target>

# Userspace init smoke binaries (copied to /sbin/init via load-init)
INIT_SMOKE_SRC = setup/pid1/init_smoke.c
INIT_MUSL_SRC  = setup/pid1/init_musl.c
MUSL_ARCH_PRCTL_SMOKE_SRC = setup/pid1/musl_arch_prctl_smoke.c
MUSL_PTHREAD_SMOKE_SRC = setup/pid1/musl_pthread_smoke.c
SETUID_EXEC_SMOKE_SRC = setup/pid1/setuid_exec_smoke.c
CHROOT_SMOKE_SRC = setup/pid1/chroot_smoke.c
SETID_HELPER_SRC = setup/pid1/setid_helper.c
PASSWD_SMOKE_SRC = $(IR0_USERSPACE_ROOT)/smoke/passwd_smoke.c
DOAS_SMOKE_SRC = $(IR0_USERSPACE_ROOT)/smoke/doas_smoke.c
IR0_AUTH_LIB_SRC = $(IR0_USERSPACE_ROOT)/lib/ir0_auth.c
RECOVERY_SMOKE_LOG = /tmp/userspace-recovery.log
DOAS_SMOKE_LOG = /tmp/userspace-doas.log
DOAS_SMOKE_BIN = $(IR0_USERSPACE_OUT)/smoke/doas_smoke
SETID_SCRIPT_SRC = setup/pid1/setid_script.sh
INIT_MINIMAL_SRC = setup/pid1/init_minimal.c
INIT_SEGV_SMOKE_SRC = setup/pid1/init_segv_smoke.c
INIT_HEAP_SMOKE_SRC = setup/pid1/init_heap_smoke.c
INIT_FAT16_SMOKE_SRC = setup/pid1/init_fat16_smoke.c
INIT_MMAP_SMOKE_SRC = setup/pid1/init_mmap_smoke.c
INIT_STACK_HEAP_ISO_SRC = setup/pid1/init_stack_heap_iso_smoke.c
INIT_FORK_MEM_SMOKE_SRC = setup/pid1/init_fork_mem_smoke.c
KTM_RECLAIM_EXIT_SMOKE_SRC = setup/pid1/ktm_reclaim_exit_smoke.c
KTM_PT_RECLAIM_SMOKE_SRC = setup/pid1/ktm_pt_reclaim_smoke.c
KTM_EXEC_STORM_SMOKE_SRC = setup/pid1/ktm_exec_storm_smoke.c
KTM_FORK_EXIT_STORM_SMOKE_SRC = setup/pid1/ktm_fork_exit_storm_smoke.c
KTM_FORK_EXIT_STORM_DEEP_SMOKE_SRC = setup/pid1/ktm_fork_exit_storm_deep_smoke.c
KTM_FORK_WAIT_STORM_SMOKE_SRC = setup/pid1/ktm_fork_wait_storm_smoke.c
KTM_EXEC_LOOP_SMOKE_SRC = setup/pid1/ktm_exec_loop_smoke.c
KTM_FORK_WAIT_DRAIN_SMOKE_SRC = setup/pid1/ktm_fork_wait_drain_smoke.c
KTM_EXEC_DRAIN_SMOKE_SRC = setup/pid1/ktm_exec_drain_smoke.c
KTM_INIT_EXIT_DRAIN_SMOKE_SRC = setup/pid1/ktm_init_exit_drain_smoke.c
KTM_FORK_ROLLBACK_SMOKE_SRC = setup/pid1/ktm_fork_rollback_smoke.c
KTM_FORK_MEM_TOUCH_SMOKE_SRC = setup/pid1/ktm_fork_mem_touch_smoke.c
KTM_FORK_NO_RECURSE_SMOKE_SRC = setup/pid1/ktm_fork_no_recursion_smoke.c
KTM_FORK_HEAP_SMOKE_SRC = setup/pid1/ktm_fork_heap_smoke.c
KTM_IPC_SMOKE_SRC = setup/pid1/ktm_ipc_smoke.c
KTM_PIPE_SMOKE_SRC = setup/pid1/ktm_pipe_smoke.c
KTM_BUSYBOX_SMOKE_SRC = setup/pid1/ktm_busybox_smoke.c
KTM_KTM_EXEC_ONLY_SMOKE_SRC = setup/pid1/ktm_exec_only_smoke.c
KTM_SHELL_SMOKE_SRC = setup/pid1/ktm_shell_smoke.c
KTM_TCC_SMOKE_SRC = setup/pid1/ktm_tcc_smoke.c
KTM_TCC_HARNESS_BIN = setup/pid1/fase52_harness
INIT_TCC_POWER_HALT_SRC = setup/pid1/init_tcc_power_halt.c
TCC_POWER_HALT_HARNESS_BIN = setup/pid1/tcc_power_halt_harness
TCC_POWER_HALT_LOG = /tmp/tcc-power-halt-smoke.log
KTM_DOOMGENERIC_SMOKE_BIN = setup/doom/doomgeneric_smoke
RUNIT_STAGE_BIN = $(IR0_USERSPACE_OUT)/stage-bin
# Smoke runit helpers (fase55d, tcc power, busybox power) live under smoke/stage-bin.
RUNIT_SMOKE_STAGE_BIN = $(IR0_USERSPACE_OUT)/smoke/stage-bin
KTM_FS_DEV_SMOKE_SRC = setup/pid1/ktm_fs_dev_smoke.c
KTM_POSIX_PSEUDOFS_SMOKE_SRC = setup/pid1/ktm_posix_pseudofs_smoke.c
INIT_HEART_SMOKE_SRC = setup/pid1/init_heart_smoke.c
KTM_FBDEV_SMOKE_SRC = setup/pid1/ktm_fbdev_smoke.c
KTM_INPUT_SMOKE_SRC = setup/pid1/ktm_input_smoke.c
KTM_INPUT_DET_SMOKE_SRC = setup/pid1/ktm_input_det_smoke.c
KTM_DOOM_PREREQ_SMOKE_SRC = setup/pid1/ktm_doom_prereq_smoke.c
KTM_DOOM_STUB_SRC = setup/doom/doomgeneric_ir0_stub.c
KTM_DOOM_TIMING_STUB_SRC = setup/doom/doomgeneric_ir0_stub.c
KTM_DOOMGENERIC_SRC = setup/doom/doomgeneric_ir0.c
KTM_BOOT_HALT_SMOKE_SRC = setup/pid1/ktm_boot_halt_smoke.c
KTM_FBDEV_GUI_SMOKE_SRC = setup/pid1/ktm_fbdev_gui_smoke.c
KTM_BOOT_HALT_BIN = setup/pid1/ktm_boot_halt
KTM_FBDEV_GUI_BIN = setup/pid1/ktm_fbdev_gui
KTM_GUI_DISPLAY ?= gtk
KTM_BOOT_HALT_GUI_LOG = /tmp/fase58c-boot-gui.log
KTM_FBDEV_GUI_LOG = /tmp/fase58c-fbdev-gui.log
KTM_DOOM_GUI_LOG = /tmp/fase58c-doom-gui.log
FASE58E_ASH_LOG = /tmp/fase58e-ash-gui.log
FASE58E_DISPLAY ?= gtk
FASE58E_ASH_SMOKE_LOG = /tmp/fase58e-ash-smoke.log
KTM_PROGRAMS_SMOKE_SRC = setup/pid1/ktm_programs_smoke.c
KTM_IPC_CAT_HELPER_SRC = setup/pid1/ktm_ipc_cat_helper.c
KTM_IPC_ECHO_HELPER_SRC = setup/pid1/ktm_ipc_echo_helper.c
KTM_IPC_BUSYBOX_HELPER_SRC = setup/pid1/ktm_ipc_busybox_helper.c
KTM_HELLO_HELPER_SRC = setup/pid1/ktm_hello_helper.c
KTM_IPC_CAT_HELPER_BIN = setup/pid1/fase48_cat
KTM_IPC_ECHO_HELPER_BIN = setup/pid1/fase48_echo
KTM_IPC_BUSYBOX_HELPER_BIN = setup/pid1/fase48_busybox
KTM_HELLO_HELPER_BIN = setup/pid1/fase50_hello
FASE50_BUSYBOX_BIN = setup/pid1/fase50_busybox_real
FASE50_BUSYBOX_CFG = $(IR0_USERSPACE_ROOT)/packages/busybox/fase58_busybox.config
FASE58_BUSYBOX_CFG = $(IR0_USERSPACE_ROOT)/packages/busybox/fase58_busybox.config
FASE58_FULL_BUSYBOX_CFG = $(IR0_USERSPACE_ROOT)/packages/busybox/fase58_full.config
# Product split: large general binary (0755) + reduced privileged binary (4755).
IR0_BUSYBOX_FULL_CFG = $(IR0_USERSPACE_ROOT)/packages/busybox/ir0_full.config
IR0_BUSYBOX_AUTH_CFG = $(IR0_USERSPACE_ROOT)/packages/busybox/ir0_auth.config
IR0_BUSYBOX_FULL_BIN = $(IR0_USERSPACE_OUT)/busybox-full
IR0_BUSYBOX_AUTH_BIN = $(IR0_USERSPACE_OUT)/busybox-auth
BB_MATRIX_SMOKE_SRC = $(IR0_USERSPACE_ROOT)/smoke/busybox_matrix_smoke.c
BB_MATRIX_SMOKE_BIN = $(IR0_USERSPACE_OUT)/smoke/busybox_matrix_smoke
BB_MATRIX_LOG = /tmp/busybox-applet-matrix.log
BB_MATRIX_TSV = $(IR0_USERSPACE_ROOT)/packages/busybox/bb_status.tsv
KTM_BUSYBOX_MANIFEST_SMOKE_SRC = setup/pid1/ktm_busybox_manifest_smoke.c
KTM_BUSYBOX_MANIFEST_SMOKE_BIN = setup/pid1/fase58l_busybox_smoke
KTM_BUSYBOX_MANIFEST_SMOKE_LOG = /tmp/fase58l-busybox-smoke.log
KTM_TRUE_HELPER_SRC = setup/pid1/ktm_true_helper.c
SH_SMOKE_SRC     = setup/pid1/sh_smoke.c
SEGV_SMOKE_SRC   = setup/pid1/userspace_segv.c
INIT_SMOKE_BIN   = setup/pid1/init
MUSL_ARCH_PRCTL_BIN = setup/pid1/musl_arch_prctl_smoke
MUSL_PTHREAD_SMOKE_BIN = setup/pid1/musl_pthread_smoke
SETUID_EXEC_SMOKE_BIN = setup/pid1/setuid_exec_smoke
CHROOT_SMOKE_BIN = setup/pid1/chroot_smoke
SETID_HELPER_BIN = setup/pid1/setid_helper
PASSWD_SMOKE_BIN = $(IR0_USERSPACE_OUT)/smoke/passwd_smoke
SH_SMOKE_BIN     = setup/pid1/sh_smoke
SEGV_SMOKE_BIN   = setup/pid1/userspace_segv
KTM_TRUE_HELPER_BIN  = setup/pid1/f41true
HEAP_SMOKE_LOG   = /tmp/userspace-heap-smoke.log
MMAP_SMOKE_LOG   = /tmp/userspace-mmap-smoke.log
ISO_SMOKE_LOG    = /tmp/userspace-stack-heap-iso.log
FORK_MEM_SMOKE_LOG = /tmp/userspace-fork-mem-smoke.log
FAT16_SMOKE_IMG  = build/fat16_smoke.img
FAT16_SMOKE_LOG  = /tmp/fat16-smoke.log
FASE41_RECLAIM_LOG = /tmp/userspace-fase41-reclaim.log
FASE42_PT_RECLAIM_LOG = /tmp/userspace-fase42-pt-reclaim.log
FASE42_EXEC_STORM_LOG = /tmp/userspace-fase42-exec-storm.log
FASE42_FORK_EXIT_STORM_LOG = /tmp/userspace-fase42-fork-exit-storm.log
FASE43_FORK_EXIT_STORM_LOG = /tmp/userspace-fase43-fork-exit-storm.log
FASE43_FORK_WAIT_STORM_LOG = /tmp/userspace-fase43-fork-wait-storm.log
FASE43_EXEC_LOOP_LOG = /tmp/userspace-fase43-exec-loop.log
FASE44_FORK_WAIT_DRAIN_LOG = /tmp/userspace-fase44-fork-wait-drain.log
FASE44_EXEC_DRAIN_LOG = /tmp/userspace-fase44-exec-drain.log
FASE44_INIT_EXIT_DRAIN_LOG = /tmp/userspace-fase44-init-exit-drain.log
FASE45_FORK_ROLLBACK_STORM_LOG = /tmp/userspace-fase45-fork-rollback-storm.log
FASE45_FORK_MEM_TOUCH_LOG = /tmp/userspace-fase45-fork-mem-touch.log
FASE46_FORK_NO_RECURSE_LOG = /tmp/userspace-fase46-fork-no-recursion.log
FASE46_FORK_HEAP_LOG = /tmp/userspace-fase46-fork-heap.log
FASE48_IPC_LOG = /tmp/userspace-fase48-ipc.log
FASE49_PIPE_LOG = /tmp/userspace-fase49-pipe.log
FASE50_BUSYBOX_LOG = /tmp/userspace-fase50-busybox.log
FASE50_KTM_EXEC_ONLY_LOG = /tmp/userspace-fase50-exec-only.log
KTM_SHELL_SHELL_LOG = /tmp/userspace-fase51-shell.log
KTM_TCC_TCC_LOG = /tmp/userspace-fase52-tcc.log
# Bisect lazy vs eager MM in legacy smokes: KERNEL_USERSPACE_ISO=kernel-x64-userspace-eager.iso
KERNEL_USERSPACE_ISO ?= kernel-x64-userspace.iso
KTM_FS_DEV_FS_DEV_LOG = /tmp/userspace-fase53a-fs-dev.log
KTM_POSIX_PSEUDOFS_POSIX_PSEUDOFS_LOG = /tmp/userspace-fase53b-posix-pseudofs.log
HEART_SMOKE_LOG = /tmp/userspace-heart.log
KTM_FBDEV_FBDEV_LOG = /tmp/userspace-fase54a-fbdev.log
KTM_INPUT_INPUT_LOG = /tmp/userspace-fase54b-input.log
KTM_INPUT_DET_INPUT_DET_LOG = /tmp/userspace-fase54c-input-deterministic.log
KTM_DOOM_PREREQ_LOG = /tmp/userspace-fase55a-doom-prereq.log
KTM_DOOM_STUB_LOG = /tmp/userspace-fase55b-doom-stub.log
KTM_DOOM_TIMING_STUB_LOG = /tmp/userspace-fase55c-timing-input.log
KTM_DOOMGENERIC_LOG = /tmp/userspace-fase55d-doomgeneric.log
MUSL_ARCH_PRCTL_LOG = /tmp/userspace-musl-arch-prctl.log
MUSL_PTHREAD_SMOKE_LOG = /tmp/userspace-musl-pthread.log
SETUID_EXEC_SMOKE_LOG = /tmp/userspace-setuid-exec.log
CHROOT_SMOKE_LOG = /tmp/userspace-chroot.log
PASSWD_SMOKE_LOG = /tmp/userspace-passwd.log
KTM_DOOM_INTERACTIVE_BIN = setup/pid1/ktm_doom_interactive
KTM_DOOM_INTERACTIVE_GUI_LOG = /tmp/fase55e-doomgeneric-gui.log
# irinit retired — product PID1 is runit (see build-runit / load-userspace-runit).
RUNIT_BIN_DIR = $(IR0_USERSPACE_OUT)/bin
RUNIT_INIT_BIN = $(RUNIT_BIN_DIR)/runit-init
RUNIT_SMOKE_LOG = /tmp/runit-boot-smoke.log
RUNIT_ASH_SMOKE_LOG = /tmp/runit-ash-smoke.log
DOOM_FRAMES ?= 0
DOOM_FRAME_DUMP_EVERY ?= 0
DOOM_DISPLAY ?= gtk
# Optional Doom IWAD for ken/games and legacy smokes (no maintainer home path).
REAL_WAD_PATH ?= $(or $(ISD_DOOM_IWAD),$(IR0_DOOM_IWAD),)
KTM_TCC_TCC_STAGE = setup/pid1/fase52_staging
FASE50_PROGRAMS_LOG = /tmp/userspace-fase50-programs.log
# Serial-log autokill: scripts/smoke_autokill.py (default max 180s; heavy smokes use --profile 90–120s).
SMOKE_QEMU_RUN = bash scripts/smoke_qemu_run.sh
MUSL_CC ?= $(shell command -v x86_64-linux-musl-gcc 2>/dev/null || command -v musl-gcc 2>/dev/null)
MUSL_CC_AARCH64 ?= $(shell \
	command -v aarch64-linux-musl-gcc 2>/dev/null || \
	(test -x $(KERNEL_ROOT)/toolchain/aarch64-linux-musl/bin/aarch64-linux-musl-gcc && \
		echo $(KERNEL_ROOT)/toolchain/aarch64-linux-musl/bin/aarch64-linux-musl-gcc) || \
	(test -x $(KERNEL_ROOT)/toolchain/aarch64-linux-musl-cross/bin/aarch64-linux-musl-gcc && \
		echo $(KERNEL_ROOT)/toolchain/aarch64-linux-musl-cross/bin/aarch64-linux-musl-gcc) || \
	true)
MUSL_AARCH64_HELLO = $(KERNEL_ROOT)/build/hello_aarch64
BUSYBOX_SRC ?= $(IR0_USERSPACE_ROOT)/packages/busybox/src
TCC_SRC ?= /tmp/tinycc-fase52

.PHONY: setup-musl-aarch64 musl-aarch64-hello smoke-musl-aarch64-toolchain
setup-musl-aarch64:
	@bash $(KERNEL_ROOT)/scripts/setup_musl_aarch64.sh

musl-aarch64-hello: setup-musl-aarch64
	@mkdir -p $(KERNEL_ROOT)/build
	@set -e; \
	if [ -n "$(MUSL_CC_AARCH64)" ] && [ -x "$(MUSL_CC_AARCH64)" ] && \
	   [ "$$($(MUSL_CC_AARCH64) -print-file-name=crt1.o)" != "crt1.o" ]; then \
		echo "  CC      hello_aarch64 with $(MUSL_CC_AARCH64)"; \
		$(MUSL_CC_AARCH64) -static -fno-pie -no-pie -Os \
			-Wl,-Ttext-segment=0x43000000 \
			-o $(MUSL_AARCH64_HELLO) \
			$(KERNEL_ROOT)/setup/pid1/hello_aarch64.c; \
	else \
		echo "  CC      hello_aarch64 freestanding (musl CRT missing)"; \
		aarch64-linux-gnu-gcc -nostdlib -static -fno-pie -no-pie -Os \
			-Wl,-Ttext-segment=0x43000000 -Wl,-e,_start \
			-o $(MUSL_AARCH64_HELLO) \
			$(KERNEL_ROOT)/setup/pid1/hello_aarch64_freestanding.c; \
	fi
	@file $(MUSL_AARCH64_HELLO) | grep -qi 'aarch64'
	@file $(MUSL_AARCH64_HELLO) | grep -qi 'static'
	@echo "✓ musl-aarch64-hello → $(MUSL_AARCH64_HELLO) (ET_EXEC @ 0x43000000)"

.PHONY: busybox-aarch64-min
BUSYBOX_AARCH64 = $(KERNEL_ROOT)/build/busybox_aarch64
busybox-aarch64-min: setup-musl-aarch64
	@mkdir -p $(KERNEL_ROOT)/build
	@if [ -x "$(BUSYBOX_AARCH64)" ] && file "$(BUSYBOX_AARCH64)" | grep -qi aarch64; then \
		echo "✓ busybox-aarch64-min already present: $(BUSYBOX_AARCH64)"; \
	elif [ -x "$(BUSYBOX_SRC)/busybox_unstripped" ]; then \
		cp -f "$(BUSYBOX_SRC)/busybox_unstripped" "$(BUSYBOX_AARCH64)"; \
		aarch64-linux-gnu-strip "$(BUSYBOX_AARCH64)" 2>/dev/null || true; \
		echo "✓ busybox-aarch64-min ← busybox_unstripped"; \
	else \
		echo "✗ build BusyBox aarch64 first (see oleada notes)"; exit 1; \
	fi
	@file $(BUSYBOX_AARCH64) | grep -qi aarch64
	@echo "✓ busybox-aarch64-min → $(BUSYBOX_AARCH64)"

smoke-musl-aarch64-toolchain: musl-aarch64-hello
	@file $(MUSL_AARCH64_HELLO) | grep -qi 'ELF'
	@echo "IR0_MUSL_AARCH64_TOOLCHAIN_OK"
	@echo "✓ smoke-musl-aarch64-toolchain passed"

build-init-smoke:
	@echo "  INIT    Building nostdlib ring-3 smoke ($(INIT_SMOKE_BIN))"
	@$(CC) -nostdlib -static -Os -fno-stack-protector -Wl,-e,_start -Wl,-s,-z,noexecstack \
		-o $(INIT_SMOKE_BIN) $(INIT_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-smoke OK ($(shell stat -c%s $(INIT_SMOKE_BIN) 2>/dev/null || stat -f%z $(INIT_SMOKE_BIN)) bytes)"

build-init-musl:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building musl static smoke with $(MUSL_CC)"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_MUSL_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-musl OK"

# Minimal musl static arch_prctl(ARCH_SET_FS) gate (F probe prerequisite).
build-musl-arch-prctl-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  MUSL    Building arch_prctl smoke ($(MUSL_ARCH_PRCTL_BIN))"
	@$(MUSL_CC) -static -Os -o $(MUSL_ARCH_PRCTL_BIN) $(MUSL_ARCH_PRCTL_SMOKE_SRC)
	@file $(MUSL_ARCH_PRCTL_BIN) | grep -q ELF
	@strings $(MUSL_ARCH_PRCTL_BIN) 2>/dev/null | grep -q "MUSL_ARCH_PRCTL_OK" || \
		(echo "✗ arch_prctl smoke missing MUSL_ARCH_PRCTL_OK string"; exit 1)
	@echo "✓ build-musl-arch-prctl-smoke OK"

build-musl-pthread-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  MUSL    Building pthread smoke ($(MUSL_PTHREAD_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(MUSL_PTHREAD_SMOKE_BIN) $(MUSL_PTHREAD_SMOKE_SRC)
	@file $(MUSL_PTHREAD_SMOKE_BIN) | grep -q ELF
	@strings $(MUSL_PTHREAD_SMOKE_BIN) 2>/dev/null | grep -q "MUSL_PTHREAD_OK" || \
		(echo "✗ pthread smoke missing MUSL_PTHREAD_OK string"; exit 1)
	@echo "✓ build-musl-pthread-smoke OK"

build-setuid-exec-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  MUSL    Building setuid-exec smoke ($(SETUID_EXEC_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(SETUID_EXEC_SMOKE_BIN) $(SETUID_EXEC_SMOKE_SRC)
	@$(MUSL_CC) -static -Os -o $(SETID_HELPER_BIN) $(SETID_HELPER_SRC)
	@file $(SETUID_EXEC_SMOKE_BIN) | grep -q ELF
	@file $(SETID_HELPER_BIN) | grep -q ELF
	@strings $(SETUID_EXEC_SMOKE_BIN) 2>/dev/null | grep -q "SETUID_EXEC_ALL_OK" || \
		(echo "✗ setuid smoke missing SETUID_EXEC_ALL_OK string"; exit 1)
	@echo "✓ build-setuid-exec-smoke OK"

build-chroot-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  MUSL    Building chroot smoke ($(CHROOT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(CHROOT_SMOKE_BIN) $(CHROOT_SMOKE_SRC)
	@file $(CHROOT_SMOKE_BIN) | grep -q ELF
	@strings $(CHROOT_SMOKE_BIN) 2>/dev/null | grep -q "CHROOT_OK" || \
		(echo "✗ chroot smoke missing CHROOT_OK string"; exit 1)
	@echo "✓ build-chroot-smoke OK"

build-passwd-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@mkdir -p $(dir $(PASSWD_SMOKE_BIN))
	@echo "  MUSL    Building passwd smoke ($(PASSWD_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(PASSWD_SMOKE_BIN) $(PASSWD_SMOKE_SRC) $(IR0_AUTH_LIB_SRC)
	@file $(PASSWD_SMOKE_BIN) | grep -q ELF
	@strings $(PASSWD_SMOKE_BIN) 2>/dev/null | grep -q "PASSWD_ALL_OK" || \
		(echo "✗ passwd smoke missing PASSWD_ALL_OK string"; exit 1)
	@echo "✓ build-passwd-smoke OK"

.PHONY: build-opendoas build-doas-smoke smoke-doas smoke-recovery
build-opendoas: check-userspace
	@$(IR0_USERSPACE_MAKE) build-opendoas

build-doas-smoke: build-opendoas
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@mkdir -p $(dir $(DOAS_SMOKE_BIN))
	@echo "  MUSL    Building doas smoke ($(DOAS_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(DOAS_SMOKE_BIN) $(DOAS_SMOKE_SRC) $(IR0_AUTH_LIB_SRC)
	@file $(DOAS_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-doas-smoke OK"

DOAS_SMOKE_TAGS = DOAS_SETUP_OK DOAS_GRANT_OK DOAS_ENV_OK DOAS_PERSIST_OK DOAS_DENY_AUTH_OK DOAS_ALL_OK

smoke-doas: build-doas-smoke load-userspace-runit kernel-x64-userspace.iso
	@echo "  SMOKE   OpenDoas grant/deny/env..."
	@DISK=$$(mktemp /tmp/ir0-doas-smoke.XXXXXX.img); \
	cp -f disk.img $$DISK && \
	python3 scripts/inject_init_minix.py $$DISK $(DOAS_SMOKE_BIN) sbin/init && \
	python3 scripts/verify_minix_rootfs.py $$DISK /sbin/init /usr/bin/doas /etc/doas.conf && \
	$(SMOKE_QEMU_RUN) --log $(DOAS_SMOKE_LOG) --timeout 90 --stale-sec 25 \
		--done DOAS_ALL_OK --fail-regex 'DOAS_SMOKE_FAIL|KERNEL PANIC' -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@for tag in $(DOAS_SMOKE_TAGS); do \
		grep -q "$$tag" $(DOAS_SMOKE_LOG) || \
			{ echo "✗ smoke-doas FAILED (missing $$tag)"; \
			  grep -E 'DOAS_|doas' $(DOAS_SMOKE_LOG) | tail -30; exit 1; }; \
	done
	@echo "✓ smoke-doas passed"

RECOVERY_SMOKE_TAGS = RECOVERY_BOOT_SELECTED RECOVERY_START RECOVERY_ROOT_RO RECOVERY_SHELL_READY
RECOVERY_SMOKE_ISO = /tmp/ir0-recovery-smoke.iso

# QEMU -kernel cannot load this x86-64 Multiboot ELF; GRUB ISO + recovery cfg.
kernel-x64-userspace-recovery.iso: kernel-x64-userspace.bin arch/x86-64/grub-recovery.cfg
	@echo "  ISO     $@ (recovery autoboot)"
	@rm -rf iso_userspace_recovery
	@mkdir -p iso_userspace_recovery/boot/grub
	@cp arch/x86-64/grub-recovery.cfg iso_userspace_recovery/boot/grub/grub.cfg
	@cp kernel-x64-userspace.bin iso_userspace_recovery/boot/kernel-x64.bin
	@grub-mkrescue -o $@ iso_userspace_recovery
	@rm -rf iso_userspace_recovery
	@echo "✓ ISO (recovery) created: $@"

smoke-recovery: load-userspace-runit kernel-x64-userspace-recovery.iso
	@echo "  SMOKE   recovery boot (ir0.recovery=1, rootfs RO)..."
	@DISK=$$(mktemp /tmp/ir0-recovery-smoke.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	$(SMOKE_QEMU_RUN) --log $(RECOVERY_SMOKE_LOG) --timeout 60 --stale-sec 20 \
		--done RECOVERY_SHELL_READY --fail-regex 'RECOVERY_HANDOFF_FAIL|KERNEL PANIC' -- \
		$(QEMU) -cdrom kernel-x64-userspace-recovery.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@for tag in $(RECOVERY_SMOKE_TAGS); do \
		grep -q "$$tag" $(RECOVERY_SMOKE_LOG) || \
			{ echo "✗ smoke-recovery FAILED (missing $$tag)"; \
			  grep -E 'RECOVERY_|RUNIT_' $(RECOVERY_SMOKE_LOG) | tail -30; exit 1; }; \
	done
	@echo "✓ smoke-recovery passed"

PIPELINE_STRESS_SRC = setup/pid1/pipeline_stress_smoke.c
PIPELINE_STRESS_BIN = setup/pid1/pipeline_stress_smoke
PIPELINE_STRESS_LOG = /tmp/ir0-pipeline-stress.log

.PHONY: build-pipeline-stress-smoke smoke-pipeline-stress

build-pipeline-stress-smoke: $(PIPELINE_STRESS_SRC)
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  MUSL    Building pipeline stress ($(PIPELINE_STRESS_BIN))"
	@$(MUSL_CC) -static -Os -idirafter includes -o $(PIPELINE_STRESS_BIN) $(PIPELINE_STRESS_SRC)
	@file $(PIPELINE_STRESS_BIN) | grep -q ELF
	@echo "✓ build-pipeline-stress-smoke OK"

# Product runit disk + BusyBox ash: hexdump|head, yes|head, od|head, N rounds.
smoke-pipeline-stress: build-pipeline-stress-smoke load-userspace-runit kernel-x64-userspace.iso
	@echo "  SMOKE   ash pipeline stress (hexdump|head + rounds)..."
	@DISK=$$(mktemp /tmp/ir0-pipeline-stress.XXXXXX.img); \
	cp -f disk.img $$DISK && \
	python3 scripts/inject_init_minix.py $$DISK $(PIPELINE_STRESS_BIN) sbin/init && \
	$(SMOKE_QEMU_RUN) --log $(PIPELINE_STRESS_LOG) --timeout 120 --stale-sec 40 \
		--done PIPELINE_STRESS_OK \
		--fail-regex 'PIPELINE_STRESS_FAIL |KERNEL PANIC' -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@grep -q PIPELINE_STRESS_OK $(PIPELINE_STRESS_LOG) || \
		{ echo "✗ smoke-pipeline-stress FAILED"; \
		  grep -E 'PIPELINE_|Segmentation|Oops|PANIC' $(PIPELINE_STRESS_LOG) | tail -40; exit 1; }
	@echo "✓ smoke-pipeline-stress passed"

# Recursive walk of the real IR0 tree over virtio-9p — the interactive
# workload (`find` from /) that no smoke exercised before it double-faulted.
SESSION_WALK_BIN = setup/pid1/session_walk_smoke
SESSION_WALK_LOG = /tmp/ir0-session-walk.log

.PHONY: build-session-walk-smoke smoke-session-walk
build-session-walk-smoke:
	@if [ -z "$(MUSL_CC)" ]; then echo "â musl cc missing"; exit 1; fi
	@$(MUSL_CC) -static -Os -o $(SESSION_WALK_BIN) setup/pid1/session_walk_smoke.c
	@echo "â build-session-walk-smoke OK"

smoke-session-walk: build-session-walk-smoke kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@echo "  SMOKE   session walk (find over real 9p tree + /proc + /heart)..."
	@DISK=$$(mktemp /tmp/ir0-session-walk.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(SESSION_WALK_BIN) sbin/init; \
	rm -f $(SESSION_WALK_LOG); \
	$(SMOKE_QEMU_RUN) --log $(SESSION_WALK_LOG) --timeout 300 --stale-sec 60 \
		--done 'SESSION_WALK_OK' \
		--fail-regex 'SESSION_WALK_.*FAIL|SESSION_WALK_KSTACK_LOW|SESSION_WALK_KSTACK_PEAK_HIGH|KERNEL PANIC' -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-fsdev local,id=ir0fs,path=$(KERNEL_ROOT),security_model=none \
		-device virtio-9p-pci,fsdev=ir0fs,mount_tag=ir0share,disable-modern=on \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK
	@grep -q SESSION_WALK_OK $(SESSION_WALK_LOG) || \
		{ echo "â smoke-session-walk FAILED"; \
		  grep -aE 'SESSION_WALK_|PANIC|ISR64' $(SESSION_WALK_LOG) | tail -30; exit 1; }
	@grep -a 'SESSION_WALK_STATS\|SESSION_WALK_KSTACK' $(SESSION_WALK_LOG) | tail -2
	@echo "â smoke-session-walk passed"

CMD_STRESS_SRC = setup/pid1/cmd_stress_smoke.c
CMD_STRESS_BIN = setup/pid1/cmd_stress_smoke
CMD_STRESS_LOG = /tmp/ir0-cmd-stress.log

.PHONY: build-cmd-stress-smoke smoke-cmd-stress

build-cmd-stress-smoke: $(CMD_STRESS_SRC)
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  MUSL    Building cmd stress ($(CMD_STRESS_BIN))"
	@$(MUSL_CC) -static -Os -o $(CMD_STRESS_BIN) $(CMD_STRESS_SRC)
	@file $(CMD_STRESS_BIN) | grep -q ELF
	@echo "✓ build-cmd-stress-smoke OK"

smoke-cmd-stress: build-cmd-stress-smoke load-userspace-runit kernel-x64-userspace.iso
	@echo "  SMOKE   BusyBox cmd battery (PID1)..."
	@DISK=$$(mktemp /tmp/ir0-cmd-stress.XXXXXX.img); \
	cp -f disk.img $$DISK && \
	python3 scripts/inject_init_minix.py $$DISK $(CMD_STRESS_BIN) sbin/init && \
	$(SMOKE_QEMU_RUN) --log $(CMD_STRESS_LOG) --timeout 120 --stale-sec 40 \
		--done CMD_STRESS_OK \
		--fail-regex 'CMD_STRESS_FAIL|KERNEL PANIC' -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@grep -q CMD_STRESS_OK $(CMD_STRESS_LOG) || \
		{ echo "✗ smoke-cmd-stress FAILED"; \
		  grep -E 'CMD_STRESS_|Segmentation|Oops|PANIC' $(CMD_STRESS_LOG) | tail -40; exit 1; }
	@echo "✓ smoke-cmd-stress passed"

.PHONY: smoke-shell-pipe-stress smoke-session-soak smoke-session-stability

SOAK_ROUNDS ?= 8
# Extra flags for the soak harness, e.g. SOAK_EXTRA="--relogin-every 3"
SOAK_EXTRA ?=

# Interactive getty session: complex ash pipelines via HMP sendkey.
smoke-shell-pipe-stress: load-userspace-runit kernel-x64-userspace.iso
	@echo "  SMOKE   shell pipe stress (getty + HMP pipelines)..."
	@chmod +x scripts/smoke_shell_pipe_stress.py
	@python3 scripts/smoke_shell_pipe_stress.py \
		--iso kernel-x64-userspace.iso \
		--disk disk.img \
		--log /tmp/ir0-shell-pipe-stress.log \
		--timeout 180
	@echo "✓ smoke-shell-pipe-stress passed"

# Long session soak: many rounds + relogins, watched by the kernel-side
# canary and stack-top classifier rather than by a command happening to die.
smoke-session-soak: load-userspace-runit kernel-x64-userspace.iso
	@echo "  SMOKE   session soak (rounds=$(SOAK_ROUNDS))..."
	@chmod +x scripts/smoke_session_soak.py
	@python3 scripts/smoke_session_soak.py \
		--iso kernel-x64-userspace.iso \
		--disk disk.img \
		--log /tmp/ir0-session-soak.log \
		--rounds $(SOAK_ROUNDS) $(SOAK_EXTRA)
	@echo "✓ smoke-session-soak passed"

smoke-sigchld-tty: load-userspace-runit kernel-x64-userspace.iso
	@echo "  SMOKE   SIGCHLD during TTY-blocked read (GPR leak regression)..."
	@python3 scripts/smoke_sigchld_tty.py
	@echo "✓ smoke-sigchld-tty passed"

smoke-kbd-resync: load-userspace-runit kernel-x64-userspace.iso
	@echo "  SMOKE   keyboard resync across session boundary..."
	@python3 scripts/smoke_kbd_resync.py
	@echo "✓ smoke-kbd-resync passed"

smoke-desktop-relogin: load-userspace-runit kernel-x64-userspace.iso
	@echo "  SMOKE   desktop relogin after exec ls..."
	@python3 scripts/smoke_desktop_relogin.py
	@echo "✓ smoke-desktop-relogin passed"

smoke-ctrl-c-spam: load-userspace-runit kernel-x64-userspace.iso
	@echo "  SMOKE   Ctrl+C spam on interactive ash..."
	@python3 scripts/smoke_ctrl_c_spam.py
	@echo "✓ smoke-ctrl-c-spam passed"

smoke-sigchld-no-false-logout: load-userspace-runit kernel-x64-userspace.iso
	@echo "  SMOKE   SIGCHLD must not false-logout shell..."
	@python3 scripts/smoke_sigchld_no_false_logout.py
	@echo "✓ smoke-sigchld-no-false-logout passed"

smoke-sigchld-sysfs-stress: load-userspace-runit kernel-x64-userspace.iso
	@echo "  SMOKE   SIGCHLD + sysfs/cat-on-dir stress..."
	@python3 scripts/smoke_sigchld_sysfs_stress.py
	@echo "✓ smoke-sigchld-sysfs-stress passed"

smoke-keyboard-stability: kernel-x64.bin
	@echo "  BATTERY keyboard/signal stability..."
	@$(MAKE) -s smoke-sigchld-tty
	@$(MAKE) -s smoke-kbd-resync
	@$(MAKE) -s smoke-desktop-relogin
	@$(MAKE) -s smoke-ctrl-c-spam
	@$(MAKE) -s smoke-sigchld-no-false-logout
	@$(MAKE) -s smoke-sigchld-sysfs-stress
	@echo "✓ smoke-keyboard-stability passed"

smoke-session-chaos: load-userspace-runit kernel-x64-userspace.iso
	@echo "  SMOKE   session chaos (getty login + /dev + 9p)..."
	@python3 scripts/smoke_session_chaos.py \
		--iso kernel-x64-userspace.iso \
		--disk disk.img \
		--log /tmp/ir0-session-chaos.log
	@echo "✓ smoke-session-chaos passed"

# Session-stability battery for 0.0.1 desk/terminal hardening.
# ktm-userdev-session-stress-run is best-effort (ash pipe still P1 under load).
smoke-session-stability: kernel-x64.bin arch-guard
	@echo "  BATTERY session stability (pipes/cmd/ash/cow/shell)..."
	@$(MAKE) -s smoke-pipeline-stress
	@$(MAKE) -s smoke-cmd-stress
	@$(MAKE) -s smoke-mm-cow-lazy
	@$(MAKE) -s smoke-tier1
	@$(MAKE) -s smoke-shell-pipe-stress
	@$(MAKE) -s smoke-session-walk
	@echo "  BATTERY optional ktm session stress..."
	@$(MAKE) -s ktm-userdev-session-stress-run || \
		echo "⚠ ktm-userdev-session-stress-run soft-fail (P1 ash/session)"
	@echo "✓ smoke-session-stability passed"

# Minimal PID 1: fork/execve/wait4 (musl); needs /bin/sh on disk for oleada 2 smoke.
build-init-minimal:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building musl minimal PID1 ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_MINIMAL_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-minimal OK"

# Minimal /bin/sh stub for fork+execve smoke (musl static).
build-sh-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  SH      Building musl /bin/sh stub ($(SH_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(SH_SMOKE_BIN) $(SH_SMOKE_SRC)
	@file $(SH_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-sh-smoke OK"

build-init-segv-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building userspace segv PID1 smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_SEGV_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-segv-smoke OK"

build-userspace-segv:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  SEGv    Building /bin/userspace_segv ($(SEGV_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(SEGV_SMOKE_BIN) $(SEGV_SMOKE_SRC)
	@file $(SEGV_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-userspace-segv OK"

build-init-heap-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building userspace heap smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_HEAP_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-heap-smoke OK"

build-init-fat16-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FAT16 mount/read smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_FAT16_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-fat16-smoke OK"

build-init-mmap-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building userspace mmap smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_MMAP_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-mmap-smoke OK"

build-init-stack-heap-iso-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building stack/heap isolation smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_STACK_HEAP_ISO_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-stack-heap-iso-smoke OK"

build-init-fork-mem-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building fork memory smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_FORK_MEM_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-fork-mem-smoke OK"

build-ktm-true-helper:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  TRUE    Building FASE41 /bin/f41true ($(KTM_TRUE_HELPER_BIN))"
	@$(MUSL_CC) -static -Os -o $(KTM_TRUE_HELPER_BIN) $(KTM_TRUE_HELPER_SRC)
	@file $(KTM_TRUE_HELPER_BIN) | grep -q ELF
	@echo "✓ build-ktm-true-helper OK"

build-ktm-reclaim-exit-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE41 reclaim smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(KTM_RECLAIM_EXIT_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-ktm-reclaim-exit-smoke OK"

build-ktm-pt-reclaim-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE42 page-table reclaim smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(KTM_PT_RECLAIM_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-ktm-pt-reclaim-smoke OK"

build-ktm-exec-storm-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE42 exec storm smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(KTM_EXEC_STORM_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-ktm-exec-storm-smoke OK"

build-ktm-fork-exit-storm-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE42 fork+exit storm smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(KTM_FORK_EXIT_STORM_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-ktm-fork-exit-storm-smoke OK"

build-ktm-fork-exit-storm-deep-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE43 fork+exit storm smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(KTM_FORK_EXIT_STORM_DEEP_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-ktm-fork-exit-storm-deep-smoke OK"

build-ktm-fork-wait-storm-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE43 fork+wait storm smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(KTM_FORK_WAIT_STORM_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-ktm-fork-wait-storm-smoke OK"

build-ktm-exec-loop-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE43 exec loop smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(KTM_EXEC_LOOP_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-ktm-exec-loop-smoke OK"

build-ktm-fork-wait-drain-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE44 fork-wait-drain smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(KTM_FORK_WAIT_DRAIN_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-ktm-fork-wait-drain-smoke OK"

build-ktm-exec-drain-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE44 exec-drain smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(KTM_EXEC_DRAIN_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-ktm-exec-drain-smoke OK"

build-ktm-init-exit-drain-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE44 init-exit-drain smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(KTM_INIT_EXIT_DRAIN_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-ktm-init-exit-drain-smoke OK"

build-ktm-fork-rollback-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE45 fork rollback storm ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(KTM_FORK_ROLLBACK_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-ktm-fork-rollback-smoke OK"

build-ktm-fork-mem-touch-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE45 fork mem touch ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(KTM_FORK_MEM_TOUCH_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-ktm-fork-mem-touch-smoke OK"

build-ktm-fork-no-recursion-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE46 fork-no-recursion ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(KTM_FORK_NO_RECURSE_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-ktm-fork-no-recursion-smoke OK"

build-ktm-fork-heap-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE46 fork+heap ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(KTM_FORK_HEAP_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-ktm-fork-heap-smoke OK"

build-ktm-ipc-helpers:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  FASE48  Building /bin/cat /bin/echo /bin/busybox"
	@$(MUSL_CC) -static -Os -o $(KTM_IPC_CAT_HELPER_BIN) $(KTM_IPC_CAT_HELPER_SRC)
	@$(MUSL_CC) -static -Os -o $(KTM_IPC_ECHO_HELPER_BIN) $(KTM_IPC_ECHO_HELPER_SRC)
	@$(MUSL_CC) -static -Os -o $(KTM_IPC_BUSYBOX_HELPER_BIN) $(KTM_IPC_BUSYBOX_HELPER_SRC)
	@file $(KTM_IPC_CAT_HELPER_BIN) $(KTM_IPC_ECHO_HELPER_BIN) $(KTM_IPC_BUSYBOX_HELPER_BIN) | grep -q ELF
	@echo "✓ build-ktm-ipc-helpers OK"

build-ktm-ipc-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE48 IPC smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(KTM_IPC_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-ktm-ipc-smoke OK"

build-ktm-pipe-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE49 pipe smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(KTM_PIPE_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-ktm-pipe-smoke OK"

build-busybox-fase50-min:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@if [ ! -d "$(BUSYBOX_SRC)" ] || [ ! -f "$(BUSYBOX_SRC)/Makefile" ]; then \
		echo "✗ BusyBox source missing at BUSYBOX_SRC=$(BUSYBOX_SRC)"; \
		echo "  Run: make -C $(IR0_USERSPACE_ROOT) fetch"; \
		echo "  Or override: make ... BUSYBOX_SRC=/path/to/busybox-<version>"; \
		exit 1; \
	fi
	@if [ ! -f "$(FASE50_BUSYBOX_CFG)" ]; then \
		echo "✗ Missing config fragment $(FASE50_BUSYBOX_CFG)"; \
		exit 1; \
	fi
	@echo "  FASE50  Building ash+coreutils static BusyBox from $(BUSYBOX_SRC)"
	@chmod +x scripts/busybox_apply_fragment.sh
	@scripts/busybox_apply_fragment.sh "$(BUSYBOX_SRC)" "$(FASE50_BUSYBOX_CFG)"
	@$(MAKE) -C "$(BUSYBOX_SRC)" CC="$(MUSL_CC)" CFLAGS="-fno-pie" LDFLAGS="-no-pie" -j$$(nproc)
	@cp -f "$(BUSYBOX_SRC)/busybox" "$(FASE50_BUSYBOX_BIN)"
	@file "$(FASE50_BUSYBOX_BIN)" | grep -q ELF
	@chmod +x scripts/busybox_check_manifest.sh
	@scripts/busybox_check_manifest.sh "$(FASE50_BUSYBOX_BIN)"
	@echo "✓ build-busybox-fase50-min OK"

build-busybox-fase58-plus:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@if [ ! -d "$(BUSYBOX_SRC)" ] || [ ! -f "$(BUSYBOX_SRC)/Makefile" ]; then \
		echo "✗ BusyBox source missing at BUSYBOX_SRC=$(BUSYBOX_SRC)"; \
		exit 1; \
	fi
	@if [ ! -f "$(FASE58_BUSYBOX_CFG)" ]; then \
		echo "✗ Missing config fragment $(FASE58_BUSYBOX_CFG)"; \
		exit 1; \
	fi
	@echo "  FASE58  Building ash+coreutils BusyBox from $(BUSYBOX_SRC)"
	@chmod +x scripts/busybox_apply_fragment.sh
	@scripts/busybox_apply_fragment.sh "$(BUSYBOX_SRC)" "$(FASE58_BUSYBOX_CFG)"
	@$(MAKE) -C "$(BUSYBOX_SRC)" CC="$(MUSL_CC)" CFLAGS="-fno-pie" LDFLAGS="-no-pie" -j$$(nproc)
	@cp -f "$(BUSYBOX_SRC)/busybox" "$(FASE50_BUSYBOX_BIN)"
	@file "$(FASE50_BUSYBOX_BIN)" | grep -q ELF
	@chmod +x scripts/busybox_check_manifest.sh
	@scripts/busybox_check_manifest.sh "$(FASE50_BUSYBOX_BIN)"
	@echo "✓ build-busybox-fase58-plus OK (installed to $(FASE50_BUSYBOX_BIN))"

build-busybox-fase58-full:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@if [ ! -d "$(BUSYBOX_SRC)" ] || [ ! -f "$(BUSYBOX_SRC)/Makefile" ]; then \
		echo "✗ BusyBox source missing at BUSYBOX_SRC=$(BUSYBOX_SRC)"; \
		exit 1; \
	fi
	@if [ ! -f "$(FASE58_FULL_BUSYBOX_CFG)" ]; then \
		echo "✗ Missing config fragment $(FASE58_FULL_BUSYBOX_CFG)"; \
		exit 1; \
	fi
	@echo "  FASE58L Building full applets BusyBox from $(BUSYBOX_SRC)"
	@chmod +x scripts/busybox_apply_fragment.sh
	@scripts/busybox_apply_fragment.sh "$(BUSYBOX_SRC)" "$(FASE58_FULL_BUSYBOX_CFG)"
	@$(MAKE) -C "$(BUSYBOX_SRC)" CC="$(MUSL_CC)" CFLAGS="-fno-pie" LDFLAGS="-no-pie" -j$$(nproc)
	@cp -f "$(BUSYBOX_SRC)/busybox" "$(FASE50_BUSYBOX_BIN)"
	@file "$(FASE50_BUSYBOX_BIN)" | grep -q ELF
	@chmod +x scripts/busybox_check_manifest.sh
	@scripts/busybox_check_manifest.sh "$(FASE50_BUSYBOX_BIN)"
	@echo "✓ build-busybox-fase58-full OK (installed to $(FASE50_BUSYBOX_BIN))"

.PHONY: build-busybox-ir0-full build-busybox-ir0-auth build-busybox-matrix-smoke \
	busybox-matrix busybox-profiles-check

# Product BusyBox binaries (full 0755 + auth 4755) come from IR0-userspace.
build-busybox-ir0-full: check-userspace
	@$(IR0_USERSPACE_MAKE) build-busybox

build-busybox-ir0-auth: build-busybox-ir0-full

build-busybox-matrix-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@mkdir -p $(dir $(BB_MATRIX_SMOKE_BIN))
	@echo "  MUSL    Building BusyBox applet matrix driver ($(BB_MATRIX_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(BB_MATRIX_SMOKE_BIN) \
		$(BB_MATRIX_SMOKE_SRC) \
		$(IR0_USERSPACE_ROOT)/smoke/matrix_capture.c
	@file $(BB_MATRIX_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-busybox-matrix-smoke OK"

# Applet status matrix from real guest behaviour (never from "it compiled").
busybox-matrix: build-busybox-matrix-smoke build-busybox-ir0-full kernel-x64-userspace.iso
	@echo "  SMOKE   BusyBox applet matrix (supported / partial / unavailable)..."
	@DISK=$$(mktemp /tmp/ir0-bbmatrix.XXXXXX.img); \
	dd if=/dev/zero of=$$DISK bs=1M count=200 status=none && \
	python3 scripts/inject_init_minix.py --format-large $$DISK && \
	python3 scripts/inject_init_minix.py $$DISK $(BB_MATRIX_SMOKE_BIN) sbin/init && \
	python3 scripts/inject_init_minix.py $$DISK $(IR0_BUSYBOX_FULL_BIN) bin/busybox && \
	python3 scripts/inject_init_minix.py --hardlink $$DISK bin/busybox bin/sh && \
	python3 scripts/inject_init_minix.py --hardlink $$DISK bin/busybox bin/ls && \
	python3 scripts/inject_init_minix.py --hardlink $$DISK bin/busybox bin/true && \
	$(SMOKE_QEMU_RUN) --log $(BB_MATRIX_LOG) --timeout 420 --stale-sec 60 \
		--done BBMATRIX_OK --fail-regex 'BBMATRIX_FAIL|KERNEL PANIC' -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 1024M -no-reboot -net none; \
	rm -f $$DISK
	@python3 $(IR0_USERSPACE_ROOT)/scripts/busybox_applet_matrix.py --log $(BB_MATRIX_LOG) \
		--binary $(IR0_BUSYBOX_FULL_BIN) --write $(BB_MATRIX_TSV) \
		--write-development $(IR0_USERSPACE_ROOT)/profiles/development.txt
	@echo "✓ busybox-matrix OK ($(BB_MATRIX_TSV))"

# Gate: no profile may advertise an applet the matrix does not back with a run.
busybox-profiles-check: check-userspace
	@python3 $(IR0_USERSPACE_ROOT)/scripts/busybox_applet_matrix.py --check \
		--matrix $(BB_MATRIX_TSV) --profiles $(IR0_USERSPACE_ROOT)/profiles

build-ktm-busybox-manifest-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE58L BusyBox smoke ($(KTM_BUSYBOX_MANIFEST_SMOKE_BIN))"
	@$(MUSL_CC) $(KTM_USERDEV_MUSL_FLAGS) \
		-o $(KTM_BUSYBOX_MANIFEST_SMOKE_BIN) $(KTM_BUSYBOX_MANIFEST_SMOKE_SRC) $(KTM_USERDEV_LIB_SRC)
	@file $(KTM_BUSYBOX_MANIFEST_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-ktm-busybox-manifest-smoke OK"

# BUSY-2 ship gate — canonical KTM runner (prebuilt format-large + manifest disk)
BUSYBOX_MANIFEST_SMOKE_LOG ?= /tmp/busybox-manifest-smoke.log
ktm-userdev-busybox-manifest-run: build-ktm-busybox-manifest-smoke build-busybox-fase58-plus kernel-x64-userspace.iso
	@echo "  SMOKE   BUSY-2 product applet manifest (KTM)..."
	@strings $(KTM_BUSYBOX_MANIFEST_SMOKE_BIN) 2>/dev/null | grep -q "KTM_BB_MANIFEST_HARNESS_ID" || \
		(echo "✗ $(KTM_BUSYBOX_MANIFEST_SMOKE_BIN) is not KTM busybox manifest harness — run build-ktm-busybox-manifest-smoke"; exit 1)
	@DISK=$$(mktemp /tmp/ir0-busy-manifest-disk.XXXXXX.img); \
	dd if=/dev/zero of=$$DISK bs=1M count=200 status=none && \
	python3 scripts/inject_init_minix.py --format-large $$DISK && \
	python3 scripts/inject_init_minix.py $$DISK $(KTM_BUSYBOX_MANIFEST_SMOKE_BIN) sbin/init && \
	chmod +x scripts/busybox_inject_manifest.sh && \
	FASE50_BUSYBOX_BIN=$(FASE50_BUSYBOX_BIN) scripts/busybox_inject_manifest.sh $$DISK $(FASE50_BUSYBOX_BIN) && \
	python3 scripts/ktm_userdev_runner.py \
		--disk $$DISK \
		--legacy-disk-init \
		--no-fsdev \
		--init $(KTM_BUSYBOX_MANIFEST_SMOKE_BIN) \
		--log $(BUSYBOX_MANIFEST_SMOKE_LOG) \
		--timeout 90 \
		--done KTM_BB_MANIFEST_OK \
		--require BUSYBOX_MANIFEST_OK \
		--require KTM_BB_MANIFEST_ECHO_PATH_OK \
		--require KTM_BB_MANIFEST_LS_PATH_OK \
		--require KTM_BB_MANIFEST_CAT_PATH_OK \
		--require KTM_BUSYBOX_COREUTILS_OK \
		--require KTM_USERDEV_OK; \
	RC=$$?; rm -f $$DISK; exit $$RC
	@echo "✓ ktm-userdev-busybox-manifest-run"

smoke-busybox-manifest: ktm-userdev-busybox-manifest-run
	@echo "✓ smoke-busybox-manifest (alias → ktm-userdev-busybox-manifest-run)"

build-ktm-hello-helper:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  FASE50  Building hello-world ($(KTM_HELLO_HELPER_BIN))"
	@$(MUSL_CC) -static -Os -o $(KTM_HELLO_HELPER_BIN) $(KTM_HELLO_HELPER_SRC)
	@file $(KTM_HELLO_HELPER_BIN) | grep -q ELF
	@echo "✓ build-ktm-hello-helper OK"

build-ktm-busybox-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE50 BusyBox smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(KTM_BUSYBOX_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-ktm-busybox-smoke OK"

build-ktm-exec-only-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE50 EXEC-only smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(KTM_KTM_EXEC_ONLY_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-ktm-exec-only-smoke OK"

build-ktm-shell-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE51 shell smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(KTM_SHELL_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-ktm-shell-smoke OK"

build-irinit:
	@echo "✗ build-irinit retired — PID1 is runit (make build-runit)"
	@exit 2

load-userspace-irinit:
	@echo "✗ load-userspace-irinit retired — use: make load-userspace-runit"
	@exit 2

smoke-userspace-irinit:
	@echo "✗ smoke-userspace-irinit retired — use: make smoke-runit-boot"
	@exit 2

run-irinit-interactive-gui:
	@echo "✗ run-irinit-interactive-gui retired — use: make run-fase58e-ash-gui"
	@exit 2

build-runit: check-userspace
	@$(IR0_USERSPACE_MAKE) build-runit build-services
	@chmod +x $(IR0_USERSPACE_ROOT)/scripts/build-services.sh
	@ARCH=x86_64 CC="$(MUSL_CC)" MUSL_CC="$(MUSL_CC)" \
		PRODUCT_OUT="$(IR0_USERSPACE_OUT)/x86_64/product" \
		SMOKE_OUT="$(IR0_USERSPACE_OUT)/x86_64/smoke" \
		$(IR0_USERSPACE_ROOT)/scripts/build-services.sh smoke
	@$(IR0_USERSPACE_MAKE) compat-links

load-userspace-runit: check-userspace build-runit build-busybox-ir0-auth build-opendoas
	@echo "note: load-userspace-runit is LEGACY — product path is make first-boot / isd-image"
	@$(IR0_USERSPACE_MAKE) build-ncurses build-nano || \
		echo "  WARN    nano not built (optional; cat /usr/bin/nano after reinject)"
	@if [ "$${IR0_GUEST_MANDOCS:-1}" != "0" ]; then \
		$(MAKE) -s prepare-guest-mandocs || \
			echo "  WARN    prepare-guest-mandocs failed (optional)"; \
	fi
	@DISK=$${DISK:-$(IR0_DEV_DISK)}; \
	set -e; \
	PROFILE=$${IR0_PRODUCT_PROFILE:-minimal}; \
	STAMP=$${DISK}.runit.stamp; \
	NEED=0; \
	GUEST_MAN="$(KERNEL_ROOT)/build/guest-man"; \
	if [ ! -f "$$DISK" ] || [ ! -f "$$STAMP" ]; then NEED=1; fi; \
	if [ $$NEED -eq 0 ] && [ "$$(cat "$$STAMP" 2>/dev/null)" != "$$PROFILE" ]; then NEED=1; fi; \
	if [ $$NEED -eq 0 ]; then \
		for dep in $(RUNIT_BIN_DIR)/runit-init $(RUNIT_STAGE_BIN)/runit_stage1 \
			$(RUNIT_STAGE_BIN)/runit_console_run \
			$(RUNIT_STAGE_BIN)/ir0_passwd $(RUNIT_STAGE_BIN)/doas \
			$(RUNIT_STAGE_BIN)/nano \
			$(IR0_USERSPACE_ROOT)/rootfs/etc/passwd \
			$(IR0_USERSPACE_ROOT)/rootfs/etc/doas.conf \
			$(IR0_USERSPACE_ROOT)/rootfs/etc/profile \
			$(IR0_USERSPACE_ROOT)/scripts/install-to-disk.sh \
			$(IR0_USERSPACE_ROOT)/scripts/stage-rootfs.sh \
			$(IR0_USERSPACE_ROOT)/scripts/pack-minix.sh \
			$(IR0_USERSPACE_ROOT)/packages/busybox/required_applets.txt \
			$(IR0_BUSYBOX_FULL_BIN) $(IR0_BUSYBOX_AUTH_BIN) \
			$$GUEST_MAN/.stamp; do \
			if [ -e "$$dep" ] && [ "$$dep" -nt "$$STAMP" ]; then NEED=1; break; fi; \
		done; \
	fi; \
	if [ $$NEED -eq 0 ] && [ "$${IR0_GUEST_MANDOCS:-1}" != "0" ]; then \
		if [ -d "$$GUEST_MAN/usr/share/man/cat7" ] && \
		   ls "$$GUEST_MAN/usr/share/man/cat7"/IR0-*.7 >/dev/null 2>&1; then \
			if [ ! -f "$$GUEST_MAN/.stamp" ] || \
			   [ "$$GUEST_MAN/.stamp" -nt "$$STAMP" ]; then \
				NEED=1; \
			fi; \
		fi; \
	fi; \
	if [ $$NEED -eq 0 ]; then \
		echo "  DISK    $$DISK up to date (cached)$${IR0_DEV_PERSIST:+ persist=1}"; \
	else \
		echo "  DISK    $$DISK (200M MINIX, profile=$$PROFILE)"; \
		dd if=/dev/zero of=$$DISK bs=1M count=200 status=none; \
		python3 scripts/inject_init_minix.py --format-large $$DISK; \
		if [ "$${IR0_GUEST_MANDOCS:-1}" != "0" ] && [ -d "$$GUEST_MAN/usr/share/man/cat7" ]; then \
			export IR0_GUEST_MANDOC_DIR="$$GUEST_MAN"; \
		else \
			unset IR0_GUEST_MANDOC_DIR || true; \
		fi; \
		$(IR0_USERSPACE_MAKE) rootfs DISK=$(KERNEL_ROOT)/$$DISK \
			PROFILE=$$PROFILE \
			IR0_GUEST_MANDOC_DIR=$${IR0_GUEST_MANDOC_DIR-}; \
		printf '%s\n' "$$PROFILE" > "$$STAMP"; \
	fi
	@$(MAKE) -s install-ken-games DISK=$${DISK:-disk.img} || \
		echo "  WARN    install-ken-games skipped (optional)"
	@$(MAKE) -s install-dennis-src DISK=$${DISK:-disk.img} || \
		echo "  WARN    install-dennis-src skipped (optional)"
	@echo "  DISK    load-userspace-runit OK"

# Doom under /usr/ken/games (Ken Thompson nod) + /usr/bin/doom for PATH.
install-ken-games: build-ktm-doom-interactive
	@chmod +x scripts/inject_ken_games_minix.sh
	@./scripts/inject_ken_games_minix.sh $${DISK:-disk.img}

# Dennis Ritchie playground: /heart/dennis + short-name samples under src/.
# Full IR0 tree: make run attaches virtfs mount_tag=dennis (see QEMU_DENNIS_9P).
install-dennis-src:
	@chmod +x scripts/inject_dennis_src_minix.sh
	@./scripts/inject_dennis_src_minix.sh $${DISK:-disk.img}

# Static GNU make for in-guest builds (musl).
build-gmake-static:
	@chmod +x scripts/build_gmake_static.sh
	@./scripts/build_gmake_static.sh

# Product disk + TinyCC + GNU make. Default profile is minimal (firstboot wizard).
# Lab autologin root: IR0_PRODUCT_PROFILE=development make load-userspace-devtools
load-userspace-devtools: build-tcc-fase52 build-gmake-static
	@if [ "$${IR0_DEV_PERSIST:-0}" != "1" ]; then rm -f disk.img.runit.stamp disk.img.devtools.stamp; fi
	@IR0_PRODUCT_PROFILE=$${IR0_PRODUCT_PROFILE:-minimal} IR0_NO_AUTOLOGIN=$${IR0_NO_AUTOLOGIN:-0} \
		$(MAKE) -s load-userspace-runit
	@chmod +x scripts/inject_devtools_minix.sh
	@./scripts/inject_devtools_minix.sh disk.img
	@printf 'devtools\n' > disk.img.devtools.stamp
	@echo "✓ load-userspace-devtools OK (profile=$${IR0_PRODUCT_PROFILE:-minimal}; /bin/tcc /bin/make …)"

smoke-runit-boot: load-userspace-runit kernel-x64-userspace.iso
	@echo "  SMOKE   runit PID1 boot (console + logger)..."
	@DISK=$$(mktemp /tmp/ir0-runit-smoke.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	$(SMOKE_QEMU_RUN) --log $(RUNIT_SMOKE_LOG) --timeout 50 --stale-sec 18 \
		--done RUNSV_CONSOLE_START --done RUNSV_LOGGER_START --done GETTY_READY -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK
	@	if grep -q "RUNIT_STAGE1_OK" $(RUNIT_SMOKE_LOG) && \
	    grep -q "RUNIT_STAGE2_OK" $(RUNIT_SMOKE_LOG) && \
	    grep -q "RUNSV_CONSOLE_START" $(RUNIT_SMOKE_LOG) && \
	    grep -q "RUNSV_LOGGER_START" $(RUNIT_SMOKE_LOG) && \
	    grep -q "GETTY_READY" $(RUNIT_SMOKE_LOG) && \
	    grep -qE "FSCK_OK|FSCK_SKIPPED" $(RUNIT_SMOKE_LOG) && \
	    grep -qE "FIRSTBOOT_SKIP|FIRSTBOOT_OK|FIRSTBOOT_PENDING" $(RUNIT_SMOKE_LOG) && \
	    grep -q "DRIVER_SUMMARY_OK" $(RUNIT_SMOKE_LOG); then \
		echo "✓ smoke-runit-boot passed (getty + stage1 helpers + driver summary)"; \
	else \
		echo "✗ smoke-runit-boot FAILED"; \
		grep -E 'RUNIT_|RUNSV_|GETTY_|FSCK_|FIRSTBOOT_|DRIVER_SUMMARY|KERNEL PANIC|panic' $(RUNIT_SMOKE_LOG) | tail -40; \
		exit 1; \
	fi

RUNIT_LOGIN_SMOKE_LOG = /tmp/runit-login-smoke.log
smoke-runit-login: load-userspace-runit kernel-x64-userspace.iso
	@echo "  SMOKE   runit Unix login (root / empty password)..."
	@if [ "$${IR0_DEV_PERSIST:-0}" != "1" ]; then rm -f $(IR0_DEV_DISK).runit.stamp; fi
	@chmod +x scripts/smoke_runit_login.py
	@IR0_PRODUCT_PROFILE=development IR0_NO_AUTOLOGIN=0 DISK=$(IR0_DEV_DISK) $(MAKE) -s load-userspace-runit
	@python3 scripts/smoke_runit_login.py --log $(RUNIT_LOGIN_SMOKE_LOG) --timeout 75 \
		--iso kernel-x64-userspace.iso --disk $(IR0_DEV_DISK)
	@echo "  LOG     $(RUNIT_LOGIN_SMOKE_LOG)"

smoke-tty-raw-probe: kernel-x64-userspace.iso
	@echo "  SMOKE   TTY raw probe (Ctrl-X=0x18 + ESC[A)..."
	@if [ "$${IR0_DEV_PERSIST:-0}" != "1" ]; then rm -f disk.img.runit.stamp; fi
	@IR0_PRODUCT_PROFILE=development IR0_NO_AUTOLOGIN=0 $(MAKE) -s load-userspace-runit
	@chmod +x scripts/smoke_tty_raw_probe.py
	@python3 scripts/smoke_tty_raw_probe.py --iso kernel-x64-userspace.iso --disk disk.img

smoke-desktop-nano: kernel-x64-userspace.iso
	@echo "  SMOKE   GNU nano Ctrl-X save path..."
	@if [ "$${IR0_DEV_PERSIST:-0}" != "1" ]; then rm -f disk.img.runit.stamp; fi
	@IR0_PRODUCT_PROFILE=development IR0_NO_AUTOLOGIN=0 $(MAKE) -s load-userspace-runit
	@chmod +x scripts/smoke_desktop_nano_mnt.py
	@python3 scripts/smoke_desktop_nano_mnt.py --iso kernel-x64-userspace.iso --disk disk.img

# Product binaries stress: top quit, man, dmesg/pipes, tcc hello, optional doom.
# Uses ISD development disk (top/man/tcc). Doom only if DOOM1.WAD is on the host.
USERSPACE_STABILITY_LOG = /tmp/ir0-userspace-stability.log
USERSPACE_STABILITY_DISK ?= $(IR0_USERSPACE_ROOT)/out/x86_64/images/development/disk.img
.PHONY: smoke-userspace-stability
smoke-userspace-stability: kernel-x64-userspace.iso
	@echo "  SMOKE   userspace stability (top/man/tcc/pipes[+doom])..."
	@test -f $(USERSPACE_STABILITY_DISK) || \
		{ echo "✗ missing $(USERSPACE_STABILITY_DISK) — pack ISD development first"; exit 2; }
	@chmod +x scripts/smoke_userspace_stability.py scripts/smoke_desktop_cmd_matrix.py
	@python3 scripts/smoke_userspace_stability.py \
		--iso kernel-x64-userspace.iso \
		--disk $(USERSPACE_STABILITY_DISK) \
		--log $(USERSPACE_STABILITY_LOG) \
		--auto-doom
	@echo "  LOG     $(USERSPACE_STABILITY_LOG)"

# Interactive firstboot wizard (HMP): password Confirm must not #UD under IRQ1.
FIRSTBOOT_WIZARD_LOG = /tmp/ir0-firstboot-wizard.log
FIRSTBOOT_WIZARD_DISK ?= $(IR0_USERSPACE_ROOT)/out/x86_64/images/desktop/disk.img
.PHONY: smoke-firstboot-wizard
smoke-firstboot-wizard: kernel-x64-userspace.iso
	@echo "  SMOKE   firstboot wizard (username + password Confirm)..."
	@test -f $(FIRSTBOOT_WIZARD_DISK) || \
		{ echo "✗ missing $(FIRSTBOOT_WIZARD_DISK) — pack ISD desktop first"; exit 2; }
	@chmod +x scripts/smoke_firstboot_wizard.py
	@python3 scripts/smoke_firstboot_wizard.py \
		--iso kernel-x64-userspace.iso \
		--disk $(FIRSTBOOT_WIZARD_DISK) \
		--log $(FIRSTBOOT_WIZARD_LOG)
	@echo "  LOG     $(FIRSTBOOT_WIZARD_LOG)"

# Non-root path: crypt(3) auth + setuid drop + /etc/profile PS1 (typed via monitor).
RUNIT_LOGIN_NONROOT_SMOKE_LOG = /tmp/runit-login-nonroot-smoke.log
smoke-runit-login-nonroot: load-userspace-runit kernel-x64-userspace.iso
	@echo "  SMOKE   runit Unix login non-root (ivan, crypt + uid drop)..."
	@chmod +x scripts/smoke_runit_login_nonroot.py
	@python3 scripts/smoke_runit_login_nonroot.py --log $(RUNIT_LOGIN_NONROOT_SMOKE_LOG) \
		--timeout 90 --iso kernel-x64-userspace.iso --disk disk.img
	@echo "  LOG     $(RUNIT_LOGIN_NONROOT_SMOKE_LOG)"

smoke-klog-ktm-off:
	@set -e; \
	backup=$$(mktemp /tmp/ir0-klog-config.XXXXXX); \
	if [ -f .config ]; then cp .config $$backup; else : > $$backup; fi; \
	restore() { if [ -s $$backup ]; then cp $$backup .config; else rm -f .config; fi; rm -f $$backup; }; \
	trap restore EXIT; \
	$(MAKE) -s ir0_defconfig PROFILE=desktop BOARD=; \
	python3 scripts/kconfig/menuconfig.py --set KTM=n; \
	$(MAKE) -s kernel-x64-userspace.iso load-userspace-runit; \
	DISK=$$(mktemp /tmp/ir0-klog-ktm-off.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	$(SMOKE_QEMU_RUN) --log /tmp/klog-ktm-off.log --timeout 50 --stale-sec 18 \
		--done GETTY_READY -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK; \
	grep -q 'GETTY_READY' /tmp/klog-ktm-off.log; \
	grep -q '\[#[0-9][0-9]*\] \[EARLY_ARCH\]' /tmp/klog-ktm-off.log; \
	echo "✓ smoke-klog-ktm-off passed"

# System power MVP: runit service calls reboot(2) HALT; PASS = serial tags (QEMU -no-reboot).
RUNIT_POWER_SMOKE_LOG = /tmp/runit-power-smoke.log
smoke-runit-power: load-userspace-runit kernel-x64-userspace.iso
	@echo "  SMOKE   runit + sys_reboot HALT (power_manag)..."
	@DISK=$$(mktemp /tmp/ir0-runit-power.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	 \
	$(IR0_USERSPACE_ROOT)/scripts/inject-smoke-service.sh $$DISK power \
		$(RUNIT_STAGE_BIN)/runit_power_run \
		$(RUNIT_STAGE_BIN)/runit_power_smoke bin/power-smoke; \
	rm -f $(RUNIT_POWER_SMOKE_LOG); \
	$(SMOKE_QEMU_RUN) --log $(RUNIT_POWER_SMOKE_LOG) --timeout 60 --stale-sec 20 \
		--done SYSTEM_SHUTDOWN_HALT -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK; \
	if grep -q "RUNIT_STAGE2_OK" $(RUNIT_POWER_SMOKE_LOG) && \
	    grep -q "POWER_SMOKE_CALL" $(RUNIT_POWER_SMOKE_LOG) && \
	    grep -q "SYSTEM_SYNC_OK" $(RUNIT_POWER_SMOKE_LOG) && \
	    grep -q "SYSTEM_SHUTDOWN_HALT" $(RUNIT_POWER_SMOKE_LOG); then \
		echo "✓ smoke-runit-power passed"; \
	else \
		echo "✗ smoke-runit-power FAILED"; \
		grep -E 'RUNIT_|RUNSV_|POWER_|SYSTEM_|KERNEL PANIC|panic' $(RUNIT_POWER_SMOKE_LOG) | tail -40; \
		exit 1; \
	fi

# BusyBox halt applet → sys_reboot (requires CONFIG_HALT in fase58 fragment).
RUNIT_BB_HALT_LOG = /tmp/runit-busybox-halt-smoke.log
smoke-runit-busybox-halt: load-userspace-runit kernel-x64-userspace.iso
	@echo "  SMOKE   runit + BusyBox halt applet..."
	@DISK=$$(mktemp /tmp/ir0-runit-bb-halt.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	 \
	$(IR0_USERSPACE_ROOT)/scripts/inject-smoke-service.sh $$DISK bbhalt \
		$(RUNIT_STAGE_BIN)/runit_busybox_halt_run \
		$(RUNIT_STAGE_BIN)/runit_busybox_halt_smoke bin/bb-halt; \
	rm -f $(RUNIT_BB_HALT_LOG); \
	$(SMOKE_QEMU_RUN) --log $(RUNIT_BB_HALT_LOG) --timeout 60 --stale-sec 20 \
		--done SYSTEM_SHUTDOWN_HALT -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK; \
	if grep -q "BUSYBOX_HALT_SMOKE_CALL" $(RUNIT_BB_HALT_LOG) && \
	    grep -q "SYSTEM_SYNC_OK" $(RUNIT_BB_HALT_LOG) && \
	    grep -q "SYSTEM_SHUTDOWN_HALT" $(RUNIT_BB_HALT_LOG); then \
		echo "✓ smoke-runit-busybox-halt passed"; \
	else \
		echo "✗ smoke-runit-busybox-halt FAILED"; \
		grep -E 'BUSYBOX_|SYSTEM_|RUNIT_|KERNEL PANIC|panic' $(RUNIT_BB_HALT_LOG) | tail -40; \
		exit 1; \
	fi

# BusyBox poweroff applet → sys_reboot(POWER_OFF).
RUNIT_BB_POWEROFF_LOG = /tmp/runit-busybox-poweroff-smoke.log
smoke-runit-busybox-poweroff: load-userspace-runit kernel-x64-userspace.iso
	@echo "  SMOKE   runit + BusyBox poweroff applet..."
	@DISK=$$(mktemp /tmp/ir0-runit-bb-pwroff.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	 \
	$(IR0_USERSPACE_ROOT)/scripts/inject-smoke-service.sh $$DISK bbpwroff \
		$(RUNIT_STAGE_BIN)/runit_busybox_poweroff_run \
		$(RUNIT_STAGE_BIN)/runit_busybox_poweroff_smoke bin/bb-pwroff; \
	rm -f $(RUNIT_BB_POWEROFF_LOG); \
	$(SMOKE_QEMU_RUN) --log $(RUNIT_BB_POWEROFF_LOG) --timeout 60 --stale-sec 20 \
		--done SYSTEM_SHUTDOWN_POWEROFF -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK; \
	if grep -q "BUSYBOX_POWEROFF_SMOKE_CALL" $(RUNIT_BB_POWEROFF_LOG) && \
	    grep -q "SYSTEM_SYNC_OK" $(RUNIT_BB_POWEROFF_LOG) && \
	    grep -q "SYSTEM_SHUTDOWN_POWEROFF" $(RUNIT_BB_POWEROFF_LOG); then \
		echo "✓ smoke-runit-busybox-poweroff passed"; \
	else \
		echo "✗ smoke-runit-busybox-poweroff FAILED"; \
		grep -E 'BUSYBOX_|SYSTEM_|RUNIT_|KERNEL PANIC|panic' $(RUNIT_BB_POWEROFF_LOG) | tail -40; \
		exit 1; \
	fi

# BusyBox reboot applet → sys_reboot(RESTART).
RUNIT_BB_REBOOT_LOG = /tmp/runit-busybox-reboot-smoke.log
smoke-runit-busybox-reboot: load-userspace-runit kernel-x64-userspace.iso
	@echo "  SMOKE   runit + BusyBox reboot applet..."
	@DISK=$$(mktemp /tmp/ir0-runit-bb-reboot.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	 \
	$(IR0_USERSPACE_ROOT)/scripts/inject-smoke-service.sh $$DISK bbreboot \
		$(RUNIT_STAGE_BIN)/runit_busybox_reboot_run \
		$(RUNIT_STAGE_BIN)/runit_busybox_reboot_smoke bin/bb-reboot; \
	rm -f $(RUNIT_BB_REBOOT_LOG); \
	$(SMOKE_QEMU_RUN) --log $(RUNIT_BB_REBOOT_LOG) --timeout 60 --stale-sec 20 \
		--done SYSTEM_SHUTDOWN_REBOOT -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK; \
	if grep -q "BUSYBOX_REBOOT_SMOKE_CALL" $(RUNIT_BB_REBOOT_LOG) && \
	    grep -q "SYSTEM_SYNC_OK" $(RUNIT_BB_REBOOT_LOG) && \
	    grep -q "SYSTEM_SHUTDOWN_REBOOT" $(RUNIT_BB_REBOOT_LOG); then \
		echo "✓ smoke-runit-busybox-reboot passed"; \
	else \
		echo "✗ smoke-runit-busybox-reboot FAILED"; \
		grep -E 'BUSYBOX_|SYSTEM_|RUNIT_|KERNEL PANIC|panic' $(RUNIT_BB_REBOOT_LOG) | tail -40; \
		exit 1; \
	fi

# TinyCC in guest compiles power_halt.c → /dev/ktm + reboot(HALT).
build-init-tcc-power-halt:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  HARNESS Building TCC power-halt ($(TCC_POWER_HALT_HARNESS_BIN))"
	@$(MUSL_CC) -static -Os -o $(TCC_POWER_HALT_HARNESS_BIN) $(INIT_TCC_POWER_HALT_SRC)
	@file $(TCC_POWER_HALT_HARNESS_BIN) | grep -q ELF
	@echo "✓ build-init-tcc-power-halt OK"

smoke-tcc-power-halt: build-runit build-init-tcc-power-halt build-tcc-fase52 $(KERNEL_USERSPACE_ISO)
	@echo "  SMOKE   TCC guest compile → KTM → reboot HALT..."
	@test -f $(TCC_POWER_HALT_HARNESS_BIN) || (echo "✗ missing harness"; exit 1)
	@strings $(TCC_POWER_HALT_HARNESS_BIN) 2>/dev/null | grep -q "TCC_POWER_HARNESS_ID" || \
		(echo "✗ $(TCC_POWER_HALT_HARNESS_BIN) is not TCC power harness"; exit 1)
	@test -d $(KTM_TCC_TCC_STAGE)/bin || (echo "✗ missing $(KTM_TCC_TCC_STAGE) — run build-tcc-fase52"; exit 1)
	@test -f $(RUNIT_STAGE_BIN)/runit_tcc_power_run || (echo "✗ missing runit_tcc_power_run — run build-runit"; exit 1)
	@DISK=$$(mktemp /tmp/ir0-tcc-power.XXXXXX.img); \
	dd if=/dev/zero of=$$DISK bs=1M count=200 status=none && \
	python3 scripts/inject_init_minix.py --format-large $$DISK && \
	FASE50_BUSYBOX_BIN=$(FASE50_BUSYBOX_BIN) $(IR0_USERSPACE_ROOT)/scripts/install-to-disk.sh $$DISK && \
	 \
	$(IR0_USERSPACE_ROOT)/scripts/inject-smoke-service.sh $$DISK tccpower \
		$(RUNIT_STAGE_BIN)/runit_tcc_power_run $(TCC_POWER_HALT_HARNESS_BIN) bin/tccph && \
	find $(KTM_TCC_TCC_STAGE) -type f | sort | while read -r f; do \
		rel="$${f#$(KTM_TCC_TCC_STAGE)/}"; \
		python3 scripts/inject_init_minix.py $$DISK "$$f" "$$rel"; \
	done && \
	python3 scripts/verify_minix_rootfs.py $$DISK /sbin/init /bin/tccph /bin/tcc \
		/etc/runit/sv/tccpower/run && \
	rm -f $(TCC_POWER_HALT_LOG); \
	$(SMOKE_QEMU_RUN) --log $(TCC_POWER_HALT_LOG) --profile tcc-power-halt \
		--done SYSTEM_SHUTDOWN_HALT -- \
		$(QEMU) -cdrom $(KERNEL_USERSPACE_ISO) \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK; \
	if grep -q "RUNIT_STAGE2_OK" $(TCC_POWER_HALT_LOG) && \
	    grep -q "RUNSV_TCC_POWER_START" $(TCC_POWER_HALT_LOG) && \
	    grep -q "TCC_POWER_LINK_OK" $(TCC_POWER_HALT_LOG) && \
	    grep -q "POWER_TCC_COMPILE_OK" $(TCC_POWER_HALT_LOG) && \
	    grep -q "POWER_TCC_HALT_CALL" $(TCC_POWER_HALT_LOG) && \
	    grep -q "SYSTEM_SYNC_OK" $(TCC_POWER_HALT_LOG) && \
	    grep -q "SYSTEM_SHUTDOWN_HALT" $(TCC_POWER_HALT_LOG) && \
	    grep -q "POWER_TCC_KTM_OK" $(TCC_POWER_HALT_LOG) && \
	    grep -q "KTM_USERDEV_OK" $(TCC_POWER_HALT_LOG); then \
		echo "✓ smoke-tcc-power-halt passed"; \
	else \
		echo "✗ smoke-tcc-power-halt FAILED"; \
		grep -E 'TCC_POWER_|POWER_TCC_|SYSTEM_|RUNIT_|RUNSV_|KERNEL PANIC|panic' $(TCC_POWER_HALT_LOG) | tail -50; \
		exit 1; \
	fi

# Hybrid KTM name for critical battery (same runit+TCC recipe)
ktm-userdev-tcc-power-halt-run: smoke-tcc-power-halt
	@echo "✓ ktm-userdev-tcc-power-halt-run (hybrid runit)"

# MM vertical slice: lazy alloc (brk + anon mmap) + fork COW (FASE40 A–F).
smoke-mm-cow-lazy: kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then \
		echo "  DISK    Creating disk.img..."; \
		$(MAKE) -s create-disk; \
	fi
	@echo "  SMOKE   MM lazy alloc + fork COW (heap + mmap + FASE40, lazy kernel)..."
	@$(MAKE) -s build-init-heap-smoke
	@DISK=$$(mktemp /tmp/ir0-mm-cow-lazy.XXXXXX.img); \
	truncate -s 200M $$DISK; \
	python3 scripts/inject_init_minix.py --format-large $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	rm -f $(HEAP_SMOKE_LOG); \
	$(SMOKE_QEMU_RUN) --log $(HEAP_SMOKE_LOG) --timeout 90 --done 'page_present=1' -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	HEAP_OK=0; \
	grep -q "FASE39_HEAP" $(HEAP_SMOKE_LOG) && grep -q "page_present=1" $(HEAP_SMOKE_LOG) && HEAP_OK=1; \
	$(MAKE) -s build-init-mmap-smoke; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	rm -f $(MMAP_SMOKE_LOG); \
	$(SMOKE_QEMU_RUN) --log $(MMAP_SMOKE_LOG) --timeout 90 \
		--done '[PF] userspace segv pid=' -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	MMAP_OK=0; \
	grep -q "FASE39_MMAP mapped=1" $(MMAP_SMOKE_LOG) && \
	grep -q "FASE39_MMAP.*verify=1" $(MMAP_SMOKE_LOG) && \
	grep -q "\\[PF\\] userspace segv pid=" $(MMAP_SMOKE_LOG) && MMAP_OK=1; \
	$(MAKE) -s build-init-fork-mem-smoke; \
	printf 'PARENT-FILE-OK!' > /tmp/ir0-fase40.dat; \
	python3 scripts/inject_init_minix.py $$DISK /tmp/ir0-fase40.dat etc/f40.dat; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	rm -f $(FORK_MEM_SMOKE_LOG); \
	$(SMOKE_QEMU_RUN) --log $(FORK_MEM_SMOKE_LOG) --timeout 180 \
		--done 'FASE40_SUMMARY A=0 B=0 C=0 D=0' -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK; \
	FORK_OK=0; \
	grep -q "FASE40_SUMMARY A=0 B=0 C=0 D=0" $(FORK_MEM_SMOKE_LOG) && FORK_OK=1; \
	if [ "$$HEAP_OK" != 1 ]; then echo "✗ smoke-mm-cow-lazy FAILED (heap/lazy brk)"; exit 1; fi; \
	if [ "$$MMAP_OK" != 1 ]; then echo "✗ smoke-mm-cow-lazy FAILED (lazy mmap)"; exit 1; fi; \
	if [ "$$FORK_OK" != 1 ]; then \
		echo "✗ smoke-mm-cow-lazy FAILED (fork COW / FASE40)"; \
		grep "FASE40" $(FORK_MEM_SMOKE_LOG) | tail -15; exit 1; \
	fi; \
	echo "✓ smoke-mm-cow-lazy passed (lazy brk + lazy mmap + fork COW FASE40 A–F)"

.PHONY: build/fat16_smoke.img smoke-fat16-mount

build/fat16_smoke.img:
	@chmod +x scripts/create_fat16_smoke_disk.sh
	@./scripts/create_fat16_smoke_disk.sh $(FAT16_SMOKE_IMG)

# GPT partition parse smoke (secondary IDE disk).
GPT_SMOKE_IMG = build/gpt_smoke.img
GPT_SMOKE_LOG = /tmp/gpt-partition-smoke.log
.PHONY: build/gpt_smoke.img smoke-gpt-partition

build/gpt_smoke.img:
	@python3 scripts/create_gpt_smoke_disk.py $(GPT_SMOKE_IMG)

smoke-gpt-partition: kernel-x64-userspace.iso build/gpt_smoke.img
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@echo "  SMOKE   GPT partition table on hdb..."
	@DISK=$$(mktemp /tmp/ir0-gpt-smoke.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	rm -f $(GPT_SMOKE_LOG); \
	$(SMOKE_QEMU_RUN) --log $(GPT_SMOKE_LOG) --timeout 60 --stale-sec 20 \
		--done GPT_PARTITION_OK -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-drive file=$(GPT_SMOKE_IMG),format=raw,if=ide,index=1 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK; \
	if grep -q "GPT_PARTITION_OK" $(GPT_SMOKE_LOG); then \
		echo "✓ smoke-gpt-partition passed"; \
	else \
		echo "✗ smoke-gpt-partition FAILED"; \
		grep -E 'GPT_|ATA_|PARTITION|panic' $(GPT_SMOKE_LOG) | tail -30; \
		exit 1; \
	fi

EXT2_SMOKE_IMG = build/ext2_smoke.img
EXT2_SMOKE_LOG = /tmp/ext2-smoke.log
INIT_EXT2_SMOKE_SRC = setup/pid1/init_ext2_smoke.c
.PHONY: build/ext2_smoke.img smoke-ext2-mount build-init-ext2-smoke

build/ext2_smoke.img:
	@chmod +x scripts/create_ext2_smoke_disk.sh
	@./scripts/create_ext2_smoke_disk.sh $(EXT2_SMOKE_IMG)

build-init-ext2-smoke:
	@if [ -z "$(MUSL_CC)" ]; then echo "✗ musl cc missing"; exit 1; fi
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_EXT2_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-ext2-smoke OK"

smoke-ext2-mount: kernel-x64-userspace.iso build/ext2_smoke.img
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@echo "  SMOKE   EXT2 mount + read HELLO.TXT on /dev/hdb..."
	@$(MAKE) -s build-init-ext2-smoke
	@DISK=$$(mktemp /tmp/ir0-ext2-smoke.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	rm -f $(EXT2_SMOKE_LOG); \
	$(SMOKE_QEMU_RUN) --log $(EXT2_SMOKE_LOG) --timeout 90 \
		--done 'EXT2OK' -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-drive file=$(EXT2_SMOKE_IMG),format=raw,if=ide,index=1 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rc=$$?; rm -f $$DISK; \
	if tr -d '\n\r' < $(EXT2_SMOKE_LOG) | grep -q 'EXT2OK'; then \
		echo "✓ smoke-ext2-mount passed"; \
	elif [ $$rc -ne 0 ]; then \
		echo "✗ smoke-ext2-mount FAILED (QEMU/autokill)"; exit $$rc; \
	else \
		echo "✗ smoke-ext2-mount FAILED (tag missing)"; exit 1; \
	fi

POSIX_DEPTH_SMOKE_LOG = /tmp/posix-depth-smoke.log
INIT_POSIX_DEPTH_SMOKE_SRC = setup/pid1/init_posix_depth_smoke.c
.PHONY: smoke-posix-depth build-init-posix-depth-smoke

build-init-posix-depth-smoke:
	@if [ -z "$(MUSL_CC)" ]; then echo "✗ musl cc missing"; exit 1; fi
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_POSIX_DEPTH_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-posix-depth-smoke OK"

smoke-posix-depth: kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@echo "  SMOKE   POSIX depth (prlimit/epoll/pselect/pty)..."
	@$(MAKE) -s build-init-posix-depth-smoke
	@DISK=$$(mktemp /tmp/ir0-posix-depth.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	rm -f $(POSIX_DEPTH_SMOKE_LOG); \
	$(SMOKE_QEMU_RUN) --log $(POSIX_DEPTH_SMOKE_LOG) --timeout 90 \
		--done 'POSIX_DEPTH_OK' -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 128M -no-reboot -net none; \
	rc=$$?; rm -f $$DISK; \
	if tr -d '\n\r' < $(POSIX_DEPTH_SMOKE_LOG) | grep -q 'POSIX_DEPTH_OK'; then \
		echo "✓ smoke-posix-depth passed"; \
	elif [ $$rc -ne 0 ]; then \
		echo "✗ smoke-posix-depth FAILED (QEMU/autokill)"; exit $$rc; \
	else \
		echo "✗ smoke-posix-depth FAILED (tag missing)"; exit 1; \
	fi

PTY_WINSZ_SMOKE_LOG = /tmp/pty-winsz-smoke.log

.PHONY: smoke-kill-sigterm build-init-kill-sigterm-smoke
build-init-kill-sigterm-smoke:
	@if [ -z "$(MUSL_CC)" ]; then echo "✗ musl cc missing"; exit 1; fi
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) setup/pid1/init_kill_sigterm_smoke.c
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-kill-sigterm-smoke OK"

KILL_SIGTERM_SMOKE_LOG = /tmp/kill-sigterm-smoke.log
smoke-kill-sigterm: kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@echo "  SMOKE   kill(2) SIGTERM + kill(pid,0) (no #UD)..."
	@$(MAKE) -s build-init-kill-sigterm-smoke
	@DISK=$$(mktemp /tmp/ir0-kill-sigterm.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	rm -f $(KILL_SIGTERM_SMOKE_LOG); \
	$(SMOKE_QEMU_RUN) --log $(KILL_SIGTERM_SMOKE_LOG) --timeout 60 \
		--done 'KILL_SIGTERM_OK' -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none || true; \
	rm -f $$DISK; \
	if grep -q "KILL_PROBE0_OK" $(KILL_SIGTERM_SMOKE_LOG) && \
	   grep -q "KILL_SIGTERM_OK" $(KILL_SIGTERM_SMOKE_LOG) && \
	   ! grep -qiE 'undefined opcode|#UD|KERNEL PANIC|panic' $(KILL_SIGTERM_SMOKE_LOG); then \
		echo "✓ smoke-kill-sigterm passed"; \
	else \
		echo "✗ smoke-kill-sigterm FAILED"; \
		grep -E 'KILL_|panic|#UD|undefined' $(KILL_SIGTERM_SMOKE_LOG) | tail -40; \
		exit 1; \
	fi

.PHONY: smoke-pty-winsz build-init-pty-winsz-smoke
build-init-pty-winsz-smoke:
	@if [ -z "$(MUSL_CC)" ]; then echo "✗ musl cc missing"; exit 1; fi
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) setup/pid1/init_pty_winsz_smoke.c
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-pty-winsz-smoke OK"

smoke-pty-winsz: kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@echo "  SMOKE   PTY TIOCSWINSZ + SIGWINCH..."
	@$(MAKE) -s build-init-pty-winsz-smoke
	@DISK=$$(mktemp /tmp/ir0-pty-winsz.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	rm -f $(PTY_WINSZ_SMOKE_LOG); \
	$(SMOKE_QEMU_RUN) --log $(PTY_WINSZ_SMOKE_LOG) --timeout 60 \
		--done 'PTY_WINSZ_OK' -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 128M -no-reboot -net none; \
	rc=$$?; rm -f $$DISK; \
	if grep -q 'PTY_WINSZ_OK' $(PTY_WINSZ_SMOKE_LOG) && \
	   grep -q 'PTY_WINCH_SENT' $(PTY_WINSZ_SMOKE_LOG); then \
		echo "✓ smoke-pty-winsz passed"; \
	else \
		echo "✗ smoke-pty-winsz FAILED"; \
		grep -E 'PTY_|errno|panic' $(PTY_WINSZ_SMOKE_LOG) | tail -30; exit 1; \
	fi

.PHONY: build-init-epoll-basic
EPOLL_SMOKE_LOG = /tmp/epoll-basic-smoke.log
build-init-epoll-basic:
	@if [ -z "$(MUSL_CC)" ]; then echo "✗ musl cc missing"; exit 1; fi
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) setup/pid1/init_epoll_basic_smoke.c
	@echo "✓ build-init-epoll-basic OK (legacy inject; prefer ktm-userdev-epoll-run)"

.PHONY: smoke-prlimit build-init-prlimit
PRLIMIT_SMOKE_LOG = /tmp/prlimit-smoke.log
build-init-prlimit:
	@if [ -z "$(MUSL_CC)" ]; then echo "✗ musl cc missing"; exit 1; fi
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) setup/pid1/init_prlimit_smoke.c
	@echo "✓ build-init-prlimit OK"

smoke-prlimit: kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@echo "  SMOKE   prlimit64 RLIMIT_NOFILE..."
	@$(MAKE) -s build-init-prlimit
	@DISK=$$(mktemp /tmp/ir0-prlimit.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	rm -f $(PRLIMIT_SMOKE_LOG); \
	$(SMOKE_QEMU_RUN) --log $(PRLIMIT_SMOKE_LOG) --timeout 45 --stale-sec 15 \
		--done PRLIMIT_OK -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 128M -no-reboot -net none; \
	rm -f $$DISK; \
	if grep -q "PRLIMIT_OK" $(PRLIMIT_SMOKE_LOG); then \
		echo "✓ smoke-prlimit passed"; \
	else \
		echo "✗ smoke-prlimit FAILED"; \
		grep -E 'PRLIMIT_|panic' $(PRLIMIT_SMOKE_LOG) | tail -30; exit 1; \
	fi

.PHONY: smoke-robust-list build-init-robust-list
ROBUST_SMOKE_LOG = /tmp/robust-list-smoke.log
build-init-robust-list:
	@if [ -z "$(MUSL_CC)" ]; then echo "✗ musl cc missing"; exit 1; fi
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) setup/pid1/init_robust_list_smoke.c
	@echo "✓ build-init-robust-list OK"

smoke-robust-list: kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@echo "  SMOKE   set/get_robust_list + exit cleanup..."
	@$(MAKE) -s build-init-robust-list
	@DISK=$$(mktemp /tmp/ir0-robust.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	rm -f $(ROBUST_SMOKE_LOG); \
	$(SMOKE_QEMU_RUN) --log $(ROBUST_SMOKE_LOG) --timeout 45 --stale-sec 15 \
		--done ROBUST_LIST_OK -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 128M -no-reboot -net none; \
	rm -f $$DISK; \
	if grep -q "ROBUST_LIST_OK" $(ROBUST_SMOKE_LOG) && \
	    grep -q "ROBUST_LIST_EXIT_OK" $(ROBUST_SMOKE_LOG); then \
		echo "✓ smoke-robust-list passed"; \
	else \
		echo "✗ smoke-robust-list FAILED"; \
		grep -E 'ROBUST_|panic' $(ROBUST_SMOKE_LOG) | tail -30; exit 1; \
	fi

# Minimal ARM64 boot image (identity map — full ARCH_OBJS need freestanding libc).
# Board: ARM64_BOARD=qemu-virt|rpi4|rpi5 (see scripts/make/arm64-board.mk).
ARM64_BOOT_CFLAGS = -ffreestanding -nostdlib -fno-builtin -O2 -mgeneral-regs-only \
	-DIR0_FREESTANDING_BOOT=1 $(ARM64_BOARD_CFLAGS) \
	-I. -Iarch/arm64/sources -Iarch/common -Iincludes -Iincludes/ir0 -Iktm/include
ARM64_BOOT_ASFLAGS = -ffreestanding -nostdlib -mgeneral-regs-only
# QEMU virt: pin GICv2 (default may be v3 — freestanding Dist/CPU iface is v2).
ARM64_QEMU_MACHINE = virt,gic-version=2
# F7b curated portable objs (not ALL_OBJS).
ARM64_SLICE_OBJS = \
	arch/arm64/sources/slice_hello.o \
	arch/arm64/sources/board.o \
	arch/arm64/sources/pl011.o \
	arch/arm64/sources/serial_io_arm64.o
ARM64_PORTABLE_OBJS = \
	$(ARM64_SLICE_OBJS) \
	arch/arm64/sources/gic_v2.o \
	arch/arm64/sources/timer.o \
	arch/arm64/sources/portable_string.o
.PHONY: kernel-arm64-boot.bin kernel-arm64-mmu.bin kernel-arm64-vbar.bin \
	kernel-arm64-el0.bin kernel-arm64-slice.bin kernel-arm64-port.bin \
	kernel-arm64-gic.bin kernel-arm64-syscall.bin \
	arm64-slice-compile arm64-portable-compile arm64-all-objs-probe \
	smoke-arm64-boot smoke-arm64-mmu smoke-arm64-vbar smoke-arm64-el0 \
	smoke-arm64-slice smoke-arm64-port smoke-arm64-gic smoke-arm64-syscall \
	smoke-arm64-nanosleep smoke-arm64
arm64-slice-compile:
	@echo "  CC      ARM64_SLICE_OBJS (F7b)"
	@for f in $(ARM64_SLICE_OBJS); do \
		src=$${f%.o}.c; \
		echo "  CC      $$src"; \
		aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c $$src -o $$f || exit 1; \
	done
	@echo "  CC      includes/string.c (compile-only probe, not linked — needs oops.h)"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) \
		-c includes/string.c -o /tmp/ir0-string-arm64-probe.o
	@echo "✓ arm64-slice-compile OK (slice+pl011+serial_io + string.c probe)"

arm64-portable-compile: arm64-slice-compile
	@echo "  CC      ARM64_PORTABLE_OBJS (F7b P2 — not ALL_OBJS)"
	@for f in $(ARM64_PORTABLE_OBJS); do \
		src=$${f%.o}.c; \
		echo "  CC      $$src"; \
		aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c $$src -o $$f || exit 1; \
	done
	@echo "  CC      arch/arm64/sources/irq_portable_stubs.c (INTERRUPT_OBJS_ARM64)"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/irq_portable_stubs.c \
		-o arch/arm64/sources/irq_portable_stubs.o
	@echo "✓ arm64-portable-compile OK (slice+pl011+serial+gic+timer+portable_string+irq stubs)"

# Pack E: probe MEMORY_OBJS for ARCH=arm64 (compile-only). Not a full ALL_OBJS link.
arm64-all-objs-probe: arm64-portable-compile
	@echo "  PROBE   arm64-all-objs-probe (MEMORY_OBJS compile-only)"
	@rm -f /tmp/ir0-arm64-all-objs-probe.log
	@echo "INTERRUPT_OBJS wall: cleared (irq_portable_stubs replaces lidt/isr_stubs_64)" \
		> /tmp/ir0-arm64-all-objs-probe.log
	@fail=0; \
	for src in mm/allocator.c mm/paging.c mm/pmm.c mm/kmem.c; do \
		echo "  CC      $$src (probe)"; \
		if ! aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -DARCH_ARM64=1 \
			-c $$src -o /tmp/ir0-arm64-probe-$$$$.o >> /tmp/ir0-arm64-all-objs-probe.log 2>&1; then \
			echo "FIRST_DIVERGENCE: $$src" | tee -a /tmp/ir0-arm64-all-objs-probe.log; \
			fail=1; \
			break; \
		fi; \
		rm -f /tmp/ir0-arm64-probe-$$$$.o; \
	done; \
	if [ $$fail -eq 0 ]; then \
		for src in kernel/main.c kernel/lib/open_flags.c \
			kernel/errno.c kernel/credentials.c \
			sched/switch/switch_arm64.c \
			kernel/clock_wait.c kernel/driver_registry.c \
			kernel/lib/copy_user.c \
			kernel/lib/logging.c kernel/futex.c \
			kernel/net_compat.c; do \
			echo "  CC      $$src (KERNEL probe)"; \
			if ! aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -DARCH_ARM64=1 \
				-I$(KERNEL_ROOT)/includes -I$(KERNEL_ROOT)/includes/ir0 \
				-I$(KERNEL_ROOT)/arch/common \
				-I$(KERNEL_ROOT)/fs -I$(KERNEL_ROOT)/net \
				-I$(KERNEL_ROOT)/sched -I$(KERNEL_ROOT) \
				-c $$src -o /tmp/ir0-arm64-probe-$$$$.o >> /tmp/ir0-arm64-all-objs-probe.log 2>&1; then \
				echo "FIRST_DIVERGENCE: $$src" | tee -a /tmp/ir0-arm64-all-objs-probe.log; \
				fail=1; \
				break; \
			fi; \
			rm -f /tmp/ir0-arm64-probe-$$$$.o; \
		done; \
	fi; \
	if [ $$fail -eq 0 ]; then \
		echo "MEMORY_OBJS+KERNEL sample(+errno/cred/switch/clock/drv/copy_user/logging/futex/net_compat): compile OK (not linked)" \
			| tee -a /tmp/ir0-arm64-all-objs-probe.log; \
		echo "NEXT: make ARCH=arm64 kernel-arm64-all.bin (portable ALL link; x86 drivers still out)"; \
		echo "✓ arm64-all-objs-probe: interrupt wall cleared; probe compile OK"; \
	else \
		echo "✗ arm64-all-objs-probe: see /tmp/ir0-arm64-all-objs-probe.log"; \
		tail -40 /tmp/ir0-arm64-all-objs-probe.log; \
		exit 1; \
	fi

kernel-arm64-boot.bin: arch/arm64/sources/boot_stub.c arch/arm64/sources/mmu_early.c \
		arch/arm64/sources/mmu_early.h arch/arm64/sources/exc_early.c \
		arch/arm64/sources/exc_early.h arch/arm64/sources/slice_hello.c \
		arch/arm64/sources/slice_hello.h arch/arm64/sources/pl011.c \
		arch/arm64/sources/pl011.h arch/arm64/sources/serial_io_arm64.c \
		arch/arm64/sources/timer.c arch/arm64/sources/timer.h \
		arch/arm64/sources/gic_v2.c arch/arm64/sources/gic_v2.h \
		arch/arm64/sources/syscall_early.c arch/arm64/sources/syscall_early.h \
		arch/arm64/sources/mm_ops.c arch/arm64/sources/vectors.S \
		arch/arm64/sources/switch_early.c arch/arm64/sources/switch_early.h \
		arch/arm64/sources/switch_early.S \
		arch/arm64/sources/process_early.c arch/arm64/sources/process_early.h \
		arch/arm64/sources/elf_load_early.c arch/arm64/sources/elf_load_early.h \
		arch/arm64/sources/hello_embed.S \
		arch/arm64/sources/busybox_embed.S \
		arch/arm64/sources/busybox_load_early.c \
		arch/arm64/sources/rootfs_early.c arch/arm64/sources/rootfs_early.h \
		arch/arm64/sources/rr_early.c arch/arm64/sources/rr_early.h \
		arch/arm64/sources/rr_early_stubs.c \
		arch/arm64/sources/virtio_blk_early.c arch/arm64/sources/virtio_blk_early.h \
		arch/arm64/sources/virtio_net_early.c arch/arm64/sources/virtio_net_early.h \
		drivers/virtio/virtio_mmio.c includes/ir0/virtio_mmio.h \
		kernel/lib/blockdev.c includes/ir0/blockdev.h \
		sched/rr_sched.c sched/switch/switch_arm64.c sched/task.h \
		$(MUSL_AARCH64_HELLO) build/busybox_aarch64 arch/arm64/linker.ld
	@$(MAKE) -s musl-aarch64-hello
	@$(MAKE) -s busybox-aarch64-min
	@echo "  CC      arch/arm64/sources/boot_stub.c (F7j process+TTBR+musl+RR+bb+virtio)"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -c \
		arch/arm64/sources/boot_stub.c -o arch/arm64/sources/boot_stub.o
	@echo "  CC      arch/arm64/sources/mmu_early.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/mmu_early.c -o arch/arm64/sources/mmu_early.o
	@echo "  CC      arch/arm64/sources/exc_early.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/exc_early.c -o arch/arm64/sources/exc_early.o
	@echo "  CC      arch/arm64/sources/pl011.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/pl011.c -o arch/arm64/sources/pl011.o
	@echo "  CC      arch/arm64/sources/board.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/board.c -o arch/arm64/sources/board.o
	@echo "  CC      arch/arm64/sources/platform.c (board power ops)"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/platform.c -o arch/arm64/sources/platform.o
	@echo "  CC      arch/arm64/sources/freestanding_stubs.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/freestanding_stubs.c -o arch/arm64/sources/freestanding_stubs.o
	@echo "  CC      arch/arm64/sources/serial_io_arm64.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/serial_io_arm64.c -o arch/arm64/sources/serial_io_arm64.o
	@echo "  CC      arch/arm64/sources/slice_hello.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/slice_hello.c -o arch/arm64/sources/slice_hello.o
	@echo "  CC      arch/arm64/sources/timer.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/timer.c -o arch/arm64/sources/timer.o
	@echo "  CC      arch/arm64/sources/gic_v2.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/gic_v2.c -o arch/arm64/sources/gic_v2.o
	@echo "  CC      arch/arm64/sources/syscall_early.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/syscall_early.c -o arch/arm64/sources/syscall_early.o
	@echo "  CC      arch/arm64/sources/mm_ops.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/mm_ops.c -o arch/arm64/sources/mm_ops.o
	@echo "  CC      arch/arm64/sources/switch_early.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/switch_early.c -o arch/arm64/sources/switch_early.o
	@echo "  CC      arch/arm64/sources/process_early.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -I$(KERNEL_ROOT)/sched \
		-I$(KERNEL_ROOT)/includes -I$(KERNEL_ROOT)/includes/ir0 \
		-c arch/arm64/sources/process_early.c -o arch/arm64/sources/process_early.o
	@echo "  CC      sched/rr_sched.c"
	@mkdir -p build/arm64-boot
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -DARCH_ARM64=1 \
		-I$(KERNEL_ROOT)/sched -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -I$(KERNEL_ROOT)/arch/common \
		-I$(KERNEL_ROOT) \
		-c sched/rr_sched.c -o build/arm64-boot/rr_sched.o
	@echo "  CC      sched/switch/switch_arm64.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -I$(KERNEL_ROOT)/sched \
		-I$(KERNEL_ROOT)/includes -I$(KERNEL_ROOT)/includes/ir0 \
		-I$(KERNEL_ROOT)/arch/common \
		-c sched/switch/switch_arm64.c -o build/arm64-boot/switch_arm64.o
	@echo "  CC      arch/arm64/sources/rr_early.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -DARCH_ARM64=1 \
		-I$(KERNEL_ROOT)/sched -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -I$(KERNEL_ROOT)/arch/common \
		-I$(KERNEL_ROOT) \
		-c arch/arm64/sources/rr_early.c -o arch/arm64/sources/rr_early.o
	@echo "  CC      arch/arm64/sources/rr_early_stubs.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -DARCH_ARM64=1 \
		-I$(KERNEL_ROOT)/sched -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -I$(KERNEL_ROOT)/arch/common \
		-I$(KERNEL_ROOT) \
		-c arch/arm64/sources/rr_early_stubs.c -o arch/arm64/sources/rr_early_stubs.o
	@echo "  CC      arch/arm64/sources/elf_load_early.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/elf_load_early.c -o arch/arm64/sources/elf_load_early.o
	@echo "  CC      arch/arm64/sources/busybox_load_early.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/busybox_load_early.c -o arch/arm64/sources/busybox_load_early.o
	@echo "  CC      arch/arm64/sources/rootfs_early.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/rootfs_early.c -o arch/arm64/sources/rootfs_early.o
	@echo "  CC      drivers/virtio/virtio_mmio.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -c \
		drivers/virtio/virtio_mmio.c -o drivers/virtio/virtio_mmio.o
	@echo "  CC      kernel/lib/blockdev.c (portable facade)"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -c \
		kernel/lib/blockdev.c -o build/arm64-boot/blockdev.o
	@echo "  CC      arch/arm64/sources/virtio_blk_early.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -c \
		arch/arm64/sources/virtio_blk_early.c -o arch/arm64/sources/virtio_blk_early.o
	@echo "  CC      arch/arm64/sources/virtio_net_early.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -c \
		arch/arm64/sources/virtio_net_early.c -o arch/arm64/sources/virtio_net_early.o
	@echo "  AS      arch/arm64/sources/hello_embed.S"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_ASFLAGS) -c \
		arch/arm64/sources/hello_embed.S -o arch/arm64/sources/hello_embed.o
	@echo "  AS      arch/arm64/sources/busybox_embed.S"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_ASFLAGS) -c \
		arch/arm64/sources/busybox_embed.S -o arch/arm64/sources/busybox_embed.o
	@echo "  AS      arch/arm64/sources/vectors.S"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_ASFLAGS) -c \
		arch/arm64/sources/vectors.S -o arch/arm64/sources/vectors.o
	@echo "  AS      arch/arm64/sources/switch_early.S"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_ASFLAGS) -c \
		arch/arm64/sources/switch_early.S -o arch/arm64/sources/switch_early_asm.o
	@echo "  CC      arch/common/boot_log.c (freestanding)"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/common/boot_log.c -o build/arm64-boot/boot_log.o
	@echo "  LD      $@"
	@aarch64-linux-gnu-ld -T arch/arm64/linker.ld -o $@ \
		arch/arm64/sources/boot_stub.o arch/arm64/sources/mmu_early.o \
		arch/arm64/sources/exc_early.o arch/arm64/sources/pl011.o \
		arch/arm64/sources/board.o arch/arm64/sources/platform.o \
		arch/arm64/sources/freestanding_stubs.o \
		arch/arm64/sources/serial_io_arm64.o arch/arm64/sources/slice_hello.o \
		build/arm64-boot/boot_log.o \
		arch/arm64/sources/timer.o arch/arm64/sources/gic_v2.o \
		arch/arm64/sources/syscall_early.o arch/arm64/sources/mm_ops.o \
		arch/arm64/sources/switch_early.o arch/arm64/sources/switch_early_asm.o \
		arch/arm64/sources/process_early.o build/arm64-boot/switch_arm64.o \
		build/arm64-boot/rr_sched.o arch/arm64/sources/rr_early.o \
		arch/arm64/sources/rr_early_stubs.o \
		arch/arm64/sources/elf_load_early.o arch/arm64/sources/hello_embed.o \
		arch/arm64/sources/busybox_load_early.o arch/arm64/sources/rootfs_early.o \
		arch/arm64/sources/busybox_embed.o \
		drivers/virtio/virtio_mmio.o \
		build/arm64-boot/blockdev.o \
		arch/arm64/sources/virtio_blk_early.o \
		arch/arm64/sources/virtio_net_early.o \
		arch/arm64/sources/vectors.o
	@echo "✓ $@"

# Alias: same image; smoke looks for post-MMU / VBAR / EL0 / slice / port tags.
kernel-arm64-mmu.bin: kernel-arm64-boot.bin
	@cp -f kernel-arm64-boot.bin $@
	@echo "✓ $@"

kernel-arm64-vbar.bin: kernel-arm64-boot.bin
	@cp -f kernel-arm64-boot.bin $@
	@echo "✓ $@"

kernel-arm64-el0.bin: kernel-arm64-boot.bin
	@cp -f kernel-arm64-boot.bin $@
	@echo "✓ $@"

kernel-arm64-slice.bin: kernel-arm64-boot.bin
	@cp -f kernel-arm64-boot.bin $@
	@echo "✓ $@"

kernel-arm64-port.bin: kernel-arm64-boot.bin
	@cp -f kernel-arm64-boot.bin $@
	@echo "✓ $@"

kernel-arm64-gic.bin: kernel-arm64-boot.bin
	@cp -f kernel-arm64-boot.bin $@
	@echo "✓ $@"

kernel-arm64-syscall.bin: kernel-arm64-boot.bin
	@cp -f kernel-arm64-boot.bin $@
	@echo "✓ $@"

# Honest min link: MEMORY_OBJS + KERNEL sample + freestanding ARCH (+ stubs).
# Does NOT claim product ALL_OBJS (x86 ATA/RTL8139/VBE remain excluded).
.PHONY: kernel-arm64-min.bin
kernel-arm64-min.bin: kernel-arm64-boot.bin arch/arm64/sources/min_link_stubs.c \
		arch/arm64/sources/freestanding_stubs.c
	@echo "  CC      ARM64_MIN MEMORY+KERNEL sample"
	@mkdir -p build/arm64-min
	@fail=0; \
	for src in mm/allocator.c mm/paging.c mm/pmm.c mm/kmem.c \
		kernel/errno.c kernel/lib/open_flags.c; do \
		base=$$(basename $$src .c); \
		echo "  CC      $$src (min)"; \
		if ! aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -DARCH_ARM64=1 \
			-I$(KERNEL_ROOT)/includes -I$(KERNEL_ROOT)/includes/ir0 \
			-I$(KERNEL_ROOT)/arch/common \
			-I$(KERNEL_ROOT)/fs -I$(KERNEL_ROOT)/net \
			-I$(KERNEL_ROOT)/sched -I$(KERNEL_ROOT) \
			-c $$src -o build/arm64-min/$$base.o; then \
			echo "✗ compile $$src"; fail=1; break; \
		fi; \
	done; \
	if [ $$fail -ne 0 ]; then exit 1; fi
	@echo "  CC      arch/arm64/sources/min_link_stubs.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/min_link_stubs.c -o build/arm64-min/min_link_stubs.o
	@echo "  LD      $@ (boot + MEMORY sample — not ALL_OBJS)"
	@aarch64-linux-gnu-ld -T arch/arm64/linker.ld -o $@ \
		arch/arm64/sources/boot_stub.o arch/arm64/sources/mmu_early.o \
		arch/arm64/sources/exc_early.o arch/arm64/sources/pl011.o \
		arch/arm64/sources/serial_io_arm64.o arch/arm64/sources/slice_hello.o \
		arch/arm64/sources/timer.o arch/arm64/sources/gic_v2.o \
		arch/arm64/sources/syscall_early.o arch/arm64/sources/mm_ops.o \
		arch/arm64/sources/switch_early.o arch/arm64/sources/switch_early_asm.o \
		arch/arm64/sources/process_early.o build/arm64-boot/switch_arm64.o \
		build/arm64-boot/rr_sched.o arch/arm64/sources/rr_early.o \
		arch/arm64/sources/rr_early_stubs.o \
		arch/arm64/sources/elf_load_early.o arch/arm64/sources/hello_embed.o \
		arch/arm64/sources/busybox_load_early.o arch/arm64/sources/rootfs_early.o \
		arch/arm64/sources/busybox_embed.o \
		drivers/virtio/virtio_mmio.o \
		build/arm64-boot/blockdev.o \
		arch/arm64/sources/virtio_blk_early.o \
		arch/arm64/sources/virtio_net_early.o \
		arch/arm64/sources/vectors.o \
		build/arm64-min/allocator.o build/arm64-min/paging.o \
		build/arm64-min/pmm.o build/arm64-min/kmem.o \
		build/arm64-min/errno.o build/arm64-min/open_flags.o \
		build/arm64-min/min_link_stubs.o \
		--defsym ARM64_MIN_LINK_MARKER=1
	@echo "✓ $@ (MEMORY+KERNEL sample+ARCH freestanding linked; drivers ALL_OBJS still BLOCKED)"

# ALL_OBJS_ARM64: MEMORY + portable KERNEL sample + LIB strings + mark (no x86 drivers).
# FS/NET/logging/futex full link remains probe-only / BLOCKED (pulls snprintf/sched).
.PHONY: kernel-arm64-all.bin
kernel-arm64-all.bin: kernel-arm64-boot.bin arch/arm64/sources/min_link_stubs.c \
		arch/arm64/sources/all_objs_mark.c arch/arm64/sources/portable_string.c
	@echo "  CC      ARM64_ALL portable objs (not x86 drivers)"
	@mkdir -p build/arm64-all
	@rm -f /tmp/ir0-arm64-all-link.log
	@fail=0; \
	for src in mm/allocator.c mm/paging.c mm/pmm.c mm/kmem.c \
		kernel/errno.c \
		kernel/lib/open_flags.c \
		arch/arm64/sources/portable_string.c; do \
		base=$$(basename $$src .c); \
		echo "  CC      $$src (all)"; \
		if ! aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -DARCH_ARM64=1 \
			-I$(KERNEL_ROOT)/includes -I$(KERNEL_ROOT)/includes/ir0 \
			-I$(KERNEL_ROOT)/arch/common \
			-I$(KERNEL_ROOT)/fs -I$(KERNEL_ROOT)/net \
			-I$(KERNEL_ROOT)/sched -I$(KERNEL_ROOT) \
			-c $$src -o build/arm64-all/$$base.o 2>>/tmp/ir0-arm64-all-link.log; then \
			echo "✗ compile $$src"; cat /tmp/ir0-arm64-all-link.log | tail -20; exit 1; \
		fi; \
	done
	@echo "  CC      arch/arm64/sources/min_link_stubs.c (all)"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/min_link_stubs.c -o build/arm64-all/min_link_stubs.o
	@echo "  CC      arch/arm64/sources/all_objs_mark.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/all_objs_mark.c -o build/arm64-all/all_objs_mark.o
	@# Provide libc string aliases used by portable kernel TUs
	@echo "  CC      build/arm64-all/string_aliases.c"
	@printf '%s\n' \
		'#include <stddef.h>' \
		'size_t arm64_portable_strlen(const char *s);' \
		'void *arm64_portable_memcpy(void *d, const void *s, size_t n);' \
		'size_t strlen(const char *s) { return arm64_portable_strlen(s); }' \
		'int strcmp(const char *a, const char *b) {' \
		'  while (*a && *a == *b) { a++; b++; } return (unsigned char)*a - (unsigned char)*b; }' \
		'int strncmp(const char *a, const char *b, size_t n) {' \
		'  while (n && *a && *a == *b) { a++; b++; n--; }' \
		'  if (!n) return 0; return (unsigned char)*a - (unsigned char)*b; }' \
		'char *strncpy(char *d, const char *s, size_t n) {' \
		'  size_t i; for (i = 0; i < n && s[i]; i++) d[i] = s[i];' \
		'  for (; i < n; i++) d[i] = 0; return d; }' \
		'void *memcpy(void *d, const void *s, size_t n) { return arm64_portable_memcpy(d, s, n); }' \
		'void *memset(void *d, int c, size_t n) {' \
		'  unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }' \
		> build/arm64-all/string_aliases.c
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		build/arm64-all/string_aliases.c -o build/arm64-all/string_aliases.o
	@echo "  LD      $@ (portable ALL — no x86 drivers)"
	@aarch64-linux-gnu-ld -T arch/arm64/linker.ld -o $@ \
		arch/arm64/sources/boot_stub.o arch/arm64/sources/mmu_early.o \
		arch/arm64/sources/exc_early.o arch/arm64/sources/pl011.o \
		arch/arm64/sources/serial_io_arm64.o arch/arm64/sources/slice_hello.o \
		arch/arm64/sources/timer.o arch/arm64/sources/gic_v2.o \
		arch/arm64/sources/syscall_early.o arch/arm64/sources/mm_ops.o \
		arch/arm64/sources/switch_early.o arch/arm64/sources/switch_early_asm.o \
		arch/arm64/sources/process_early.o build/arm64-boot/switch_arm64.o \
		build/arm64-boot/rr_sched.o arch/arm64/sources/rr_early.o \
		arch/arm64/sources/rr_early_stubs.o \
		arch/arm64/sources/elf_load_early.o arch/arm64/sources/hello_embed.o \
		arch/arm64/sources/busybox_load_early.o arch/arm64/sources/rootfs_early.o \
		arch/arm64/sources/busybox_embed.o \
		drivers/virtio/virtio_mmio.o \
		build/arm64-boot/blockdev.o \
		arch/arm64/sources/virtio_blk_early.o \
		arch/arm64/sources/virtio_net_early.o \
		arch/arm64/sources/vectors.o \
		build/arm64-all/allocator.o build/arm64-all/paging.o \
		build/arm64-all/pmm.o build/arm64-all/kmem.o \
		build/arm64-all/errno.o \
		build/arm64-all/open_flags.o \
		build/arm64-all/portable_string.o \
		build/arm64-all/string_aliases.o \
		build/arm64-all/min_link_stubs.o build/arm64-all/all_objs_mark.o
	@echo "✓ $@ (ALL_OBJS_ARM64 portable link; ATA/RTL/VBE/ISA still BLOCKED)"

smoke-arm64-boot: kernel-arm64-boot.bin
	@echo "  SMOKE   ARM64 QEMU virt boot tag..."
	@rm -f /tmp/arm64-boot-smoke.log
	@$(SMOKE_QEMU_RUN) --log /tmp/arm64-boot-smoke.log --timeout 20 --stale-sec 8 \
		--done ARM64_BOOT_OK -- \
		qemu-system-aarch64 -M $(ARM64_QEMU_MACHINE) -cpu cortex-a53 -m 128M \
		-kernel kernel-arm64-boot.bin -nographic -serial mon:stdio \
		-display none -no-reboot 2>/dev/null || true
	@if grep -q "ARM64_BOOT_OK" /tmp/arm64-boot-smoke.log && \
	    awk '/\[INFO\] \[BOOT\] IR0 kernel/{found=1; exit} NR>20{exit} END{exit !found}' \
		/tmp/arm64-boot-smoke.log; then \
		echo "✓ smoke-arm64-boot passed (banner-first + ARM64_BOOT_OK)"; \
		grep -E '\[BOOT\]|\[ARCH\]|ARM64_BOOT' /tmp/arm64-boot-smoke.log | head -10; \
	else \
		echo "✗ smoke-arm64-boot FAILED (need BOOT banner then ARM64_BOOT_OK)"; \
		head -30 /tmp/arm64-boot-smoke.log; exit 1; \
	fi

smoke-arm64-mmu: kernel-arm64-mmu.bin
	@echo "  SMOKE   ARM64 QEMU virt early MMU tag..."
	@rm -f /tmp/arm64-mmu-smoke.log
	@$(SMOKE_QEMU_RUN) --log /tmp/arm64-mmu-smoke.log --timeout 20 --stale-sec 8 \
		--done ARM64_MMU_OK -- \
		qemu-system-aarch64 -M $(ARM64_QEMU_MACHINE) -cpu cortex-a53 -m 128M \
		-kernel kernel-arm64-mmu.bin -nographic -serial mon:stdio \
		-display none -no-reboot 2>/dev/null || true
	@if grep -q "ARM64_MMU_OK" /tmp/arm64-mmu-smoke.log; then \
		echo "✓ smoke-arm64-mmu passed"; \
	else \
		echo "✗ smoke-arm64-mmu FAILED"; \
		tail -40 /tmp/arm64-mmu-smoke.log; exit 1; \
	fi

smoke-arm64-vbar: kernel-arm64-vbar.bin
	@echo "  SMOKE   ARM64 QEMU virt VBAR/SVC tag..."
	@rm -f /tmp/arm64-vbar-smoke.log
	@$(SMOKE_QEMU_RUN) --log /tmp/arm64-vbar-smoke.log --timeout 20 --stale-sec 8 \
		--done ARM64_VBAR_OK -- \
		qemu-system-aarch64 -M $(ARM64_QEMU_MACHINE) -cpu cortex-a53 -m 128M \
		-kernel kernel-arm64-vbar.bin -nographic -serial mon:stdio \
		-display none -no-reboot 2>/dev/null || true
	@if grep -q "ARM64_VBAR_OK" /tmp/arm64-vbar-smoke.log && \
	   grep -q "ARM64_SVC_RET_OK" /tmp/arm64-vbar-smoke.log; then \
		echo "✓ smoke-arm64-vbar passed"; \
	else \
		echo "✗ smoke-arm64-vbar FAILED"; \
		tail -40 /tmp/arm64-vbar-smoke.log; exit 1; \
	fi

smoke-arm64-el0: kernel-arm64-el0.bin
	@echo "  SMOKE   ARM64 QEMU virt EL0 drop + SVC..."
	@rm -f /tmp/arm64-el0-smoke.log
	@$(SMOKE_QEMU_RUN) --log /tmp/arm64-el0-smoke.log --timeout 20 --stale-sec 8 \
		--done ARM64_EL0_RET_OK -- \
		qemu-system-aarch64 -M $(ARM64_QEMU_MACHINE) -cpu cortex-a53 -m 128M \
		-kernel kernel-arm64-el0.bin -nographic -serial mon:stdio \
		-display none -no-reboot 2>/dev/null || true
	@if grep -q "ARM64_EL0_DROP" /tmp/arm64-el0-smoke.log && \
	   grep -q "ARM64_EL0_SVC_OK" /tmp/arm64-el0-smoke.log && \
	   grep -q "ARM64_EL0_RET_OK" /tmp/arm64-el0-smoke.log; then \
		echo "✓ smoke-arm64-el0 passed"; \
	else \
		echo "✗ smoke-arm64-el0 FAILED"; \
		tail -50 /tmp/arm64-el0-smoke.log; exit 1; \
	fi

smoke-arm64-slice: kernel-arm64-slice.bin arm64-slice-compile
	@echo "  SMOKE   ARM64 QEMU virt F7b.1 portable slice..."
	@rm -f /tmp/arm64-slice-smoke.log
	@$(SMOKE_QEMU_RUN) --log /tmp/arm64-slice-smoke.log --timeout 20 --stale-sec 8 \
		--done ARM64_SLICE_OK -- \
		qemu-system-aarch64 -M $(ARM64_QEMU_MACHINE) -cpu cortex-a53 -m 128M \
		-kernel kernel-arm64-slice.bin -nographic -serial mon:stdio \
		-display none -no-reboot 2>/dev/null || true
	@if grep -q "ARM64_SLICE_OK" /tmp/arm64-slice-smoke.log; then \
		echo "✓ smoke-arm64-slice passed"; \
	else \
		echo "✗ smoke-arm64-slice FAILED"; \
		tail -50 /tmp/arm64-slice-smoke.log; exit 1; \
	fi

smoke-arm64-port: kernel-arm64-port.bin arm64-portable-compile
	@echo "  SMOKE   ARM64 QEMU virt F7b pack (PL011+paging+timer+GIC IRQ)..."
	@rm -f /tmp/arm64-port-smoke.log
	@$(SMOKE_QEMU_RUN) --log /tmp/arm64-port-smoke.log --timeout 20 --stale-sec 8 \
		--done ARM64_TIMER_IRQ_OK -- \
		qemu-system-aarch64 -M $(ARM64_QEMU_MACHINE) -cpu cortex-a53 -m 128M \
		-kernel kernel-arm64-port.bin -nographic -serial mon:stdio \
		-display none -no-reboot 2>/dev/null || true
	@if grep -q "ARM64_PL011_OK" /tmp/arm64-port-smoke.log && \
	   grep -q "ARM64_PAGING_OK" /tmp/arm64-port-smoke.log && \
	   grep -q "ARM64_GIC_MAP_OK" /tmp/arm64-port-smoke.log && \
	   grep -q "ARM64_TIMER_OK" /tmp/arm64-port-smoke.log && \
	   grep -q "ARM64_GIC_OK" /tmp/arm64-port-smoke.log && \
	   grep -q "ARM64_TIMER_IRQ_OK" /tmp/arm64-port-smoke.log; then \
		echo "✓ smoke-arm64-port passed"; \
	else \
		echo "✗ smoke-arm64-port FAILED"; \
		tail -60 /tmp/arm64-port-smoke.log; exit 1; \
	fi

smoke-arm64-gic: kernel-arm64-gic.bin
	@echo "  SMOKE   ARM64 QEMU virt GICv2 + timer IRQ..."
	@rm -f /tmp/arm64-gic-smoke.log
	@$(SMOKE_QEMU_RUN) --log /tmp/arm64-gic-smoke.log --timeout 20 --stale-sec 8 \
		--done ARM64_TIMER_IRQ_OK -- \
		qemu-system-aarch64 -M $(ARM64_QEMU_MACHINE) -cpu cortex-a53 -m 128M \
		-kernel kernel-arm64-gic.bin -nographic -serial mon:stdio \
		-display none -no-reboot 2>/dev/null || true
	@if grep -q "ARM64_GIC_OK" /tmp/arm64-gic-smoke.log && \
	   grep -q "ARM64_TIMER_IRQ_OK" /tmp/arm64-gic-smoke.log; then \
		echo "✓ smoke-arm64-gic passed"; \
	else \
		echo "✗ smoke-arm64-gic FAILED"; \
		tail -60 /tmp/arm64-gic-smoke.log; exit 1; \
	fi

smoke-arm64-syscall: kernel-arm64-syscall.bin
	@echo "  SMOKE   ARM64 QEMU virt F7d EL0 (getpid+nanosleep+write)..."
	@rm -f /tmp/arm64-syscall-smoke.log
	@$(SMOKE_QEMU_RUN) --log /tmp/arm64-syscall-smoke.log --timeout 20 --stale-sec 8 \
		--done ARM64_SYSCALL_OK -- \
		qemu-system-aarch64 -M $(ARM64_QEMU_MACHINE) -cpu cortex-a53 -m 128M \
		-kernel kernel-arm64-syscall.bin -nographic -serial mon:stdio \
		-display none -no-reboot 2>/dev/null || true
	@if grep -q "ARM64_EL0_PAGE_OK" /tmp/arm64-syscall-smoke.log && \
	   grep -q "ARM64_SWITCH_B" /tmp/arm64-syscall-smoke.log && \
	   grep -q "ARM64_SWITCH_OK" /tmp/arm64-syscall-smoke.log && \
	   grep -q "ARM64_TTBR_B_OK" /tmp/arm64-syscall-smoke.log && \
	   grep -q "ARM64_TTBR_SWITCH_OK" /tmp/arm64-syscall-smoke.log && \
	   grep -q "ARM64_PROCESS_SWITCH_OK" /tmp/arm64-syscall-smoke.log && \
	   grep -q "ARM64_PROCESS_TTBR_OK" /tmp/arm64-syscall-smoke.log && \
	   grep -q "ARM64_FORK_OK" /tmp/arm64-syscall-smoke.log && \
	   grep -q "ARM64_EXEC_OK" /tmp/arm64-syscall-smoke.log && \
	   grep -q "ARM64_PROCESS_T_SWITCH_OK" /tmp/arm64-syscall-smoke.log && \
	   grep -q "ARM64_RR_SCHED_OK" /tmp/arm64-syscall-smoke.log && \
	   grep -q "ARM64_RR_TICK_OK" /tmp/arm64-syscall-smoke.log && \
	   grep -q "ARM64_MUSL_LOAD_OK" /tmp/arm64-syscall-smoke.log && \
	   grep -q "ARM64_MUSL_HELLO_OK" /tmp/arm64-syscall-smoke.log && \
	   grep -q "ARM64_BUSYBOX_LOAD_OK" /tmp/arm64-syscall-smoke.log && \
	   grep -q "ARM64_BUSYBOX_EL0_OK" /tmp/arm64-syscall-smoke.log && \
	   grep -q "ARM64_ROOTFS_OK" /tmp/arm64-syscall-smoke.log && \
	   grep -q "ARM64_BUSYBOX_INIT_OK" /tmp/arm64-syscall-smoke.log && \
	   grep -q "ARM64_NANOSLEEP_OK" /tmp/arm64-syscall-smoke.log && \
	   grep -q "ARM64_CLOCK_GETTIME_OK" /tmp/arm64-syscall-smoke.log && \
	   grep -q "ARM64_GETTIMEOFDAY_OK" /tmp/arm64-syscall-smoke.log && \
	   grep -q "ARM64_CLOCK_NANOSLEEP_OK" /tmp/arm64-syscall-smoke.log && \
	   grep -q "ARM64_SYSCALL_OK" /tmp/arm64-syscall-smoke.log && \
	   grep -q "ARM64_WRITE_OK" /tmp/arm64-syscall-smoke.log; then \
		echo "✓ smoke-arm64-syscall passed (incl. F7h/F7i/F7j + fork/exec + musl + process_t)"; \
	else \
		echo "✗ smoke-arm64-syscall FAILED"; \
		tail -60 /tmp/arm64-syscall-smoke.log; exit 1; \
	fi

.PHONY: smoke-arm64-virtio-blk smoke-arm64-virtio-net
ARM64_VIRTIO_DISK ?= /tmp/ir0-arm64-virtio-blk.img
smoke-arm64-virtio-blk: kernel-arm64-syscall.bin
	@echo "  SMOKE   ARM64 virtio-mmio + virtio-blk..."
	@dd if=/dev/zero of=$(ARM64_VIRTIO_DISK) bs=1M count=4 status=none
	@rm -f /tmp/arm64-virtio-blk-smoke.log
	@$(SMOKE_QEMU_RUN) --log /tmp/arm64-virtio-blk-smoke.log --timeout 25 --stale-sec 10 \
		--done ARM64_BLOCKDEV_FACADE_OK -- \
		qemu-system-aarch64 -M $(ARM64_QEMU_MACHINE) -cpu cortex-a53 -m 128M \
		-kernel kernel-arm64-syscall.bin -nographic -serial mon:stdio \
		-global virtio-mmio.force-legacy=false \
		-drive if=none,file=$(ARM64_VIRTIO_DISK),format=raw,id=hd0 \
		-device virtio-blk-device,drive=hd0 \
		-display none -no-reboot 2>/dev/null || true
	@if grep -q "ARM64_VIRTIO_MMIO_OK" /tmp/arm64-virtio-blk-smoke.log && \
	   grep -q "ARM64_VIRTIO_BLK_OK" /tmp/arm64-virtio-blk-smoke.log && \
	   grep -q "\\[BLOCKDEV\\]\\[CLASSIFY\\] BLOCKDEV_FACADE_OK" /tmp/arm64-virtio-blk-smoke.log && \
	   grep -q "ARM64_BLOCKDEV_FACADE_OK" /tmp/arm64-virtio-blk-smoke.log; then \
		echo "✓ smoke-arm64-virtio-blk passed (virtio + portable ir0_block facade)"; \
	else \
		echo "✗ smoke-arm64-virtio-blk FAILED"; \
		tail -80 /tmp/arm64-virtio-blk-smoke.log; exit 1; \
	fi

smoke-arm64-virtio-net: kernel-arm64-syscall.bin
	@echo "  SMOKE   ARM64 virtio-mmio + virtio-net..."
	@rm -f /tmp/arm64-virtio-net-smoke.log
	@$(SMOKE_QEMU_RUN) --log /tmp/arm64-virtio-net-smoke.log --timeout 25 --stale-sec 10 \
		--done ARM64_VIRTIO_NET_OK -- \
		qemu-system-aarch64 -M $(ARM64_QEMU_MACHINE) -cpu cortex-a53 -m 128M \
		-kernel kernel-arm64-syscall.bin -nographic -serial mon:stdio \
		-global virtio-mmio.force-legacy=false \
		-netdev user,id=n0 -device virtio-net-device,netdev=n0 \
		-display none -no-reboot 2>/dev/null || true
	@if grep -q "ARM64_VIRTIO_MMIO_OK" /tmp/arm64-virtio-net-smoke.log && \
	   grep -q "ARM64_VIRTIO_NET_OK" /tmp/arm64-virtio-net-smoke.log; then \
		echo "✓ smoke-arm64-virtio-net passed"; \
	else \
		echo "✗ smoke-arm64-virtio-net FAILED"; \
		tail -80 /tmp/arm64-virtio-net-smoke.log; exit 1; \
	fi

.PHONY: smoke-arm64-nanosleep smoke-arm64-switch
smoke-arm64-nanosleep: smoke-arm64-syscall
	@echo "✓ smoke-arm64-nanosleep (alias of smoke-arm64-syscall + NANOSLEEP tag)"

smoke-arm64-switch: smoke-arm64-syscall
	@echo "✓ smoke-arm64-switch (alias — F7h/F7i tags in smoke-arm64-syscall)"

.PHONY: smoke-arm64-ttbr
smoke-arm64-ttbr: smoke-arm64-syscall
	@echo "✓ smoke-arm64-ttbr (alias — F7i TTBR tags in smoke-arm64-syscall)"

.PHONY: smoke-arm64
smoke-arm64: smoke-arm64-boot smoke-arm64-mmu smoke-arm64-slice smoke-arm64-port \
	smoke-arm64-gic smoke-arm64-syscall smoke-arm64-vbar smoke-arm64-el0
	@echo "✓ smoke-arm64 (boot+mmu+slice+port+gic+syscall+vbar+el0) passed"

.PHONY: smoke-stream-sock build-init-stream-sock-smoke
.PHONY: smoke-hostshare-9p build-init-hostshare-9p-smoke
.PHONY: smoke-hostshare-tree build-init-hostshare-tree-smoke
.PHONY: smoke-hostshare-exec build-init-hostshare-exec
# (arm64 full kernel-arm64.bin remains available via ARCH=arm64)
build-init-stream-sock-smoke:
	@if [ -z "$(MUSL_CC)" ]; then echo "✗ musl cc missing"; exit 1; fi
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) setup/pid1/init_stream_sock_smoke.c
	@echo "✓ build-init-stream-sock-smoke OK"

build-init-hostshare-9p-smoke:
	@if [ -z "$(MUSL_CC)" ]; then echo "✗ musl cc missing"; exit 1; fi
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) setup/pid1/init_hostshare_9p_smoke.c
	@echo "✓ build-init-hostshare-9p-smoke OK"

build-init-hostshare-tree-smoke:
	@if [ -z "$(MUSL_CC)" ]; then echo "✗ musl cc missing"; exit 1; fi
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) setup/pid1/init_hostshare_tree_smoke.c
	@echo "✓ build-init-hostshare-tree-smoke OK"

HOSTSHARE_TREE_SMOKE_LOG = /tmp/hostshare-tree-smoke.log
smoke-hostshare-tree: kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@echo "  SMOKE   virtio-9p tree (mkdir/readdir/write-at/rename/symlink/unlink)..."
	@$(MAKE) -s build-init-hostshare-tree-smoke
	@SHARE=$$(mktemp -d /tmp/ir0-hostshare-tree.XXXXXX); \
	DISK=$$(mktemp /tmp/ir0-hostshare-tree.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	rm -f $(HOSTSHARE_TREE_SMOKE_LOG); \
	$(SMOKE_QEMU_RUN) --log $(HOSTSHARE_TREE_SMOKE_LOG) --timeout 90 \
		--done 'HOSTSHARE_TREE_OK' -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-fsdev local,id=ir0fs,path=$$SHARE,security_model=none \
		-device virtio-9p-pci,fsdev=ir0fs,mount_tag=ir0share,disable-modern=on \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK; \
	if grep -q "HOSTSHARE_TREE_MKDIR_OK" $(HOSTSHARE_TREE_SMOKE_LOG) && \
	   grep -q "HOSTSHARE_TREE_WRITE_OK" $(HOSTSHARE_TREE_SMOKE_LOG) && \
	   grep -q "HOSTSHARE_TREE_READ_OK" $(HOSTSHARE_TREE_SMOKE_LOG) && \
	   grep -q "HOSTSHARE_TREE_READDIR_OK" $(HOSTSHARE_TREE_SMOKE_LOG) && \
	   grep -q "HOSTSHARE_TREE_RENAME_OK" $(HOSTSHARE_TREE_SMOKE_LOG) && \
	   grep -q "HOSTSHARE_SYMLINK_OK" $(HOSTSHARE_TREE_SMOKE_LOG) && \
	   grep -q "HOSTSHARE_TREE_UNLINK_OK" $(HOSTSHARE_TREE_SMOKE_LOG) && \
	   grep -q "HOSTSHARE_TREE_OK" $(HOSTSHARE_TREE_SMOKE_LOG) && \
	   test -f $$SHARE/tree_done.txt && \
	   grep -q "HOSTSHARE_TREE_OK" $$SHARE/tree_done.txt && \
	   test -L $$SHARE/symlink_probe && \
	   ! test -d $$SHARE/a; then \
		echo "✓ smoke-hostshare-tree passed (guest tree ops + symlink + host marker)"; \
		rm -rf $$SHARE; \
	else \
		echo "✗ smoke-hostshare-tree FAILED"; \
		echo "--- share ---"; ls -laR $$SHARE 2>/dev/null; \
		grep -E 'HOSTSHARE_TREE_|panic|MOUNT' $(HOSTSHARE_TREE_SMOKE_LOG) | tail -60; \
		rm -rf $$SHARE; \
		exit 1; \
	fi

HOSTSHARE_EXEC_STUB = setup/pid1/init_hostshare_exec
build-init-hostshare-exec:
	@if [ -z "$(MUSL_CC)" ]; then echo "✗ musl cc missing"; exit 1; fi
	@echo "  INIT    Building hostshare exec stub ($(HOSTSHARE_EXEC_STUB))"
	@$(MUSL_CC) -static -Os -o $(HOSTSHARE_EXEC_STUB) setup/pid1/init_hostshare_exec.c
	@file $(HOSTSHARE_EXEC_STUB) | grep -q ELF
	@echo "✓ build-init-hostshare-exec OK"

HOSTSHARE_EXEC_SMOKE_LOG = /tmp/hostshare-exec-smoke.log
# Legacy stub PID1 path (kept for lab).
smoke-hostshare-exec-stub: build-init-hostshare-exec build-ktm-fork-wait-case kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@echo "  SMOKE   hostshare exec (stub PID1 + /mnt/host/ir0_payload)..."
	@python3 scripts/ktm_userdev_runner.py \
		--stub $(HOSTSHARE_EXEC_STUB) \
		--init $(KTM_FORK_WAIT_BIN) \
		--log $(HOSTSHARE_EXEC_SMOKE_LOG) --timeout 120 \
		--done KTM_USERDEV_OK \
		--require HOSTSHARE_EXEC_MOUNT_OK \
		--require 'TEST_END|fork_wait_signal|PASS' \
		--require KTM_USERDEV_OK
	@echo "✓ smoke-hostshare-exec-stub passed"

# Product-shaped pilot: runit PID1 + kill(SIGTERM) in fork_wait case.
smoke-hostshare-exec: build-ktm-fork-wait-case build-runit kernel-x64-userspace.iso
	@echo "  SMOKE   hostshare exec under runit PID1 (fork_wait + kill)..."
	@DISK=$$(mktemp /tmp/ir0-hostshare-exec-runit.XXXXXX.img); \
	chmod +x scripts/ktm_prepare_runit_hostshare_disk.sh; \
	scripts/ktm_prepare_runit_hostshare_disk.sh --quiet-console $$DISK >/dev/null; \
	RC=0; \
	python3 scripts/ktm_userdev_runner.py \
		--disk $$DISK \
		--init $(KTM_FORK_WAIT_BIN) \
		--log $(HOSTSHARE_EXEC_SMOKE_LOG) --timeout 180 \
		--done KTM_USERDEV_OK \
		--require RUNIT_STAGE2_OK \
		--require RUNSV_HOSTSHARE_START \
		--require HOSTSHARE_EXEC_MOUNT_OK \
		--require 'TEST_END|fork_wait_signal|PASS' \
		--require KTM_USERDEV_OK || RC=$$?; \
	rm -f $$DISK; \
	if [ $$RC -eq 0 ]; then echo "✓ smoke-hostshare-exec passed (runit PID1)"; else echo "✗ smoke-hostshare-exec FAILED"; exit $$RC; fi

HOSTSHARE_9P_SMOKE_LOG = /tmp/hostshare-9p-smoke.log
smoke-hostshare-9p: kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@echo "  SMOKE   virtio-9p host share (QEMU -virtfs → /mnt/host)..."
	@$(MAKE) -s build-init-hostshare-9p-smoke
	@SHARE=$$(mktemp -d /tmp/ir0-hostshare.XXXXXX); \
	mkdir -p $$SHARE/subdir; \
	DISK=$$(mktemp /tmp/ir0-hostshare.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	rm -f $(HOSTSHARE_9P_SMOKE_LOG); \
	$(SMOKE_QEMU_RUN) --log $(HOSTSHARE_9P_SMOKE_LOG) --timeout 60 \
		--done 'HOSTSHARE_9P_OK' --done 'KTM_HOSTSHARE_OK' -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-fsdev local,id=ir0fs,path=$$SHARE,security_model=none \
		-device virtio-9p-pci,fsdev=ir0fs,mount_tag=ir0share,disable-modern=on \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK; \
	if grep -q "HOSTSHARE_9P_READY" $(HOSTSHARE_9P_SMOKE_LOG) && \
	   grep -q "HOSTSHARE_9P_SUBDIR_OK" $(HOSTSHARE_9P_SMOKE_LOG) && \
	   grep -q "HOSTSHARE_9P_OK" $(HOSTSHARE_9P_SMOKE_LOG) && \
	   grep -q "KTM_HOSTSHARE_OK" $(HOSTSHARE_9P_SMOKE_LOG) && \
	   test -f $$SHARE/ktm_hostshare.txt && \
	   grep -q "KTM_HOSTSHARE_OK" $$SHARE/ktm_hostshare.txt && \
	   test -f $$SHARE/subdir/ktm_hostshare.txt && \
	   grep -q "HOSTSHARE_9P_SUBDIR_OK" $$SHARE/subdir/ktm_hostshare.txt; then \
		echo "✓ smoke-hostshare-9p passed (flat + subdir write visible on host)"; \
		rm -rf $$SHARE; \
	else \
		echo "✗ smoke-hostshare-9p FAILED"; \
		echo "--- share ---"; ls -laR $$SHARE 2>/dev/null; \
		grep -E 'HOSTSHARE_|KTM_|panic|MOUNT' $(HOSTSHARE_9P_SMOKE_LOG) | tail -50; \
		rm -rf $$SHARE; \
		exit 1; \
	fi

STREAM_SOCK_SMOKE_LOG = /tmp/stream-sock-smoke.log
# Legacy serial inject kept as build-init-stream-sock-smoke only;
# smoke-stream-sock aliases to ktm-userdev-stream-sock-run (see KTM targets).

.PHONY: smoke-ahci-multi
AHCI_MULTI_SMOKE_LOG = /tmp/ahci-multi-smoke.log
smoke-ahci-multi: kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@echo "  SMOKE   AHCI dual-port register..."
	@DISK0=$$(mktemp /tmp/ir0-ahci-m0.XXXXXX.img); \
	DISK1=$$(mktemp /tmp/ir0-ahci-m1.XXXXXX.img); \
	cp -f disk.img $$DISK0; \
	cp -f disk.img $$DISK1; \
	rm -f $(AHCI_MULTI_SMOKE_LOG); \
	$(SMOKE_QEMU_RUN) --log $(AHCI_MULTI_SMOKE_LOG) --timeout 60 --stale-sec 20 \
		--done AHCI_MULTI_OK -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK0,format=raw,if=none,id=disk0 \
		-drive file=$$DISK1,format=raw,if=none,id=disk1 \
		-device ahci,id=ahci0 \
		-device ide-hd,drive=disk0,bus=ahci0.0 \
		-device ide-hd,drive=disk1,bus=ahci0.1 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK0 $$DISK1; \
	if grep -q "AHCI_MULTI_OK" $(AHCI_MULTI_SMOKE_LOG); then \
		echo "✓ smoke-ahci-multi passed"; \
	else \
		echo "✗ smoke-ahci-multi FAILED"; \
		grep -E 'AHCI_|panic' $(AHCI_MULTI_SMOKE_LOG) | tail -40; exit 1; \
	fi

.PHONY: smoke-isa-debug-exit build-init-isa-debug-exit-smoke
ISA_DEBUG_EXIT_LOG = /tmp/isa-debug-exit-smoke.log
build-init-isa-debug-exit-smoke:
	@if [ -z "$(MUSL_CC)" ]; then echo "✗ musl cc missing"; exit 1; fi
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) setup/pid1/init_isa_debug_exit_smoke.c
	@echo "✓ build-init-isa-debug-exit-smoke OK"

smoke-isa-debug-exit: kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@echo "  SMOKE   isa-debug-exit on halt..."
	@$(MAKE) -s build-init-isa-debug-exit-smoke
	@DISK=$$(mktemp /tmp/ir0-isa-exit.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	rm -f $(ISA_DEBUG_EXIT_LOG); \
	$(SMOKE_QEMU_RUN) --log $(ISA_DEBUG_EXIT_LOG) --timeout 45 --stale-sec 15 \
		--done ISA_DEBUG_EXIT_OK -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-device isa-debug-exit,iobase=0xf4,iosize=0x04 \
		-serial stdio -display none -m 128M -no-reboot -net none; \
	rm -f $$DISK; \
	if grep -q "ISA_DEBUG_EXIT_OK" $(ISA_DEBUG_EXIT_LOG); then \
		echo "✓ smoke-isa-debug-exit passed"; \
	else \
		echo "✗ smoke-isa-debug-exit FAILED"; \
		grep -E 'ISA_|SHUTDOWN|HALT|REBOOT' $(ISA_DEBUG_EXIT_LOG) | tail -30; exit 1; \
	fi

.PHONY: smoke-reboot-kexec-enosys build-init-reboot-kexec-enosys
KEXEC_ENOSYS_LOG = /tmp/reboot-kexec-enosys-smoke.log
build-init-reboot-kexec-enosys:
	@if [ -z "$(MUSL_CC)" ]; then echo "✗ musl cc missing"; exit 1; fi
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) setup/pid1/init_reboot_kexec_enosys_smoke.c
	@echo "✓ build-init-reboot-kexec-enosys OK"

smoke-reboot-kexec-enosys: kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@echo "  SMOKE   reboot(KEXEC) → stub reboot..."
	@$(MAKE) -s build-init-reboot-kexec-enosys
	@DISK=$$(mktemp /tmp/ir0-kexec-enosys.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	rm -f $(KEXEC_ENOSYS_LOG); \
	$(SMOKE_QEMU_RUN) --log $(KEXEC_ENOSYS_LOG) --timeout 45 --stale-sec 15 \
		--done REBOOT_KEXEC_STUB -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 128M -no-reboot -net none; \
	rm -f $$DISK; \
	if grep -q "REBOOT_KEXEC_STUB" $(KEXEC_ENOSYS_LOG) && \
	    grep -q "KEXEC_SMOKE_CALL" $(KEXEC_ENOSYS_LOG) && \
	    ! grep -q "KEXEC_SMOKE_FAIL" $(KEXEC_ENOSYS_LOG); then \
		echo "✓ smoke-reboot-kexec-enosys passed"; \
	else \
		echo "✗ smoke-reboot-kexec-enosys FAILED"; \
		grep -E 'KEXEC_|REBOOT_|SUSPEND_|panic' $(KEXEC_ENOSYS_LOG) | tail -30; exit 1; \
	fi

.PHONY: smoke-kexec-load build-init-kexec-load
KEXEC_LOAD_LOG = /tmp/kexec-load-smoke.log
build-init-kexec-load:
	@if [ -z "$(MUSL_CC)" ]; then echo "✗ musl cc missing"; exit 1; fi
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) setup/pid1/init_kexec_load_smoke.c
	@echo "✓ build-init-kexec-load OK"

smoke-kexec-load: kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@echo "  SMOKE   kexec_load + reboot(KEXEC) loaded..."
	@$(MAKE) -s build-init-kexec-load
	@DISK=$$(mktemp /tmp/ir0-kexec-load.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	rm -f $(KEXEC_LOAD_LOG); \
	$(SMOKE_QEMU_RUN) --log $(KEXEC_LOAD_LOG) --timeout 45 --stale-sec 15 \
		--done KEXEC_PAYLOAD_OK -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 128M -no-reboot -net none; \
	rm -f $$DISK; \
	if grep -q "KEXEC_LOAD_OK" $(KEXEC_LOAD_LOG) && \
	    grep -q "REBOOT_KEXEC_LOADED" $(KEXEC_LOAD_LOG) && \
	    grep -q "KEXEC_PAYLOAD_OK" $(KEXEC_LOAD_LOG); then \
		echo "✓ smoke-kexec-load passed"; \
	else \
		echo "✗ smoke-kexec-load FAILED"; \
		grep -E 'KEXEC_|REBOOT_|panic' $(KEXEC_LOAD_LOG) | tail -40; exit 1; \
	fi

.PHONY: smoke-reboot-suspend-stub build-init-reboot-suspend-stub
SUSPEND_STUB_LOG = /tmp/reboot-suspend-stub-smoke.log
build-init-reboot-suspend-stub:
	@$(MAKE) -s build-init-reboot-s3

smoke-reboot-suspend-stub: smoke-reboot-s3
	@echo "✓ smoke-reboot-suspend-stub passed (alias of smoke-reboot-s3)"

.PHONY: smoke-reboot-s3 build-init-reboot-s3
S3_SMOKE_LOG = /tmp/reboot-s3-smoke.log
build-init-reboot-s3:
	@if [ -z "$(MUSL_CC)" ]; then echo "✗ musl cc missing"; exit 1; fi
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) setup/pid1/init_reboot_s3_smoke.c
	@echo "✓ build-init-reboot-s3 OK"

smoke-reboot-s3: kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@echo "  SMOKE   reboot(SW_SUSPEND) S3 enter/resume..."
	@$(MAKE) -s build-init-reboot-s3
	@DISK=$$(mktemp /tmp/ir0-s3.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	rm -f $(S3_SMOKE_LOG); \
	$(SMOKE_QEMU_RUN) --log $(S3_SMOKE_LOG) --timeout 45 --stale-sec 15 \
		--done SUSPEND_S3_OK -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 128M -no-reboot -net none; \
	rm -f $$DISK; \
	if grep -q "SYSTEM_S3_ENTER" $(S3_SMOKE_LOG) && \
	    grep -q "SYSTEM_S3_RESUME_OK" $(S3_SMOKE_LOG) && \
	    grep -q "SUSPEND_S3_OK" $(S3_SMOKE_LOG); then \
		echo "✓ smoke-reboot-s3 passed"; \
	else \
		echo "✗ smoke-reboot-s3 FAILED"; \
		grep -E 'S3_|SUSPEND_|ACPI_|panic' $(S3_SMOKE_LOG) | tail -40; exit 1; \
	fi

.PHONY: smoke-sched-prio
SCHED_PRIO_LOG = /tmp/sched-prio-smoke.log
smoke-sched-prio: kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@echo "  SMOKE   priority-band scheduler selftest (POLICY=2)..."
	@DISK=$$(mktemp /tmp/ir0-sched-prio.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	rm -f $(SCHED_PRIO_LOG); \
	$(SMOKE_QEMU_RUN) --log $(SCHED_PRIO_LOG) --timeout 90 --stale-sec 60 \
		--done SCHED_PRIO_OK -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 128M -no-reboot -net none; \
	rm -f $$DISK; \
	if grep -q "SCHED_PRIO_OK" $(SCHED_PRIO_LOG) && \
	    grep -q "SCHED_POLICY=priority" $(SCHED_PRIO_LOG) && \
	    ! grep -q "SCHED_PRIO_FAIL" $(SCHED_PRIO_LOG); then \
		echo "✓ smoke-sched-prio passed"; \
	else \
		echo "✗ smoke-sched-prio FAILED"; \
		grep -E 'SCHED_|panic' $(SCHED_PRIO_LOG) | tail -40; exit 1; \
	fi

.PHONY: smoke-posix-setsid build-init-posix-setsid
POSIX_SETSID_LOG = /tmp/posix-setsid-smoke.log
build-init-posix-setsid:
	@if [ -z "$(MUSL_CC)" ]; then echo "✗ musl cc missing"; exit 1; fi
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) setup/pid1/init_posix_setsid_smoke.c
	@echo "✓ build-init-posix-setsid OK"

smoke-posix-setsid: kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@echo "  SMOKE   setsid + setpgid..."
	@$(MAKE) -s build-init-posix-setsid
	@DISK=$$(mktemp /tmp/ir0-posix-setsid.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	rm -f $(POSIX_SETSID_LOG); \
	$(SMOKE_QEMU_RUN) --log $(POSIX_SETSID_LOG) --timeout 120 --stale-sec 90 \
		--done POSIX_SETSID_OK -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 128M -no-reboot -net none; \
	rm -f $$DISK; \
	if grep -q "SETSID_OK" $(POSIX_SETSID_LOG) && \
	    grep -q "SETPGID_OK" $(POSIX_SETSID_LOG) && \
	    grep -q "POSIX_SETSID_OK" $(POSIX_SETSID_LOG); then \
		echo "✓ smoke-posix-setsid passed"; \
	else \
		echo "✗ smoke-posix-setsid FAILED"; \
		grep -E 'SETSID|SETPGID|POSIX_|panic' $(POSIX_SETSID_LOG) | tail -30; exit 1; \
	fi

.PHONY: smoke-posix-sighup-tty build-init-posix-sighup-tty
POSIX_SIGHUP_LOG = /tmp/posix-sighup-tty-smoke.log
build-init-posix-sighup-tty:
	@if [ -z "$(MUSL_CC)" ]; then echo "✗ musl cc missing"; exit 1; fi
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) setup/pid1/init_posix_sighup_tty_smoke.c
	@echo "✓ build-init-posix-sighup-tty OK"

smoke-posix-sighup-tty: kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@echo "  SMOKE   PTY TIOCSCTTY + SIGHUP..."
	@$(MAKE) -s build-init-posix-sighup-tty
	@DISK=$$(mktemp /tmp/ir0-posix-sighup.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	rm -f $(POSIX_SIGHUP_LOG); \
	$(SMOKE_QEMU_RUN) --log $(POSIX_SIGHUP_LOG) --timeout 120 --stale-sec 60 \
		--done POSIX_SIGHUP_TTY_OK -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK; \
	if grep -q "PTY_SIGHUP_PGRP" $(POSIX_SIGHUP_LOG) && \
	    grep -q "SIGHUP_OK" $(POSIX_SIGHUP_LOG) && \
	    grep -q "POSIX_SIGHUP_TTY_OK" $(POSIX_SIGHUP_LOG); then \
		echo "✓ smoke-posix-sighup-tty passed"; \
	else \
		echo "✗ smoke-posix-sighup-tty FAILED"; \
		grep -E 'SIGHUP|PTY_|POSIX_|panic' $(POSIX_SIGHUP_LOG) | tail -40; exit 1; \
	fi
.PHONY: smoke-ahci-detect smoke-ahci-read
AHCI_SMOKE_LOG = /tmp/ahci-detect-smoke.log
smoke-ahci-detect: kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@echo "  SMOKE   AHCI PCI detect..."
	@DISK=$$(mktemp /tmp/ir0-ahci-smoke.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	rm -f $(AHCI_SMOKE_LOG); \
	$(SMOKE_QEMU_RUN) --log $(AHCI_SMOKE_LOG) --timeout 45 --stale-sec 15 \
		--done AHCI_DETECT_OK -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=none,id=disk0 \
		-device ahci,id=ahci0 \
		-device ide-hd,drive=disk0,bus=ahci0.0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK; \
	if grep -q "AHCI_DETECT_OK" $(AHCI_SMOKE_LOG); then \
		echo "✓ smoke-ahci-detect passed"; \
	else \
		echo "✗ smoke-ahci-detect FAILED"; \
		grep -E 'AHCI_|panic' $(AHCI_SMOKE_LOG) | tail -20; \
		exit 1; \
	fi

AHCI_READ_SMOKE_LOG = /tmp/ahci-read-smoke.log
smoke-ahci-read: kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@echo "  SMOKE   AHCI block read (LBA0)..."
	@DISK=$$(mktemp /tmp/ir0-ahci-read.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	rm -f $(AHCI_READ_SMOKE_LOG); \
	$(SMOKE_QEMU_RUN) --log $(AHCI_READ_SMOKE_LOG) --timeout 60 --stale-sec 20 \
		--done AHCI_READ_OK -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=none,id=disk0 \
		-device ahci,id=ahci0 \
		-device ide-hd,drive=disk0,bus=ahci0.0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK; \
	if grep -q "AHCI_READ_OK" $(AHCI_READ_SMOKE_LOG) && \
	    (grep -q "AHCI_NCQ_OK" $(AHCI_READ_SMOKE_LOG) || \
	     grep -q "AHCI_NCQ_UNSUPPORTED" $(AHCI_READ_SMOKE_LOG)); then \
		echo "✓ smoke-ahci-read passed"; \
	else \
		echo "✗ smoke-ahci-read FAILED"; \
		grep -E 'AHCI_|panic' $(AHCI_READ_SMOKE_LOG) | tail -30; \
		exit 1; \
	fi

.PHONY: smoke-nvme-read
NVME_READ_SMOKE_LOG = /tmp/nvme-read-smoke.log
smoke-nvme-read: kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@echo "  SMOKE   NVMe block read (LBA0)..."
	@DISK=$$(mktemp /tmp/ir0-nvme-read.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	rm -f $(NVME_READ_SMOKE_LOG); \
	$(SMOKE_QEMU_RUN) --log $(NVME_READ_SMOKE_LOG) --timeout 60 --stale-sec 20 \
		--done NVME_READ_OK -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=none,id=nvme0 \
		-device nvme,serial=nvme0,drive=nvme0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK; \
	if grep -q "NVME_DETECT_OK" $(NVME_READ_SMOKE_LOG) && \
	    grep -q "NVME_READ_OK" $(NVME_READ_SMOKE_LOG); then \
		echo "✓ smoke-nvme-read passed"; \
	else \
		echo "✗ smoke-nvme-read FAILED"; \
		grep -E 'NVME_|panic' $(NVME_READ_SMOKE_LOG) | tail -40; \
		exit 1; \
	fi

# D1.18 — read-only FAT16 on secondary ATA disk (hdb), no MINIX root mutation.
smoke-fat16-mount: kernel-x64-userspace.iso build/fat16_smoke.img
	@if [ ! -f disk.img ]; then \
		echo "  DISK    Creating disk.img..."; \
		$(MAKE) -s disk.img; \
	fi
	@echo "  SMOKE   FAT16 mount + read HELLO.TXT on /dev/hdb..."
	@$(MAKE) -s build-init-fat16-smoke
	@DISK=$$(mktemp /tmp/ir0-fat16-smoke.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	rm -f $(FAT16_SMOKE_LOG); \
	$(SMOKE_QEMU_RUN) --log $(FAT16_SMOKE_LOG) --timeout 90 \
		--done 'FAT16OK' -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-drive file=$(FAT16_SMOKE_IMG),format=raw,if=ide,index=1 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rc=$$?; rm -f $$DISK; \
	if tr -d '\n\r' < $(FAT16_SMOKE_LOG) | grep -q 'FAT16OK'; then \
		echo "✓ smoke-fat16-mount passed (hdb FAT16 mount + HELLO.TXT read)"; \
	elif [ $$rc -ne 0 ]; then \
		echo "✗ smoke-fat16-mount FAILED (QEMU/autokill)"; exit $$rc; \
	else \
		echo "✗ smoke-fat16-mount FAILED (tag missing)"; exit 1; \
	fi

# D1.19 — Linux↔IR0 ABI ground-truth audit (same ELF, strace vs serial)
LINUX_ABI_AUDIT_DIR := build/linux_abi_audit
LINUX_ABI_BRK_PROBE := $(LINUX_ABI_AUDIT_DIR)/brk_probe
LINUX_ABI_WAIT4_PROBE := $(LINUX_ABI_AUDIT_DIR)/wait4_probe
LINUX_ABI_READ_PROBE := $(LINUX_ABI_AUDIT_DIR)/read_probe
LINUX_ABI_MMAP_PROBE := $(LINUX_ABI_AUDIT_DIR)/mmap_probe
LINUX_ABI_MOUNT_PROBE := $(LINUX_ABI_AUDIT_DIR)/mount_probe
LINUX_ABI_OPENAT_PROBE := $(LINUX_ABI_AUDIT_DIR)/openat_probe
LINUX_ABI_STAT_PROBE := $(LINUX_ABI_AUDIT_DIR)/stat_probe
LINUX_ABI_VFS_WRITE_PROBE := $(LINUX_ABI_AUDIT_DIR)/vfs_write_probe

.PHONY: build-linux-abi-brk-probe build-linux-abi-wait4-probe build-linux-abi-read-probe \
	build-linux-abi-mmap-probe build-linux-abi-mount-probe \
	build-linux-abi-openat-probe build-linux-abi-stat-probe build-linux-abi-vfs-write-probe \
	linux-abi-audit linux-abi-audit-brk linux-abi-audit-wait4 linux-abi-audit-read \
	linux-abi-audit-pipe linux-abi-audit-poll linux-abi-audit-nanosleep \
	linux-abi-audit-getcwd linux-abi-audit-chdir linux-abi-audit-dup linux-abi-audit-execve \
	linux-abi-audit-mmap linux-abi-audit-mount linux-abi-audit-openat linux-abi-audit-stat \
	linux-abi-audit-vfs-write linux-abi-audit-sigreturn-blocked-syscall

build-linux-abi-brk-probe: scripts/linux_abi/workloads/brk_probe.c
	@mkdir -p $(LINUX_ABI_AUDIT_DIR)
	@if command -v musl-gcc >/dev/null 2>&1; then \
		musl-gcc -static -Os -o $(LINUX_ABI_BRK_PROBE) scripts/linux_abi/workloads/brk_probe.c; \
	else \
		gcc -static -Os -o $(LINUX_ABI_BRK_PROBE) scripts/linux_abi/workloads/brk_probe.c; \
	fi
	@echo "✓ $(LINUX_ABI_BRK_PROBE)"

build-linux-abi-wait4-probe: scripts/linux_abi/workloads/wait4_probe.c
	@mkdir -p $(LINUX_ABI_AUDIT_DIR)
	@if command -v musl-gcc >/dev/null 2>&1; then \
		musl-gcc -static -Os -o $(LINUX_ABI_WAIT4_PROBE) scripts/linux_abi/workloads/wait4_probe.c; \
	else \
		gcc -static -Os -o $(LINUX_ABI_WAIT4_PROBE) scripts/linux_abi/workloads/wait4_probe.c; \
	fi
	@echo "✓ $(LINUX_ABI_WAIT4_PROBE)"

build-linux-abi-read-probe: scripts/linux_abi/workloads/read_probe.c
	@mkdir -p $(LINUX_ABI_AUDIT_DIR)
	@if command -v musl-gcc >/dev/null 2>&1; then \
		musl-gcc -static -Os -o $(LINUX_ABI_READ_PROBE) scripts/linux_abi/workloads/read_probe.c; \
	else \
		gcc -static -Os -o $(LINUX_ABI_READ_PROBE) scripts/linux_abi/workloads/read_probe.c; \
	fi
	@echo "✓ $(LINUX_ABI_READ_PROBE)"

build-linux-abi-mmap-probe: scripts/linux_abi/workloads/mmap_probe.c
	@mkdir -p $(LINUX_ABI_AUDIT_DIR)
	@if command -v musl-gcc >/dev/null 2>&1; then \
		musl-gcc -static -Os -o $(LINUX_ABI_MMAP_PROBE) scripts/linux_abi/workloads/mmap_probe.c; \
	else \
		gcc -static -Os -o $(LINUX_ABI_MMAP_PROBE) scripts/linux_abi/workloads/mmap_probe.c; \
	fi
	@echo "✓ $(LINUX_ABI_MMAP_PROBE)"

build-linux-abi-mount-probe: scripts/linux_abi/workloads/mount_probe.c
	@mkdir -p $(LINUX_ABI_AUDIT_DIR)
	@if command -v musl-gcc >/dev/null 2>&1; then \
		musl-gcc -static -Os -o $(LINUX_ABI_MOUNT_PROBE) scripts/linux_abi/workloads/mount_probe.c; \
	else \
		gcc -static -Os -o $(LINUX_ABI_MOUNT_PROBE) scripts/linux_abi/workloads/mount_probe.c; \
	fi
	@echo "✓ $(LINUX_ABI_MOUNT_PROBE)"

build-linux-abi-openat-probe: scripts/linux_abi/workloads/openat_probe.c
	@mkdir -p $(LINUX_ABI_AUDIT_DIR)
	@if command -v musl-gcc >/dev/null 2>&1; then \
		musl-gcc -static -Os -DOPEN_EXISTING_PATH=\"/proc/uptime\" -o $(LINUX_ABI_OPENAT_PROBE) scripts/linux_abi/workloads/openat_probe.c; \
	else \
		gcc -static -Os -DOPEN_EXISTING_PATH=\"/proc/uptime\" -o $(LINUX_ABI_OPENAT_PROBE) scripts/linux_abi/workloads/openat_probe.c; \
	fi
	@echo "✓ $(LINUX_ABI_OPENAT_PROBE)"

build-linux-abi-stat-probe: scripts/linux_abi/workloads/stat_probe.c
	@mkdir -p $(LINUX_ABI_AUDIT_DIR)
	@if command -v musl-gcc >/dev/null 2>&1; then \
		musl-gcc -static -Os -o $(LINUX_ABI_STAT_PROBE) scripts/linux_abi/workloads/stat_probe.c; \
	else \
		gcc -static -Os -o $(LINUX_ABI_STAT_PROBE) scripts/linux_abi/workloads/stat_probe.c; \
	fi
	@echo "✓ $(LINUX_ABI_STAT_PROBE)"

build-linux-abi-vfs-write-probe: scripts/linux_abi/workloads/vfs_write_probe.c
	@mkdir -p $(LINUX_ABI_AUDIT_DIR)
	@if command -v musl-gcc >/dev/null 2>&1; then \
		musl-gcc -static -Os -o $(LINUX_ABI_VFS_WRITE_PROBE) scripts/linux_abi/workloads/vfs_write_probe.c; \
	else \
		gcc -static -Os -o $(LINUX_ABI_VFS_WRITE_PROBE) scripts/linux_abi/workloads/vfs_write_probe.c; \
	fi
	@echo "✓ $(LINUX_ABI_VFS_WRITE_PROBE)"

LINUX_ABI_VFS_WRITE_FAT_PROBE := $(LINUX_ABI_AUDIT_DIR)/vfs_write_fat_probe
build-linux-abi-vfs-write-fat-probe: setup/pid1/init_vfs_write_fat.c
	@mkdir -p $(LINUX_ABI_AUDIT_DIR)
	@if command -v musl-gcc >/dev/null 2>&1; then \
		musl-gcc -static -Os -o $(LINUX_ABI_VFS_WRITE_FAT_PROBE) setup/pid1/init_vfs_write_fat.c; \
	else \
		gcc -static -Os -o $(LINUX_ABI_VFS_WRITE_FAT_PROBE) setup/pid1/init_vfs_write_fat.c; \
	fi
	@echo "✓ $(LINUX_ABI_VFS_WRITE_FAT_PROBE)"

linux-abi-audit-vfs-write-fat: kernel-x64-userspace.iso build-linux-abi-vfs-write-probe build-linux-abi-vfs-write-fat-probe
	@chmod +x scripts/linux_abi/run_linux_vfs_write.sh scripts/linux_abi/run_ir0_vfs_write_fat.sh
	@python3 scripts/linux_abi_audit.py --contract vfs_write_fat || true
	@if grep -q 'bundle_status: VERIFIED' $(LINUX_ABI_AUDIT_DIR)/report.md && grep -qi 'vfs_write_fat\|FAT' $(LINUX_ABI_AUDIT_DIR)/report.md 2>/dev/null; then \
		echo "✓ linux-abi-audit-vfs-write-fat VERIFIED (see $(LINUX_ABI_AUDIT_DIR)/report.md)"; \
	elif grep -q 'vfs_write_fat.*VERIFIED\|bundle_status: VERIFIED' $(LINUX_ABI_AUDIT_DIR)/report.md; then \
		echo "✓ linux-abi-audit-vfs-write-fat VERIFIED (see $(LINUX_ABI_AUDIT_DIR)/report.md)"; \
	else \
		echo "✗ linux-abi-audit-vfs-write-fat — see $(LINUX_ABI_AUDIT_DIR)/report.md"; \
		tail -40 $(LINUX_ABI_AUDIT_DIR)/report.md; \
		exit 1; \
	fi

linux-abi-audit: kernel-x64-userspace.iso build-linux-abi-brk-probe
	@chmod +x scripts/linux_abi/run_linux_brk.sh scripts/linux_abi/run_ir0_brk.sh
	@python3 scripts/linux_abi_audit.py --all
	@grep -q '^## Overall: PASS' $(LINUX_ABI_AUDIT_DIR)/report.md && \
		echo "✓ linux-abi-audit passed (see $(LINUX_ABI_AUDIT_DIR)/report.md)" || \
		(echo "✗ linux-abi-audit FAILED — see $(LINUX_ABI_AUDIT_DIR)/report.md"; exit 1)

linux-abi-audit-brk: linux-abi-audit

linux-abi-audit-wait4: kernel-x64-userspace.iso build-linux-abi-wait4-probe
	@chmod +x scripts/linux_abi/run_linux_wait4.sh scripts/linux_abi/run_ir0_wait4.sh
	@python3 scripts/linux_abi_audit.py --contract wait4
	@grep -q '^## wait4 — PASS' $(LINUX_ABI_AUDIT_DIR)/report.md && \
		echo "✓ linux-abi-audit-wait4 passed (see $(LINUX_ABI_AUDIT_DIR)/report.md)" || \
		(echo "✗ linux-abi-audit-wait4 FAILED — see $(LINUX_ABI_AUDIT_DIR)/report.md"; exit 1)

linux-abi-audit-read: kernel-x64-userspace.iso build-linux-abi-read-probe
	@chmod +x scripts/linux_abi/run_linux_read.sh scripts/linux_abi/run_ir0_read.sh
	@python3 scripts/linux_abi_audit.py --contract read
	@grep -q '^## read — PASS' $(LINUX_ABI_AUDIT_DIR)/report.md && \
		echo "✓ linux-abi-audit-read passed (see $(LINUX_ABI_AUDIT_DIR)/report.md)" || \
		(echo "✗ linux-abi-audit-read FAILED — see $(LINUX_ABI_AUDIT_DIR)/report.md"; exit 1)

linux-abi-audit-pipe: kernel-x64-userspace.iso build-linux-abi-read-probe
	@chmod +x scripts/linux_abi/run_linux_read.sh scripts/linux_abi/run_ir0_read.sh
	@python3 scripts/linux_abi_audit.py --contract pipe
	@grep -q '^## pipe — PASS' $(LINUX_ABI_AUDIT_DIR)/report.md && \
		echo "✓ linux-abi-audit-pipe passed (see $(LINUX_ABI_AUDIT_DIR)/report.md)" || \
		(echo "✗ linux-abi-audit-pipe FAILED — see $(LINUX_ABI_AUDIT_DIR)/report.md"; exit 1)

linux-abi-audit-poll: kernel-x64-userspace.iso
	@chmod +x scripts/linux_abi/run_linux_workload.sh scripts/linux_abi/run_ir0_workload.sh
	@python3 scripts/linux_abi_audit.py --contract poll
	@grep -q '^## poll — PASS' $(LINUX_ABI_AUDIT_DIR)/report.md && \
		echo "✓ linux-abi-audit-poll passed (see $(LINUX_ABI_AUDIT_DIR)/report.md)" || \
		(echo "✗ linux-abi-audit-poll FAILED — see $(LINUX_ABI_AUDIT_DIR)/report.md"; exit 1)

linux-abi-audit-nanosleep: kernel-x64-userspace.iso
	@chmod +x scripts/linux_abi/run_linux_workload.sh scripts/linux_abi/run_ir0_workload.sh
	@python3 scripts/linux_abi_audit.py --contract nanosleep
	@grep -q '^## nanosleep — PASS' $(LINUX_ABI_AUDIT_DIR)/report.md && \
		echo "✓ linux-abi-audit-nanosleep passed (see $(LINUX_ABI_AUDIT_DIR)/report.md)" || \
		(echo "✗ linux-abi-audit-nanosleep FAILED — see $(LINUX_ABI_AUDIT_DIR)/report.md"; exit 1)

linux-abi-audit-getcwd: kernel-x64-userspace.iso
	@chmod +x scripts/linux_abi/run_linux_workload.sh scripts/linux_abi/run_ir0_workload.sh
	@python3 scripts/linux_abi_audit.py --contract getcwd
	@grep -q '^## getcwd — PASS' $(LINUX_ABI_AUDIT_DIR)/report.md && \
		echo "✓ linux-abi-audit-getcwd passed (see $(LINUX_ABI_AUDIT_DIR)/report.md)" || \
		(echo "✗ linux-abi-audit-getcwd FAILED — see $(LINUX_ABI_AUDIT_DIR)/report.md"; exit 1)

linux-abi-audit-chdir: kernel-x64-userspace.iso
	@chmod +x scripts/linux_abi/run_linux_workload.sh scripts/linux_abi/run_ir0_workload.sh
	@python3 scripts/linux_abi_audit.py --contract chdir
	@grep -q '^## chdir — PASS' $(LINUX_ABI_AUDIT_DIR)/report.md && \
		echo "✓ linux-abi-audit-chdir passed (see $(LINUX_ABI_AUDIT_DIR)/report.md)" || \
		(echo "✗ linux-abi-audit-chdir FAILED — see $(LINUX_ABI_AUDIT_DIR)/report.md"; exit 1)

linux-abi-audit-dup: kernel-x64-userspace.iso
	@chmod +x scripts/linux_abi/run_linux_workload.sh scripts/linux_abi/run_ir0_workload.sh
	@python3 scripts/linux_abi_audit.py --contract dup
	@grep -q '^## dup — PASS' $(LINUX_ABI_AUDIT_DIR)/report.md && \
		echo "✓ linux-abi-audit-dup passed (see $(LINUX_ABI_AUDIT_DIR)/report.md)" || \
		(echo "✗ linux-abi-audit-dup FAILED — see $(LINUX_ABI_AUDIT_DIR)/report.md"; exit 1)

linux-abi-audit-execve: kernel-x64-userspace.iso
	@chmod +x scripts/linux_abi/run_linux_execve.sh scripts/linux_abi/run_ir0_execve.sh 2>/dev/null || true
	@python3 scripts/linux_abi_audit.py --contract execve
	@grep -q '^## execve — PASS' $(LINUX_ABI_AUDIT_DIR)/report.md && \
		echo "✓ linux-abi-audit-execve passed (see $(LINUX_ABI_AUDIT_DIR)/report.md)" || \
		(echo "✗ linux-abi-audit-execve FAILED — see $(LINUX_ABI_AUDIT_DIR)/report.md"; exit 1)

linux-abi-audit-mmap: kernel-x64-userspace.iso build-linux-abi-mmap-probe
	@chmod +x scripts/linux_abi/run_linux_mmap.sh scripts/linux_abi/run_ir0_mmap.sh
	@python3 scripts/linux_abi_audit.py --contract mmap
	@grep -q '^## mmap — PASS' $(LINUX_ABI_AUDIT_DIR)/report.md && \
		echo "✓ linux-abi-audit-mmap passed (see $(LINUX_ABI_AUDIT_DIR)/report.md)" || \
		(echo "✗ linux-abi-audit-mmap FAILED — see $(LINUX_ABI_AUDIT_DIR)/report.md"; exit 1)

linux-abi-audit-mount: kernel-x64-userspace.iso build-linux-abi-mount-probe
	@chmod +x scripts/linux_abi/run_linux_mount.sh scripts/linux_abi/run_ir0_mount.sh
	@python3 scripts/linux_abi_audit.py --contract mount
	@grep -q '^## mount — PASS' $(LINUX_ABI_AUDIT_DIR)/report.md && \
		echo "✓ linux-abi-audit-mount passed (see $(LINUX_ABI_AUDIT_DIR)/report.md)" || \
		(echo "✗ linux-abi-audit-mount FAILED — see $(LINUX_ABI_AUDIT_DIR)/report.md"; exit 1)

linux-abi-audit-openat: kernel-x64-userspace.iso build-linux-abi-openat-probe
	@chmod +x scripts/linux_abi/run_linux_openat.sh scripts/linux_abi/run_ir0_openat.sh
	@python3 scripts/linux_abi_audit.py --contract openat
	@grep -q '^## openat — PASS' $(LINUX_ABI_AUDIT_DIR)/report.md && \
		echo "✓ linux-abi-audit-openat passed (see $(LINUX_ABI_AUDIT_DIR)/report.md)" || \
		(echo "✗ linux-abi-audit-openat FAILED — see $(LINUX_ABI_AUDIT_DIR)/report.md"; exit 1)

linux-abi-audit-stat: kernel-x64-userspace.iso build-linux-abi-stat-probe
	@chmod +x scripts/linux_abi/run_linux_stat.sh scripts/linux_abi/run_ir0_stat.sh
	@python3 scripts/linux_abi_audit.py --contract stat
	@grep -q '^## stat — PASS' $(LINUX_ABI_AUDIT_DIR)/report.md && \
		echo "✓ linux-abi-audit-stat passed (see $(LINUX_ABI_AUDIT_DIR)/report.md)" || \
		(echo "✗ linux-abi-audit-stat FAILED — see $(LINUX_ABI_AUDIT_DIR)/report.md"; exit 1)

linux-abi-audit-vfs-write: kernel-x64-userspace.iso build-linux-abi-vfs-write-probe
	@chmod +x scripts/linux_abi/run_linux_vfs_write.sh scripts/linux_abi/run_ir0_vfs_write.sh
	@python3 scripts/linux_abi_audit.py --contract vfs_write || true
	@if grep -q 'bundle_status: VERIFIED' $(LINUX_ABI_AUDIT_DIR)/report.md; then \
		echo "✓ linux-abi-audit-vfs-write VERIFIED (see $(LINUX_ABI_AUDIT_DIR)/report.md)"; \
	elif grep -q 'bundle_status: PARTIAL' $(LINUX_ABI_AUDIT_DIR)/report.md; then \
		echo "△ linux-abi-audit-vfs-write PARTIAL (see $(LINUX_ABI_AUDIT_DIR)/report.md)"; \
	else \
		echo "✗ linux-abi-audit-vfs-write BLOCKED — see $(LINUX_ABI_AUDIT_DIR)/report.md"; exit 1; \
	fi

linux-abi-audit-sigreturn-blocked-syscall: kernel-x64-userspace.iso
	@chmod +x scripts/linux_abi/run_linux_workload.sh scripts/linux_abi/run_ir0_workload.sh
	@python3 scripts/linux_abi_audit.py --contract sigreturn_blocked_syscall
	@grep -q '^## sigreturn_blocked_syscall — PASS' $(LINUX_ABI_AUDIT_DIR)/report.md && \
		echo "✓ linux-abi-audit-sigreturn-blocked-syscall passed (see $(LINUX_ABI_AUDIT_DIR)/report.md)" || \
		(echo "✗ linux-abi-audit-sigreturn-blocked-syscall FAILED — see $(LINUX_ABI_AUDIT_DIR)/report.md"; exit 1)

smoke-runit-ash-interactive: load-userspace-runit kernel-x64-userspace.iso
	@echo "  SMOKE   runit PID1 + ash interactive (headless + monitor sendkey)..."
	@chmod +x scripts/smoke_runit_ash_interactive.py
	@python3 scripts/smoke_runit_ash_interactive.py --log $(RUNIT_ASH_SMOKE_LOG) --timeout 90 --iso kernel-x64-userspace.iso --disk disk.img
	@echo "  LOG     $(RUNIT_ASH_SMOKE_LOG)"

# T1 GUI — runit → BusyBox ash on /dev/console (tier1 stable; not legacy-only).
.PHONY: run-fase58e-ash-gui check-fase58e-logs

run-fase58e-ash-gui: load-userspace-runit kernel-x64-userspace.iso
	@case "$(FASE58E_DISPLAY)" in none|headless) \
		echo "✗ FASE58E ash GUI blocked: FASE58E_DISPLAY=$(FASE58E_DISPLAY)"; exit 1;; esac
	@echo "  FASE58E   runit → getty/login → ash on /dev/console"
	@echo "  QEMU     display=$(FASE58E_DISPLAY)"
	@echo "  LOG      serial -> $(FASE58E_ASH_LOG)"
	@echo "  HINT     login: auto (etc/ir0-autologin) or IR0_NO_AUTOLOGIN=1 → root/(empty) or ivan/ivan"
	@echo "  HINT     then: ls / pwd / echo hi"
	@echo "  HINT     Doom via virtio-9p: /mnt/host/doomgeneric /mnt/host/doom1.wad"
	@rm -f $(FASE58E_ASH_LOG); \
	DISK=$$(mktemp /tmp/ir0-fase58e-ash.XXXXXX.img); \
	SHARE=$$(mktemp -d /tmp/ir0-fase58e-share.XXXXXX); \
	cp -f disk.img $$DISK; \
	VIRTFS=""; \
	if [ -n "$(REAL_WAD_PATH)" ] && [ -f "$(REAL_WAD_PATH)" ]; then \
		$(MAKE) -s build-ktm-doom-interactive; \
		cp -f $(KTM_DOOM_INTERACTIVE_BIN) $$SHARE/doomgeneric; \
		cp -f "$(REAL_WAD_PATH)" $$SHARE/doom1.wad; \
		printf '0\n0\n' > $$SHARE/doom-frames; \
		VIRTFS="-fsdev local,id=ir0fs,path=$$SHARE,security_model=none -device virtio-9p-pci,fsdev=ir0fs,mount_tag=ir0share,disable-modern=on"; \
		echo "  SHARE   Doom/WAD on virtio-9p ($$SHARE) — mount: mkdir -p /mnt/host && mount -t 9p ir0share /mnt/host"; \
	fi; \
	python3 scripts/verify_minix_rootfs.py $$DISK /sbin/init /bin/sh /bin/busybox; \
	if [ "$(FASE58E_DISPLAY)" = "sdl" ]; then \
		DISP="-display sdl2"; \
	else \
		DISP="-display gtk"; \
	fi; \
	$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		$$VIRTFS \
		-serial file:$(FASE58E_ASH_LOG) \
		$$DISP -m 256M -no-reboot -net none; \
	rm -f $$DISK; \
	rm -rf $$SHARE

check-fase58e-logs:
	@echo "=== FASE58E/K (runit ash GUI + compact smoke tags) ==="
	@if [ -f "$(FASE58E_ASH_LOG)" ]; then \
		grep -E 'RUNIT_STAGE1_OK|RUNIT_STAGE2_OK|RUNSV_CONSOLE_START|ASH_INTERACTIVE_READY|KBD_USER_POLL_OK|TTY_CANON_LINE_READY|SYS_READ_RETURN_OK|ASH_COMMAND_ECHO_OK|ASH_COMMAND_EXEC_OK' "$(FASE58E_ASH_LOG)" || echo "(no FASE58E/K tags)"; \
	else echo "missing $(FASE58E_ASH_LOG)"; fi
	@if [ -f "$(FASE58E_ASH_SMOKE_LOG)" ]; then \
		echo "=== FASE58E ash smoke ($(FASE58E_ASH_SMOKE_LOG)) ==="; \
		grep -E 'RUNIT_STAGE1_OK|RUNIT_STAGE2_OK|RUNSV_CONSOLE_START|ASH_INTERACTIVE_READY|KBD_USER_POLL_OK|TTY_CANON_LINE_READY|SYS_READ_RETURN_OK|ASH_COMMAND_ECHO_OK|ASH_COMMAND_EXEC_OK' "$(FASE58E_ASH_SMOKE_LOG)" || echo "(no smoke tags)"; \
	fi

build-tcc-fase52:
	@./setup/tcc/build-fase52.sh

build-ktm-tcc-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  HARNESS Building FASE52 TCC smoke ($(KTM_TCC_HARNESS_BIN))"
	@$(MUSL_CC) -static -Os -o $(KTM_TCC_HARNESS_BIN) $(KTM_TCC_SMOKE_SRC)
	@file $(KTM_TCC_HARNESS_BIN) | grep -q ELF
	@echo "✓ build-ktm-tcc-smoke OK"

build-ktm-fs-dev-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE53A fs/dev smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(KTM_FS_DEV_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-ktm-fs-dev-smoke OK"

build-ktm-posix-pseudofs-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE53B posix/pseudo-fs smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(KTM_POSIX_PSEUDOFS_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-ktm-posix-pseudofs-smoke OK"

build-init-heart-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building /heart smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_HEART_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-heart-smoke OK"

build-ktm-fbdev-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE54A fbdev smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(KTM_FBDEV_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-ktm-fbdev-smoke OK"

build-ktm-boot-halt-bin:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    FASE58C boot-halt probe ($(KTM_BOOT_HALT_BIN))"
	@$(MUSL_CC) -static -Os -o $(KTM_BOOT_HALT_BIN) $(KTM_BOOT_HALT_SMOKE_SRC)
	@file $(KTM_BOOT_HALT_BIN) | grep -q ELF
	@echo "✓ build-ktm-boot-halt-bin OK"

build-ktm-fbdev-gui-bin:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    FASE58C fbdev probe ($(KTM_FBDEV_GUI_BIN))"
	@$(MUSL_CC) -static -Os -o $(KTM_FBDEV_GUI_BIN) $(KTM_FBDEV_GUI_SMOKE_SRC)
	@file $(KTM_FBDEV_GUI_BIN) | grep -q ELF
	@echo "✓ build-ktm-fbdev-gui-bin OK"

build-ktm-input-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE54B input smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(KTM_INPUT_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-ktm-input-smoke OK"

build-ktm-input-det-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE54C deterministic input smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(KTM_INPUT_DET_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-ktm-input-det-smoke OK"

build-ktm-doom-prereq-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE55A doom prereq smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(KTM_DOOM_PREREQ_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-ktm-doom-prereq-smoke OK"

build-ktm-doom-stub:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE55B doom stub ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(KTM_DOOM_STUB_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-ktm-doom-stub OK"

build-ktm-doom-timing-stub:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE55C timing+input smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(KTM_DOOM_TIMING_STUB_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-ktm-doom-timing-stub OK"

build-ktm-doomgeneric-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  HARNESS Building FASE55D real doomgeneric ($(KTM_DOOMGENERIC_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -s -ffunction-sections -fdata-sections \
		-Wl,--gc-sections -Wl,--strip-all -std=gnu99 \
		-DIR0_DOOM_PORT -DFEATURE_SOUND \
		-Isetup/doom/upstream/doomgeneric \
		$(KTM_DOOMGENERIC_SRC) \
		setup/doom/upstream/doomgeneric/*.c \
		-o $(KTM_DOOMGENERIC_SMOKE_BIN) -lm
	@file $(KTM_DOOMGENERIC_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-ktm-doomgeneric-smoke OK"

build-ktm-doom-interactive:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  DOOM    Building FASE55E interactive doomgeneric ($(KTM_DOOM_INTERACTIVE_BIN))"
	@$(MUSL_CC) -static -Os -s -ffunction-sections -fdata-sections \
		-Wl,--gc-sections -Wl,--strip-all -std=gnu99 \
		-DFASE55E_INTERACTIVE=1 -DIR0_DOOM_PORT -DFEATURE_SOUND \
		-Isetup/doom/upstream/doomgeneric \
		$(KTM_DOOMGENERIC_SRC) \
		setup/doom/upstream/doomgeneric/*.c \
		-o $(KTM_DOOM_INTERACTIVE_BIN) -lm
	@file $(KTM_DOOM_INTERACTIVE_BIN) | grep -q ELF
	@echo "✓ build-ktm-doom-interactive OK"

build-ktm-programs-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE50 programs smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(KTM_PROGRAMS_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-ktm-programs-smoke OK"

# Kernel ISO booting /sbin/init (CONFIG_KERNEL_DEBUG_SHELL=n)
# Default: lazy anon mmap + brk (defconfig LAZY_*=y). Bisect: make kernel-x64-userspace-eager.bin
kernel-x64-userspace.bin:
	@rm -f kernel/main.o kernel/process/*.o kernel/elf_loader.o \
		mm/paging.o arch/common/arch_interface.o kernel/console_backend.o \
		drivers/video/console.o sched/rr_sched.o
	@$(MAKE) kernel-x64.bin USERSPACE_INIT_BUILD=1
	@cp kernel-x64.bin $@
	@rm -f kernel/main.o kernel/process/*.o
	@$(MAKE) kernel-x64.bin
	@echo "✓ Kernel (userspace init, lazy MM) copied: $@"

kernel-x64-userspace-eager.bin:
	@rm -f kernel/main.o kernel/process/*.o kernel/elf_loader.o \
		mm/paging.o arch/common/arch_interface.o kernel/console_backend.o \
		drivers/video/console.o sched/rr_sched.o
	@$(MAKE) kernel-x64.bin USERSPACE_INIT_BUILD=1 USERSPACE_EAGER_MM=1
	@cp kernel-x64.bin $@
	@rm -f kernel/main.o kernel/process/*.o
	@$(MAKE) kernel-x64.bin
	@echo "✓ Kernel (userspace init, eager MM bisect) copied: $@"

# Back-compat alias (same kernel as kernel-x64-userspace.bin).
kernel-x64-userspace-lazy.bin: kernel-x64-userspace.bin
	@cp kernel-x64-userspace.bin $@
	@echo "✓ Kernel (lazy MM alias) copied: $@"

kernel-x64-userspace.iso: kernel-x64-userspace.bin arch/x86-64/grub.cfg
	@echo "  ISO     $@ (userspace init boot, lazy MM)"
	@rm -rf iso_userspace
	@mkdir -p iso_userspace/boot/grub
	@cp arch/x86-64/grub.cfg iso_userspace/boot/grub/
	@cp kernel-x64-userspace.bin iso_userspace/boot/kernel-x64.bin
	@grub-mkrescue -o $@ iso_userspace
	@rm -rf iso_userspace
	@echo "  ISO     $@"

kernel-x64-userspace-lazy.iso: kernel-x64-userspace.iso
	@cp kernel-x64-userspace.iso $@
	@echo "✓ ISO (lazy MM alias) created: $@"

kernel-x64-userspace-eager.iso: kernel-x64-userspace-eager.bin arch/x86-64/grub.cfg
	@echo "  ISO     $@ (userspace init boot, eager MM bisect)"
	@rm -rf iso_userspace_eager
	@mkdir -p iso_userspace_eager/boot/grub
	@cp arch/x86-64/grub.cfg iso_userspace_eager/boot/grub/
	@cp kernel-x64-userspace-eager.bin iso_userspace_eager/boot/kernel-x64.bin
	@grub-mkrescue -o $@ iso_userspace_eager
	@rm -rf iso_userspace_eager
	@echo "✓ ISO (userspace init, eager MM bisect) created: $@"

# --- Validation (tooling entry points; historical smokes: IR0_LEGACY_SMOKE=1) ---
.PHONY: ctr smoke-tier1 test-fast agent-fast diag-contract ktm ktm-check ktm-manifest ktm-classify ktm-classify-selftest ktm-report

ctr:
	@chmod +x scripts/ctr.sh
	@./scripts/ctr.sh

test-fast: kernel-x64.bin arch-guard
	@$(MAKE) -s -C tests/host run

agent-fast: test-fast kernel-tests
	@echo "✓ agent-fast OK (host + ktest)"

diag-contract:
	@chmod +x scripts/ir0-diag.sh
	@test -n "$(CONTRACT)" || (echo "usage: make diag-contract CONTRACT=poll" >&2; exit 2)
	@scripts/ir0-diag.sh contract "$(CONTRACT)"

smoke-tier1: kernel-x64.bin arch-guard
	@$(MAKE) -s smoke-runit-boot
	@$(MAKE) -s smoke-runit-ash-interactive

# Release 0.0.1 gate — deterministic regression bundle (D1.20).
# Does NOT include smoke-desk-* (optional sibling IR0-desktop; see TREE_CONTRACT).
.PHONY: smoke-release-0.0.1 release-0.0.1

smoke-release-0.0.1:
	@echo "  RELEASE 0.0.1 gate (D1.20 deterministic bundle)"
	@$(MAKE) -s roadmap-phase1-stability
	@$(MAKE) -s linux-abi-audit
	@$(MAKE) -s smoke-runit-ash-interactive
	@$(MAKE) -s smoke-fat16-mount
	@echo "✓ smoke-release-0.0.1 passed"

release-0.0.1: kernel-text-budget smoke-release-0.0.1
	@echo "✓ release-0.0.1 gate passed (kernel-text-budget + smoke-release-0.0.1)"

# T3 kernel-prep battery (IPC/shm/fb MAP_SHARED) — not Xfbdev boot itself.
.PHONY: smoke-t3-prep
smoke-t3-prep:
	@echo "  SMOKE   T3 prep battery (unix/shm/fb/epoll)..."
	@$(MAKE) -s smoke-stream-sock
	@$(MAKE) -s smoke-socketpair
	@$(MAKE) -s smoke-unix-abstract
	@$(MAKE) -s smoke-scm-rights
	@$(MAKE) -s smoke-sysv-shm
	@$(MAKE) -s smoke-memfd-shared
	@$(MAKE) -s smoke-posix-shm
	@$(MAKE) -s smoke-unix-harden
	@$(MAKE) -s smoke-unix-flags
	@$(MAKE) -s smoke-event-fds
	@$(MAKE) -s smoke-fb-map-shared
	@$(MAKE) -s smoke-epoll-basic
	@echo "✓ smoke-t3-prep passed"

# ---------------------------------------------------------------------------
# DESK smokes — OPTIONAL out-of-tree product tests (SEP-1 tree contract).
# Sibling IR0-desktop only. NOT part of release-0.0.1 / merge-critical battery.
# Missing clone → exit 2 (never silent PASS). See:
#   ../IR0-desktop/Documentation/TREE_CONTRACT.md
#   Documentation/ARCH_DEBT_SEP.md
# TinyX remains force_tinyx lab-only; soft DESK ≠ ship desktop ISO.
# ---------------------------------------------------------------------------
IR0_DESKTOP_ROOT ?= $(abspath $(KERNEL_ROOT)/../IR0-desktop)
.PHONY: smoke-desk-xfbdev
smoke-desk-xfbdev:
	@if [ ! -f "$(IR0_DESKTOP_ROOT)/smoke/run-xfbdev-smoke.sh" ]; then \
		echo "✗ smoke-desk-xfbdev: missing $(IR0_DESKTOP_ROOT)/smoke/run-xfbdev-smoke.sh" >&2; \
		echo "  Optional target: clone IR0-desktop next to IR0, or set IR0_DESKTOP_ROOT=" >&2; \
		echo "  Not required for make release-0.0.1 / smoke-release-0.0.1" >&2; \
		exit 2; \
	fi
	@if ! command -v "$${MUSL_CC:-x86_64-linux-musl-gcc}" >/dev/null 2>&1; then \
		echo "✗ smoke-desk-xfbdev: need MUSL_CC (default x86_64-linux-musl-gcc) in PATH" >&2; \
		exit 2; \
	fi
	@echo "  SMOKE   DESK-1 mini X (optional sibling IR0-desktop → IR0_XFBDEV_SMOKE_OK)..."
	@IR0_ROOT="$(KERNEL_ROOT)" bash "$(IR0_DESKTOP_ROOT)/smoke/run-xfbdev-smoke.sh"
	@echo "✓ smoke-desk-xfbdev passed (DESK-1)"

.PHONY: smoke-desk-wm
smoke-desk-wm:
	@if [ ! -f "$(IR0_DESKTOP_ROOT)/smoke/run-desk-wm-smoke.sh" ]; then \
		echo "✗ smoke-desk-wm: missing $(IR0_DESKTOP_ROOT)/smoke/run-desk-wm-smoke.sh" >&2; \
		echo "  Optional DESK target — not part of release-0.0.1" >&2; \
		exit 2; \
	fi
	@if ! command -v "$${MUSL_CC:-x86_64-linux-musl-gcc}" >/dev/null 2>&1; then \
		echo "✗ smoke-desk-wm: need MUSL_CC in PATH" >&2; \
		exit 2; \
	fi
	@echo "  SMOKE   DESK-2 fb session WM/panel (IR0_DESK_WM_SMOKE_OK)..."
	@IR0_ROOT="$(KERNEL_ROOT)" bash "$(IR0_DESKTOP_ROOT)/smoke/run-desk-wm-smoke.sh"
	@echo "✓ smoke-desk-wm passed (DESK-2)"

.PHONY: smoke-desk-session
smoke-desk-session:
	@if [ ! -f "$(IR0_DESKTOP_ROOT)/smoke/run-desk-session-smoke.sh" ]; then \
		echo "✗ smoke-desk-session: missing $(IR0_DESKTOP_ROOT)/smoke/run-desk-session-smoke.sh" >&2; \
		echo "  Optional DESK X session — not part of release-0.0.1" >&2; \
		exit 2; \
	fi
	@if ! command -v "$${MUSL_CC:-x86_64-linux-musl-gcc}" >/dev/null 2>&1; then \
		echo "✗ smoke-desk-session: need MUSL_CC in PATH" >&2; \
		exit 2; \
	fi
	@echo "  SMOKE   DESK mini X + WM session (IR0_DESK_SESSION_OK)..."
	@IR0_ROOT="$(KERNEL_ROOT)" bash "$(IR0_DESKTOP_ROOT)/smoke/run-desk-session-smoke.sh"
	@echo "✓ smoke-desk-session passed (mini X WM)"

.PHONY: smoke-desk-classicube smoke-desk-play smoke-desk
smoke-desk-classicube:
	@if [ ! -f "$(IR0_DESKTOP_ROOT)/smoke/run-desk-classicube-smoke.sh" ]; then \
		echo "✗ smoke-desk-classicube: missing sibling script (optional; not release)" >&2; \
		exit 2; \
	fi
	@echo "  SMOKE   DESK-3 ClassiCube soft_fb0 (CLASSICUBE_OK)..."
	@IR0_ROOT="$(KERNEL_ROOT)" bash "$(IR0_DESKTOP_ROOT)/smoke/run-desk-classicube-smoke.sh"
	@echo "✓ smoke-desk-classicube passed (DESK-3 soft)"

smoke-desk-play:
	@if [ ! -f "$(IR0_DESKTOP_ROOT)/smoke/run-desk-classicube-smoke.sh" ]; then \
		echo "✗ smoke-desk-play: missing sibling script (optional; not release)" >&2; \
		exit 2; \
	fi
	@echo "  SMOKE   DESK-4 play/input (DESK_PLAY_OK)..."
	@IR0_ROOT="$(KERNEL_ROOT)" bash "$(IR0_DESKTOP_ROOT)/smoke/run-desk-classicube-smoke.sh" --play
	@echo "✓ smoke-desk-play passed (DESK-4)"

smoke-desk: smoke-desk-xfbdev smoke-desk-wm smoke-desk-session smoke-desk-classicube smoke-desk-play
	@echo "✓ smoke-desk battery passed (DESK-1..4 soft + X session; optional / not release)"

ktm: ktm-check

.PHONY: ktm-run ktm-userdev-run ktm-userdev-cow-run ktm-userdev-fork-storm-run \
	ktm-userdev-fork-storm-virtfs-run ktm-userdev-fork-storm-runit-run \
	ktm-userdev-exec-drain-run ktm-userdev-exec-drain-virtfs-run ktm-userdev-exec-drain-runit-run \
	ktm-userdev-session-stress-run build-ktm-session-stress-case \
	ktm-userdev-reap-drain-run ktm-userdev-reap-drain-virtfs-run ktm-userdev-reap-drain-runit-run \
	ktm-userdev-init-exit-drain-run ktm-userdev-init-exit-drain-virtfs-run \
	ktm-userdev-posix-pseudofs-run ktm-userdev-posix-pseudofs-virtfs-run ktm-userdev-posix-pseudofs-runit-run \
	ktm-userdev-input-det-run ktm-userdev-input-det-virtfs-run ktm-userdev-input-det-runit-run \
	smoke-ktm-drains-runit \
	ktm-userdev-nic-reach-run ktm-userdev-nic-reach-virtfs-run smoke-nic-reach \
	ktm-userdev-tcp-guest-run ktm-userdev-tcp-guest-virtfs-run smoke-tcp-guest \
	ktm-userdev-tcp-wire-run ktm-userdev-tcp-wire-virtfs-run smoke-tcp-wire \
	ktm-userdev-fault-pipe-run smoke-ktm-fault build-ktm-fault-pipe-case build-ktm-fault-exec-case \
	ktm-userdev-fault-class-b-run smoke-class-b-mitigated smoke-class-b-repro \
	build-ktm-fault-class-b-case \
	ktm-userdev-epoll-run ktm-userdev-epoll-runit-run build-ktm-epoll-case smoke-epoll-basic \
	ktm-userdev-socketpair-runit-run ktm-userdev-fb-map-shared-runit-run \
	ktm-userdev-scm-rights-runit-run ktm-userdev-unix-abstract-runit-run \
	ktm-userdev-sysv-shm-runit-run ktm-userdev-memfd-shared-runit-run \
	ktm-userdev-unix-harden-runit-run ktm-userdev-unix-flags-runit-run \
	ktm-userdev-event-fds-runit-run ktm-userdev-posix-shm-runit-run \
	ktm-userdev-stream-sock-run build-ktm-stream-sock-case smoke-stream-sock \
	ktm-userdev-socketpair-run build-ktm-socketpair-case smoke-socketpair \
	ktm-userdev-fb-map-shared-run build-ktm-fb-map-shared-case smoke-fb-map-shared \
	ktm-userdev-scm-rights-run build-ktm-scm-rights-case smoke-scm-rights \
	ktm-userdev-unix-abstract-run build-ktm-unix-abstract-case smoke-unix-abstract \
	ktm-userdev-sysv-shm-run build-ktm-sysv-shm-case smoke-sysv-shm \
	ktm-userdev-memfd-shared-run build-ktm-memfd-shared-case smoke-memfd-shared \
	ktm-userdev-unix-harden-run build-ktm-unix-harden-case smoke-unix-harden \
	ktm-userdev-unix-flags-run build-ktm-unix-flags-case smoke-unix-flags \
	ktm-userdev-event-fds-run build-ktm-event-fds-case smoke-event-fds \
	ktm-userdev-posix-shm-run build-ktm-posix-shm-case smoke-posix-shm \
	ktm-userdev-busybox-manifest-run smoke-busybox-manifest \
	ktm-userdev-doom-55d-run ktm-userdev-tcc-power-halt-run \
	ktm-userdev-x11-evdev-fb-run build-ktm-x11-evdev-fb-case smoke-x11-evdev-fb \
	smoke-t3-prep \
	build-ktm-tcp-wire-case \
	build-ktm-fork-wait-case build-ktm-cow-touch-case build-ktm-fork-storm-case \
	build-ktm-exec-drain-case build-ktm-reap-drain-case \
	build-ktm-init-exit-drain-case \
	build-ktm-posix-pseudofs-case build-ktm-input-det-case build-ktm-nic-reach-case \
	build-ktm-tcp-guest-case
ktm-run: kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@python3 scripts/ktm_runner.py --scenario $(or $(SCENARIO),process.lifecycle) \
		--log /tmp/ktm-run.log --timeout 60

KTM_TESTS_DIR = tests/ktm
KTM_USERDEV_LIB = $(KTM_TESTS_DIR)/lib
KTM_USERDEV_DIR = $(KTM_TESTS_DIR)/userdev
# -idirafter includes: musl stdint/sys win; still finds <ir0/...> uapi (avoid -Iincludes shadow).
KTM_USERDEV_MUSL_FLAGS = -static -Os -I$(KTM_USERDEV_LIB) -idirafter includes
KTM_USERDEV_LIB_SRC = $(KTM_USERDEV_LIB)/libktm_user.c

KTM_FORK_WAIT_SRC = $(KTM_USERDEV_DIR)/ktm_fork_wait_case.c $(KTM_USERDEV_LIB_SRC)
KTM_FORK_WAIT_BIN = $(KTM_USERDEV_DIR)/ktm_fork_wait_case
KTM_COW_TOUCH_SRC = $(KTM_USERDEV_DIR)/ktm_cow_touch_case.c $(KTM_USERDEV_LIB_SRC)
KTM_COW_TOUCH_BIN = $(KTM_USERDEV_DIR)/ktm_cow_touch_case
KTM_FORK_STORM_SRC = $(KTM_USERDEV_DIR)/ktm_fork_storm_case.c $(KTM_USERDEV_LIB_SRC)
KTM_FORK_STORM_BIN = $(KTM_USERDEV_DIR)/ktm_fork_storm_case
KTM_EXEC_DRAIN_SRC = $(KTM_USERDEV_DIR)/ktm_exec_drain_case.c $(KTM_USERDEV_LIB_SRC)
KTM_EXEC_DRAIN_BIN = $(KTM_USERDEV_DIR)/ktm_exec_drain_case
KTM_SESSION_STRESS_SRC = $(KTM_USERDEV_DIR)/ktm_session_stress_case.c $(KTM_USERDEV_LIB_SRC)
KTM_SESSION_STRESS_BIN = $(KTM_USERDEV_DIR)/ktm_session_stress_case
# Kernel PROFILE defaults to desktop (kernel defconfig). ISD session disk is
# development BusyBox+runit unless the caller sets KTM_SESSION_ISD_PROFILE.
KTM_SESSION_ISD_PROFILE ?= development
KTM_REAP_DRAIN_SRC = $(KTM_USERDEV_DIR)/ktm_reap_drain_case.c $(KTM_USERDEV_LIB_SRC)
KTM_REAP_DRAIN_BIN = $(KTM_USERDEV_DIR)/ktm_reap_drain_case
KTM_INIT_EXIT_DRAIN_SRC = $(KTM_USERDEV_DIR)/ktm_init_exit_drain_case.c $(KTM_USERDEV_LIB_SRC)
KTM_INIT_EXIT_DRAIN_BIN = $(KTM_USERDEV_DIR)/ktm_init_exit_drain_case
KTM_POSIX_PSEUDOFS_SRC = $(KTM_USERDEV_DIR)/ktm_posix_pseudofs_case.c $(KTM_USERDEV_LIB_SRC)
KTM_POSIX_PSEUDOFS_BIN = $(KTM_USERDEV_DIR)/ktm_posix_pseudofs_case
KTM_INPUT_DET_SRC = $(KTM_USERDEV_DIR)/ktm_input_det_case.c $(KTM_USERDEV_LIB_SRC)
KTM_INPUT_DET_BIN = $(KTM_USERDEV_DIR)/ktm_input_det_case
KTM_NIC_REACH_SRC = $(KTM_USERDEV_DIR)/ktm_nic_reach_case.c $(KTM_USERDEV_LIB_SRC)
KTM_NIC_REACH_BIN = $(KTM_USERDEV_DIR)/ktm_nic_reach_case
KTM_TCP_GUEST_SRC = $(KTM_USERDEV_DIR)/ktm_tcp_guest_case.c $(KTM_USERDEV_LIB_SRC)
KTM_TCP_GUEST_BIN = $(KTM_USERDEV_DIR)/ktm_tcp_guest_case
KTM_TCP_WIRE_SRC = $(KTM_USERDEV_DIR)/ktm_tcp_wire_case.c $(KTM_USERDEV_LIB_SRC)
KTM_TCP_WIRE_BIN = $(KTM_USERDEV_DIR)/ktm_tcp_wire_case
KTM_TCP_PEER_CC_SRC = $(KTM_USERDEV_DIR)/ktm_tcp_peer_cc_case.c $(KTM_USERDEV_LIB_SRC)
KTM_TCP_PEER_CC_BIN = $(KTM_USERDEV_DIR)/ktm_tcp_peer_cc_case
KTM_FAULT_PIPE_SRC = $(KTM_USERDEV_DIR)/ktm_fault_pipe_case.c $(KTM_USERDEV_LIB_SRC)
KTM_FAULT_PIPE_BIN = $(KTM_USERDEV_DIR)/ktm_fault_pipe_case
KTM_FAULT_EXEC_SRC = $(KTM_USERDEV_DIR)/ktm_fault_exec_case.c $(KTM_USERDEV_LIB_SRC)
KTM_FAULT_EXEC_BIN = $(KTM_USERDEV_DIR)/ktm_fault_exec_case
KTM_FAULT_CLASS_B_SRC = $(KTM_USERDEV_DIR)/ktm_fault_class_b_case.c $(KTM_USERDEV_LIB_SRC)
KTM_FAULT_CLASS_B_BIN = $(KTM_USERDEV_DIR)/ktm_fault_class_b_case
KTM_EPOLL_SRC = $(KTM_USERDEV_DIR)/ktm_epoll_case.c $(KTM_USERDEV_LIB_SRC)
KTM_EPOLL_BIN = $(KTM_USERDEV_DIR)/ktm_epoll_case
KTM_STREAM_SOCK_SRC = $(KTM_USERDEV_DIR)/ktm_stream_sock_case.c $(KTM_USERDEV_LIB_SRC)
KTM_STREAM_SOCK_BIN = $(KTM_USERDEV_DIR)/ktm_stream_sock_case
KTM_SOCKETPAIR_SRC = $(KTM_USERDEV_DIR)/ktm_socketpair_case.c $(KTM_USERDEV_LIB_SRC)
KTM_SOCKETPAIR_BIN = $(KTM_USERDEV_DIR)/ktm_socketpair_case
KTM_FB_MAP_SHARED_SRC = $(KTM_USERDEV_DIR)/ktm_fb_map_shared_case.c $(KTM_USERDEV_LIB_SRC)
KTM_FB_MAP_SHARED_BIN = $(KTM_USERDEV_DIR)/ktm_fb_map_shared_case
KTM_X11_EVDEV_FB_SRC = $(KTM_USERDEV_DIR)/ktm_x11_evdev_fb_case.c $(KTM_USERDEV_LIB_SRC)
KTM_X11_EVDEV_FB_BIN = $(KTM_USERDEV_DIR)/ktm_x11_evdev_fb_case
KTM_SCM_RIGHTS_SRC = $(KTM_USERDEV_DIR)/ktm_scm_rights_case.c $(KTM_USERDEV_LIB_SRC)
KTM_SCM_RIGHTS_BIN = $(KTM_USERDEV_DIR)/ktm_scm_rights_case
KTM_UNIX_ABSTRACT_SRC = $(KTM_USERDEV_DIR)/ktm_unix_abstract_case.c $(KTM_USERDEV_LIB_SRC)
KTM_UNIX_ABSTRACT_BIN = $(KTM_USERDEV_DIR)/ktm_unix_abstract_case
KTM_SYSV_SHM_SRC = $(KTM_USERDEV_DIR)/ktm_sysv_shm_case.c $(KTM_USERDEV_LIB_SRC)
KTM_SYSV_SHM_BIN = $(KTM_USERDEV_DIR)/ktm_sysv_shm_case
KTM_MEMFD_SHARED_SRC = $(KTM_USERDEV_DIR)/ktm_memfd_shared_case.c $(KTM_USERDEV_LIB_SRC)
KTM_MEMFD_SHARED_BIN = $(KTM_USERDEV_DIR)/ktm_memfd_shared_case
KTM_UNIX_HARDEN_SRC = $(KTM_USERDEV_DIR)/ktm_unix_harden_case.c $(KTM_USERDEV_LIB_SRC)
KTM_UNIX_HARDEN_BIN = $(KTM_USERDEV_DIR)/ktm_unix_harden_case
KTM_UNIX_FLAGS_SRC = $(KTM_USERDEV_DIR)/ktm_unix_flags_case.c $(KTM_USERDEV_LIB_SRC)
KTM_UNIX_FLAGS_BIN = $(KTM_USERDEV_DIR)/ktm_unix_flags_case
KTM_EVENT_FDS_SRC = $(KTM_USERDEV_DIR)/ktm_event_fds_case.c $(KTM_USERDEV_LIB_SRC)
KTM_EVENT_FDS_BIN = $(KTM_USERDEV_DIR)/ktm_event_fds_case
KTM_POSIX_SHM_SRC = $(KTM_USERDEV_DIR)/ktm_posix_shm_case.c $(KTM_USERDEV_LIB_SRC)
KTM_POSIX_SHM_BIN = $(KTM_USERDEV_DIR)/ktm_posix_shm_case

# All userdev pilots run via hostshare stub + share payload (virtio-9p).
build-ktm-fork-wait-case build-ktm-cow-touch-case build-ktm-fork-storm-case \
	build-ktm-exec-drain-case build-ktm-reap-drain-case \
	build-ktm-init-exit-drain-case build-ktm-posix-pseudofs-case \
	build-ktm-input-det-case build-ktm-nic-reach-case \
	build-ktm-tcp-guest-case build-ktm-tcp-wire-case \
	build-ktm-fault-pipe-case build-ktm-fault-class-b-case build-ktm-epoll-case build-ktm-stream-sock-case \
	build-ktm-socketpair-case build-ktm-fb-map-shared-case \
	build-ktm-scm-rights-case build-ktm-unix-abstract-case build-ktm-sysv-shm-case \
	build-ktm-memfd-shared-case build-ktm-unix-harden-case \
	build-ktm-unix-flags-case build-ktm-event-fds-case build-ktm-posix-shm-case: \
	build-init-hostshare-exec

build-ktm-fork-wait-case:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  KTM     Building fork_wait_signal pilot ($(KTM_FORK_WAIT_BIN))"
	@$(MUSL_CC) $(KTM_USERDEV_MUSL_FLAGS) \
		-o $(KTM_FORK_WAIT_BIN) $(KTM_FORK_WAIT_SRC)
	@file $(KTM_FORK_WAIT_BIN) | grep -q ELF
	@echo "✓ build-ktm-fork-wait-case OK"

build-ktm-cow-touch-case:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  KTM     Building cow_touch pilot ($(KTM_COW_TOUCH_BIN))"
	@$(MUSL_CC) $(KTM_USERDEV_MUSL_FLAGS) \
		-o $(KTM_COW_TOUCH_BIN) $(KTM_COW_TOUCH_SRC)
	@file $(KTM_COW_TOUCH_BIN) | grep -q ELF
	@echo "✓ build-ktm-cow-touch-case OK"

build-ktm-fork-storm-case:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  KTM     Building fork_exit_storm depth ($(KTM_FORK_STORM_BIN))"
	@$(MUSL_CC) $(KTM_USERDEV_MUSL_FLAGS) \
		-o $(KTM_FORK_STORM_BIN) $(KTM_FORK_STORM_SRC)
	@file $(KTM_FORK_STORM_BIN) | grep -q ELF
	@echo "✓ build-ktm-fork-storm-case OK"

build-ktm-fault-pipe-case:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  KTM     Building fault_pipe pilot ($(KTM_FAULT_PIPE_BIN))"
	@$(MUSL_CC) $(KTM_USERDEV_MUSL_FLAGS) \
		-o $(KTM_FAULT_PIPE_BIN) $(KTM_FAULT_PIPE_SRC)
	@file $(KTM_FAULT_PIPE_BIN) | grep -q ELF
	@echo "✓ build-ktm-fault-pipe-case OK"

build-ktm-fault-exec-case:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  KTM     Building fault_exec pilot ($(KTM_FAULT_EXEC_BIN))"
	@$(MUSL_CC) $(KTM_USERDEV_MUSL_FLAGS) \
		-o $(KTM_FAULT_EXEC_BIN) $(KTM_FAULT_EXEC_SRC)
	@file $(KTM_FAULT_EXEC_BIN) | grep -q ELF
	@echo "✓ build-ktm-fault-exec-case OK"

build-ktm-epoll-case:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  KTM     Building epoll_basic pilot ($(KTM_EPOLL_BIN))"
	@$(MUSL_CC) $(KTM_USERDEV_MUSL_FLAGS) \
		-o $(KTM_EPOLL_BIN) $(KTM_EPOLL_SRC)
	@file $(KTM_EPOLL_BIN) | grep -q ELF
	@echo "✓ build-ktm-epoll-case OK"

build-ktm-stream-sock-case:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  KTM     Building stream_sock pilot ($(KTM_STREAM_SOCK_BIN))"
	@$(MUSL_CC) $(KTM_USERDEV_MUSL_FLAGS) \
		-o $(KTM_STREAM_SOCK_BIN) $(KTM_STREAM_SOCK_SRC)
	@file $(KTM_STREAM_SOCK_BIN) | grep -q ELF
	@echo "✓ build-ktm-stream-sock-case OK"

build-ktm-socketpair-case:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  KTM     Building socketpair pilot ($(KTM_SOCKETPAIR_BIN))"
	@$(MUSL_CC) $(KTM_USERDEV_MUSL_FLAGS) \
		-o $(KTM_SOCKETPAIR_BIN) $(KTM_SOCKETPAIR_SRC)
	@file $(KTM_SOCKETPAIR_BIN) | grep -q ELF
	@echo "✓ build-ktm-socketpair-case OK"

build-ktm-fb-map-shared-case:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  KTM     Building fb_map_shared pilot ($(KTM_FB_MAP_SHARED_BIN))"
	@$(MUSL_CC) $(KTM_USERDEV_MUSL_FLAGS) \
		-o $(KTM_FB_MAP_SHARED_BIN) $(KTM_FB_MAP_SHARED_SRC)
	@file $(KTM_FB_MAP_SHARED_BIN) | grep -q ELF
	@echo "✓ build-ktm-fb-map-shared-case OK"

ktm-userdev-run: build-ktm-fork-wait-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@python3 scripts/ktm_userdev_runner.py --log /tmp/ktm-userdev-run.log --timeout 90

ktm-userdev-fault-pipe-run: build-ktm-fault-pipe-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_FAULT_PIPE_BIN) \
		--log /tmp/ktm-userdev-fault-pipe.log --timeout 90 \
		--done KTM_FAULT_PIPE_OK \
		--require 'TEST_END|fault_pipe|PASS' \
		--require KTM_FAULT_PIPE_OK \
		--require KTM_SNAPSHOT_DELTA_OK \
		--require KTM_FAULT_PIPE_ENOMEM_OK \
		--require KTM_USERDEV_OK
	@echo "✓ ktm-userdev-fault-pipe-run (pipe.create fault injection)"

.PHONY: ktm-userdev-fault-exec-run
ktm-userdev-fault-exec-run: build-ktm-fault-exec-case build-ktm-true-helper build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_FAULT_EXEC_BIN) \
		--inject $(KTM_TRUE_HELPER_BIN):bin/f41true \
		--log /tmp/ktm-userdev-fault-exec.log --timeout 90 \
		--done KTM_FAULT_EXEC_OK \
		--require 'TEST_END|fault_exec|PASS' \
		--require KTM_FAULT_EXEC_OK \
		--require KTM_FAULT_EXEC_PRECOMMIT_OK \
		--require KTM_FAULT_EXEC_POSTCOMMIT_OK \
		--require KTM_USERDEV_OK
	@echo "✓ ktm-userdev-fault-exec-run (execve fault injection)"

smoke-ktm-fault: ktm-userdev-fault-pipe-run ktm-userdev-fault-exec-run

ktm-userdev-epoll-run: build-ktm-epoll-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_EPOLL_BIN) \
		--log /tmp/ktm-userdev-epoll.log --timeout 90 \
		--done KTM_EPOLL_OK \
		--require KTM_EPOLL_OK \
		--require EPOLL_BASIC_OK \
		--require KTM_USERDEV_OK
	@echo "✓ ktm-userdev-epoll-run (epoll create/ctl/wait)"

# Canonical plane is KTM; keep smoke name as alias for CI muscle memory.
ktm-userdev-epoll-runit-run: build-ktm-epoll-case build-runit kernel-x64-userspace.iso
	@echo "  SMOKE   epoll-basic under runit PID1..."
	@chmod +x scripts/ktm_userdev_runit_run.sh; \
	scripts/ktm_userdev_runit_run.sh \
		--init $(KTM_EPOLL_BIN) \
		--log /tmp/ktm-userdev-epoll-runit-run.log \
		--done KTM_EPOLL_OK \
		--require KTM_EPOLL_OK \
		--require EPOLL_BASIC_OK \
		--require KTM_USERDEV_OK
	@echo "✓ ktm-userdev-epoll-runit-run (runit PID1 + 9p)"

smoke-epoll-basic: ktm-userdev-epoll-runit-run
	@echo "✓ smoke-epoll-basic (alias → ktm-userdev-epoll-runit-run)"


ktm-userdev-stream-sock-run: build-ktm-stream-sock-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_STREAM_SOCK_BIN) \
		--log /tmp/ktm-userdev-stream-sock.log --timeout 90 \
		--done KTM_STREAM_SOCK_OK \
		--require KTM_STREAM_SOCK_OK \
		--require STREAM_SOCK_OK \
		--require STREAM_SENDRECV_OK \
		--require KTM_USERDEV_OK
	@echo "✓ ktm-userdev-stream-sock-run (stub PID1 + 9p payload)"

# Product-shaped: runit PID1 + hostshare payload service.
ktm-userdev-stream-sock-runit-run: build-ktm-stream-sock-case build-runit kernel-x64-userspace.iso
	@echo "  SMOKE   stream-sock under runit PID1..."
	@chmod +x scripts/ktm_userdev_runit_run.sh; \
	scripts/ktm_userdev_runit_run.sh \
		--init $(KTM_STREAM_SOCK_BIN) \
		--log /tmp/ktm-userdev-stream-sock-runit.log \
		--done KTM_STREAM_SOCK_OK \
		--require KTM_STREAM_SOCK_OK \
		--require STREAM_SOCK_OK \
		--require STREAM_SENDRECV_OK \
		--require KTM_USERDEV_OK
	@echo "✓ ktm-userdev-stream-sock-runit-run (runit PID1 + 9p)"

smoke-stream-sock: ktm-userdev-stream-sock-runit-run
	@echo "✓ smoke-stream-sock (alias → ktm-userdev-stream-sock-runit-run)"

ktm-userdev-socketpair-run: build-ktm-socketpair-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_SOCKETPAIR_BIN) \
		--log /tmp/ktm-userdev-socketpair.log --timeout 90 \
		--done KTM_SOCKETPAIR_OK \
		--require KTM_SOCKETPAIR_OK \
		--require SOCKETPAIR_OK \
		--require KTM_USERDEV_OK
	@echo "✓ ktm-userdev-socketpair-run (AF_UNIX SOCK_STREAM socketpair)"

ktm-userdev-socketpair-runit-run: build-ktm-socketpair-case build-runit kernel-x64-userspace.iso
	@echo "  SMOKE   socketpair under runit PID1..."
	@chmod +x scripts/ktm_userdev_runit_run.sh; \
	scripts/ktm_userdev_runit_run.sh \
		--init $(KTM_SOCKETPAIR_BIN) \
		--log /tmp/ktm-userdev-socketpair-runit-run.log \
		--done KTM_SOCKETPAIR_OK \
		--require KTM_SOCKETPAIR_OK \
		--require SOCKETPAIR_OK \
		--require KTM_USERDEV_OK
	@echo "✓ ktm-userdev-socketpair-runit-run (runit PID1 + 9p)"

smoke-socketpair: ktm-userdev-socketpair-runit-run
	@echo "✓ smoke-socketpair (alias → ktm-userdev-socketpair-runit-run)"


ktm-userdev-fb-map-shared-run: build-ktm-fb-map-shared-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_FB_MAP_SHARED_BIN) \
		--log /tmp/ktm-userdev-fb-map-shared.log --timeout 90 \
		--done KTM_FB_MAP_SHARED_OK \
		--require KTM_FB_MAP_SHARED_OK \
		--require FB_MAP_SHARED_OK \
		--require FB_MAP_SHARED_USER_OK \
		--require KTM_USERDEV_OK
	@echo "✓ ktm-userdev-fb-map-shared-run (MAP_SHARED /dev/fb0)"

ktm-userdev-fb-map-shared-runit-run: build-ktm-fb-map-shared-case build-runit kernel-x64-userspace.iso
	@echo "  SMOKE   fb-map-shared under runit PID1..."
	@chmod +x scripts/ktm_userdev_runit_run.sh; \
	scripts/ktm_userdev_runit_run.sh \
		--init $(KTM_FB_MAP_SHARED_BIN) \
		--log /tmp/ktm-userdev-fb-map-shared-runit-run.log \
		--done KTM_FB_MAP_SHARED_OK \
		--require KTM_FB_MAP_SHARED_OK \
		--require FB_MAP_SHARED_OK \
		--require FB_MAP_SHARED_USER_OK \
		--require KTM_USERDEV_OK
	@echo "✓ ktm-userdev-fb-map-shared-runit-run (runit PID1 + 9p)"

smoke-fb-map-shared: ktm-userdev-fb-map-shared-runit-run
	@echo "✓ smoke-fb-map-shared (alias → ktm-userdev-fb-map-shared-runit-run)"


build-ktm-x11-evdev-fb-case:
	@if [ -z "$(MUSL_CC)" ]; then echo "✗ musl cross compiler not found"; exit 1; fi
	@echo "  KTM     Building x11_evdev_fb pilot ($(KTM_X11_EVDEV_FB_BIN))"
	@$(MUSL_CC) $(KTM_USERDEV_MUSL_FLAGS) -o $(KTM_X11_EVDEV_FB_BIN) $(KTM_X11_EVDEV_FB_SRC)
	@file $(KTM_X11_EVDEV_FB_BIN) | grep -q ELF
	@echo "✓ build-ktm-x11-evdev-fb-case OK"

ktm-userdev-x11-evdev-fb-run: build-ktm-x11-evdev-fb-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_X11_EVDEV_FB_BIN) \
		--log /tmp/ktm-userdev-x11-evdev-fb.log --timeout 90 \
		--done KTM_X11_EVDEV_FB_OK \
		--require KTM_X11_EVDEV_FB_OK \
		--require FB_BITFIELD_OK \
		--require EVIOCGVERSION_OK \
		--require EVIOCGBIT_OK \
		--require SYN_REPORT_OK \
		--require INPUT_EVENT0_PATH_OK \
		--require KTM_USERDEV_OK
	@echo "✓ ktm-userdev-x11-evdev-fb-run"

smoke-x11-evdev-fb: ktm-userdev-x11-evdev-fb-run
	@echo "✓ smoke-x11-evdev-fb (alias → ktm-userdev-x11-evdev-fb-run)"

build-ktm-scm-rights-case:
	@if [ -z "$(MUSL_CC)" ]; then echo "✗ musl cross compiler not found"; exit 1; fi
	@echo "  KTM     Building scm_rights pilot ($(KTM_SCM_RIGHTS_BIN))"
	@$(MUSL_CC) $(KTM_USERDEV_MUSL_FLAGS) -o $(KTM_SCM_RIGHTS_BIN) $(KTM_SCM_RIGHTS_SRC)
	@file $(KTM_SCM_RIGHTS_BIN) | grep -q ELF
	@echo "✓ build-ktm-scm-rights-case OK"

ktm-userdev-scm-rights-run: build-ktm-scm-rights-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_SCM_RIGHTS_BIN) \
		--log /tmp/ktm-userdev-scm-rights.log --timeout 90 \
		--done KTM_SCM_RIGHTS_OK \
		--require KTM_SCM_RIGHTS_OK --require SCM_RIGHTS_OK --require KTM_USERDEV_OK
	@echo "✓ ktm-userdev-scm-rights-run"

ktm-userdev-scm-rights-runit-run: build-ktm-scm-rights-case build-runit kernel-x64-userspace.iso
	@echo "  SMOKE   scm-rights under runit PID1..."
	@chmod +x scripts/ktm_userdev_runit_run.sh; \
	scripts/ktm_userdev_runit_run.sh \
		--init $(KTM_SCM_RIGHTS_BIN) \
		--log /tmp/ktm-userdev-scm-rights-runit-run.log \
		--done KTM_SCM_RIGHTS_OK \
		--require KTM_SCM_RIGHTS_OK \
		--require SCM_RIGHTS_OK \
		--require KTM_USERDEV_OK
	@echo "✓ ktm-userdev-scm-rights-runit-run (runit PID1 + 9p)"

smoke-scm-rights: ktm-userdev-scm-rights-runit-run
	@echo "✓ smoke-scm-rights (alias → ktm-userdev-scm-rights-runit-run)"


build-ktm-unix-abstract-case:
	@if [ -z "$(MUSL_CC)" ]; then echo "✗ musl cross compiler not found"; exit 1; fi
	@echo "  KTM     Building unix_abstract pilot ($(KTM_UNIX_ABSTRACT_BIN))"
	@$(MUSL_CC) $(KTM_USERDEV_MUSL_FLAGS) -o $(KTM_UNIX_ABSTRACT_BIN) $(KTM_UNIX_ABSTRACT_SRC)
	@file $(KTM_UNIX_ABSTRACT_BIN) | grep -q ELF
	@echo "✓ build-ktm-unix-abstract-case OK"

ktm-userdev-unix-abstract-run: build-ktm-unix-abstract-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_UNIX_ABSTRACT_BIN) \
		--log /tmp/ktm-userdev-unix-abstract.log --timeout 90 \
		--done KTM_UNIX_ABSTRACT_OK \
		--require KTM_UNIX_ABSTRACT_OK --require UNIX_ABSTRACT_OK --require SOCK_POLL_OK --require KTM_USERDEV_OK
	@echo "✓ ktm-userdev-unix-abstract-run"

ktm-userdev-unix-abstract-runit-run: build-ktm-unix-abstract-case build-runit kernel-x64-userspace.iso
	@echo "  SMOKE   unix-abstract under runit PID1..."
	@chmod +x scripts/ktm_userdev_runit_run.sh; \
	scripts/ktm_userdev_runit_run.sh \
		--init $(KTM_UNIX_ABSTRACT_BIN) \
		--log /tmp/ktm-userdev-unix-abstract-runit-run.log \
		--done KTM_UNIX_ABSTRACT_OK \
		--require KTM_UNIX_ABSTRACT_OK \
		--require UNIX_ABSTRACT_OK \
		--require SOCK_POLL_OK \
		--require KTM_USERDEV_OK
	@echo "✓ ktm-userdev-unix-abstract-runit-run (runit PID1 + 9p)"

smoke-unix-abstract: ktm-userdev-unix-abstract-runit-run
	@echo "✓ smoke-unix-abstract (alias → ktm-userdev-unix-abstract-runit-run)"


build-ktm-sysv-shm-case:
	@if [ -z "$(MUSL_CC)" ]; then echo "✗ musl cross compiler not found"; exit 1; fi
	@echo "  KTM     Building sysv_shm pilot ($(KTM_SYSV_SHM_BIN))"
	@$(MUSL_CC) $(KTM_USERDEV_MUSL_FLAGS) -o $(KTM_SYSV_SHM_BIN) $(KTM_SYSV_SHM_SRC)
	@file $(KTM_SYSV_SHM_BIN) | grep -q ELF
	@echo "✓ build-ktm-sysv-shm-case OK"

ktm-userdev-sysv-shm-run: build-ktm-sysv-shm-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_SYSV_SHM_BIN) \
		--log /tmp/ktm-userdev-sysv-shm.log --timeout 90 \
		--done KTM_SYSV_SHM_OK \
		--require KTM_SYSV_SHM_OK --require SYSV_SHM_OK --require KTM_USERDEV_OK
	@echo "✓ ktm-userdev-sysv-shm-run"

ktm-userdev-sysv-shm-runit-run: build-ktm-sysv-shm-case build-runit kernel-x64-userspace.iso
	@echo "  SMOKE   sysv-shm under runit PID1..."
	@chmod +x scripts/ktm_userdev_runit_run.sh; \
	scripts/ktm_userdev_runit_run.sh \
		--init $(KTM_SYSV_SHM_BIN) \
		--log /tmp/ktm-userdev-sysv-shm-runit-run.log \
		--done KTM_SYSV_SHM_OK \
		--require KTM_SYSV_SHM_OK \
		--require SYSV_SHM_OK \
		--require KTM_USERDEV_OK
	@echo "✓ ktm-userdev-sysv-shm-runit-run (runit PID1 + 9p)"

smoke-sysv-shm: ktm-userdev-sysv-shm-runit-run
	@echo "✓ smoke-sysv-shm (alias → ktm-userdev-sysv-shm-runit-run)"


build-ktm-memfd-shared-case:
	@if [ -z "$(MUSL_CC)" ]; then echo "✗ musl cross compiler not found"; exit 1; fi
	@echo "  KTM     Building memfd_shared pilot ($(KTM_MEMFD_SHARED_BIN))"
	@$(MUSL_CC) $(KTM_USERDEV_MUSL_FLAGS) -o $(KTM_MEMFD_SHARED_BIN) $(KTM_MEMFD_SHARED_SRC)
	@file $(KTM_MEMFD_SHARED_BIN) | grep -q ELF
	@echo "✓ build-ktm-memfd-shared-case OK"

ktm-userdev-memfd-shared-run: build-ktm-memfd-shared-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_MEMFD_SHARED_BIN) \
		--log /tmp/ktm-userdev-memfd-shared.log --timeout 90 \
		--done MEMFD_SHARED_OK \
		--require KTM_MEMFD_SHARED_OK --require MEMFD_SHARED_OK --require KTM_USERDEV_OK
	@echo "✓ ktm-userdev-memfd-shared-run"

ktm-userdev-memfd-shared-runit-run: build-ktm-memfd-shared-case build-runit kernel-x64-userspace.iso
	@echo "  SMOKE   memfd-shared under runit PID1..."
	@chmod +x scripts/ktm_userdev_runit_run.sh; \
	scripts/ktm_userdev_runit_run.sh \
		--init $(KTM_MEMFD_SHARED_BIN) \
		--log /tmp/ktm-userdev-memfd-shared-runit-run.log \
		--done KTM_USERDEV_OK \
		--require KTM_MEMFD_SHARED_OK \
		--require MEMFD_SHARED_OK \
		--require KTM_USERDEV_OK
	@echo "✓ ktm-userdev-memfd-shared-runit-run (runit PID1 + 9p)"

smoke-memfd-shared: ktm-userdev-memfd-shared-runit-run
	@echo "✓ smoke-memfd-shared (alias → ktm-userdev-memfd-shared-runit-run)"


build-ktm-unix-harden-case:
	@if [ -z "$(MUSL_CC)" ]; then echo "✗ musl cross compiler not found"; exit 1; fi
	@echo "  KTM     Building unix_harden pilot ($(KTM_UNIX_HARDEN_BIN))"
	@$(MUSL_CC) $(KTM_USERDEV_MUSL_FLAGS) -o $(KTM_UNIX_HARDEN_BIN) $(KTM_UNIX_HARDEN_SRC)
	@file $(KTM_UNIX_HARDEN_BIN) | grep -q ELF
	@echo "✓ build-ktm-unix-harden-case OK"

ktm-userdev-unix-harden-run: build-ktm-unix-harden-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_UNIX_HARDEN_BIN) \
		--log /tmp/ktm-userdev-unix-harden.log --timeout 90 \
		--done KTM_UNIX_HARDEN_OK \
		--require KTM_UNIX_HARDEN_OK --require GETPEERNAME_OK --require SCM_MULTI_OK --require KTM_USERDEV_OK
	@echo "✓ ktm-userdev-unix-harden-run"

ktm-userdev-unix-harden-runit-run: build-ktm-unix-harden-case build-runit kernel-x64-userspace.iso
	@echo "  SMOKE   unix-harden under runit PID1..."
	@chmod +x scripts/ktm_userdev_runit_run.sh; \
	scripts/ktm_userdev_runit_run.sh \
		--init $(KTM_UNIX_HARDEN_BIN) \
		--log /tmp/ktm-userdev-unix-harden-runit-run.log \
		--done KTM_UNIX_HARDEN_OK \
		--require KTM_UNIX_HARDEN_OK \
		--require GETPEERNAME_OK \
		--require SCM_MULTI_OK \
		--require KTM_USERDEV_OK
	@echo "✓ ktm-userdev-unix-harden-runit-run (runit PID1 + 9p)"

smoke-unix-harden: ktm-userdev-unix-harden-runit-run
	@echo "✓ smoke-unix-harden (alias → ktm-userdev-unix-harden-runit-run)"


build-ktm-unix-flags-case:
	@if [ -z "$(MUSL_CC)" ]; then echo "✗ musl cross compiler not found"; exit 1; fi
	@echo "  KTM     Building unix_flags pilot ($(KTM_UNIX_FLAGS_BIN))"
	@$(MUSL_CC) $(KTM_USERDEV_MUSL_FLAGS) -o $(KTM_UNIX_FLAGS_BIN) $(KTM_UNIX_FLAGS_SRC)
	@file $(KTM_UNIX_FLAGS_BIN) | grep -q ELF
	@echo "✓ build-ktm-unix-flags-case OK"

ktm-userdev-unix-flags-run: build-ktm-unix-flags-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_UNIX_FLAGS_BIN) \
		--log /tmp/ktm-userdev-unix-flags.log --timeout 90 \
		--done KTM_USERDEV_OK \
		--require UNIX_FLAGS_OK --require ACCEPT4_OK --require MSG_PEEK_OK --require SO_REUSEADDR_OK --require KTM_USERDEV_OK
	@echo "✓ ktm-userdev-unix-flags-run"

ktm-userdev-unix-flags-runit-run: build-ktm-unix-flags-case build-runit kernel-x64-userspace.iso
	@echo "  SMOKE   unix-flags under runit PID1..."
	@chmod +x scripts/ktm_userdev_runit_run.sh; \
	scripts/ktm_userdev_runit_run.sh \
		--init $(KTM_UNIX_FLAGS_BIN) \
		--log /tmp/ktm-userdev-unix-flags-runit-run.log \
		--done KTM_USERDEV_OK \
		--require UNIX_FLAGS_OK \
		--require ACCEPT4_OK \
		--require MSG_PEEK_OK \
		--require SO_REUSEADDR_OK \
		--require KTM_USERDEV_OK
	@echo "✓ ktm-userdev-unix-flags-runit-run (runit PID1 + 9p)"

smoke-unix-flags: ktm-userdev-unix-flags-runit-run
	@echo "✓ smoke-unix-flags (alias → ktm-userdev-unix-flags-runit-run)"


build-ktm-event-fds-case:
	@if [ -z "$(MUSL_CC)" ]; then echo "✗ musl cross compiler not found"; exit 1; fi
	@echo "  KTM     Building event_fds pilot ($(KTM_EVENT_FDS_BIN))"
	@$(MUSL_CC) $(KTM_USERDEV_MUSL_FLAGS) -o $(KTM_EVENT_FDS_BIN) $(KTM_EVENT_FDS_SRC)
	@file $(KTM_EVENT_FDS_BIN) | grep -q ELF
	@echo "✓ build-ktm-event-fds-case OK"

ktm-userdev-event-fds-run: build-ktm-event-fds-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_EVENT_FDS_BIN) \
		--log /tmp/ktm-userdev-event-fds.log --timeout 90 \
		--done KTM_USERDEV_OK \
		--require EVENTFD_OK --require TIMERFD_OK --require KTM_USERDEV_OK
	@echo "✓ ktm-userdev-event-fds-run"

ktm-userdev-event-fds-runit-run: build-ktm-event-fds-case build-runit kernel-x64-userspace.iso
	@echo "  SMOKE   event-fds under runit PID1..."
	@chmod +x scripts/ktm_userdev_runit_run.sh; \
	scripts/ktm_userdev_runit_run.sh \
		--init $(KTM_EVENT_FDS_BIN) \
		--log /tmp/ktm-userdev-event-fds-runit-run.log \
		--done KTM_USERDEV_OK \
		--require EVENTFD_OK \
		--require TIMERFD_OK \
		--require KTM_USERDEV_OK
	@echo "✓ ktm-userdev-event-fds-runit-run (runit PID1 + 9p)"

smoke-event-fds: ktm-userdev-event-fds-runit-run
	@echo "✓ smoke-event-fds (alias → ktm-userdev-event-fds-runit-run)"


build-ktm-posix-shm-case:
	@if [ -z "$(MUSL_CC)" ]; then echo "✗ musl cross compiler not found"; exit 1; fi
	@echo "  KTM     Building posix_shm pilot ($(KTM_POSIX_SHM_BIN))"
	@$(MUSL_CC) $(KTM_USERDEV_MUSL_FLAGS) -o $(KTM_POSIX_SHM_BIN) $(KTM_POSIX_SHM_SRC)
	@file $(KTM_POSIX_SHM_BIN) | grep -q ELF
	@echo "✓ build-ktm-posix-shm-case OK"

ktm-userdev-posix-shm-run: build-ktm-posix-shm-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_POSIX_SHM_BIN) \
		--log /tmp/ktm-userdev-posix-shm.log --timeout 90 \
		--done KTM_USERDEV_OK \
		--require POSIX_SHM_OK --require KTM_USERDEV_OK
	@echo "✓ ktm-userdev-posix-shm-run"

ktm-userdev-posix-shm-runit-run: build-ktm-posix-shm-case build-runit kernel-x64-userspace.iso
	@echo "  SMOKE   posix-shm under runit PID1..."
	@chmod +x scripts/ktm_userdev_runit_run.sh; \
	scripts/ktm_userdev_runit_run.sh \
		--init $(KTM_POSIX_SHM_BIN) \
		--log /tmp/ktm-userdev-posix-shm-runit-run.log \
		--done KTM_USERDEV_OK \
		--require POSIX_SHM_OK \
		--require KTM_USERDEV_OK
	@echo "✓ ktm-userdev-posix-shm-runit-run (runit PID1 + 9p)"

smoke-posix-shm: ktm-userdev-posix-shm-runit-run
	@echo "✓ smoke-posix-shm (alias → ktm-userdev-posix-shm-runit-run)"


ktm-userdev-cow-run: build-ktm-cow-touch-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_COW_TOUCH_BIN) \
		--log /tmp/ktm-userdev-cow-run.log --timeout 90 \
		--done KTM_USERDEV_COW_OK \
		--require 'TEST_END|cow_touch|PASS' \
		--require KTM_USERDEV_COW_OK

ktm-userdev-fork-storm-run: build-ktm-fork-storm-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_FORK_STORM_BIN) \
		--log /tmp/ktm-userdev-fork-storm.log --timeout 240 \
		--done KTM_USERDEV_FORK_STORM_OK \
		--require 'TEST_END|fork_exit_storm|PASS' \
		--require KTM_USERDEV_FORK_STORM_OK
	@echo "✓ ktm-userdev-fork-storm-run (FASE42/44 depth ≥ legacy storms; share payload)"

# Same storm + virtio-9p: guest writes /mnt/host/ktm_fork_storm.txt visible on host.
.PHONY: ktm-userdev-fork-storm-virtfs-run
ktm-userdev-fork-storm-virtfs-run: build-ktm-fork-storm-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_FORK_STORM_BIN) \
		--log /tmp/ktm-userdev-fork-storm-virtfs.log --timeout 240 \
		--done KTM_USERDEV_FORK_STORM_OK \
		--require 'TEST_END|fork_exit_storm|PASS' \
		--require KTM_USERDEV_FORK_STORM_OK \
		--require KTM_HOSTSHARE_REPORT_OK \
		--host-file ktm_fork_storm.txt \
		--host-grep KTM_USERDEV_FORK_STORM_OK
	@echo "✓ ktm-userdev-fork-storm-virtfs-run (storm + virtio-9p host artifact)"

ktm-userdev-fork-storm-runit-run: build-ktm-fork-storm-case build-runit kernel-x64-userspace.iso
	@echo "  SMOKE   fork-storm under runit PID1..."
	@chmod +x scripts/ktm_userdev_runit_run.sh; \
	scripts/ktm_userdev_runit_run.sh \
		--init $(KTM_FORK_STORM_BIN) \
		--log /tmp/ktm-userdev-fork-storm-runit-run.log \
		--timeout 240 \
		--done KTM_USERDEV_FORK_STORM_OK \
		--require 'TEST_END|fork_exit_storm|PASS' \
		--require KTM_USERDEV_FORK_STORM_OK \
		--require KTM_HOSTSHARE_REPORT_OK \
		--host-file ktm_fork_storm.txt \
		--host-grep KTM_USERDEV_FORK_STORM_OK
	@echo "✓ ktm-userdev-fork-storm-runit-run (runit PID1 + 9p)"

build-ktm-exec-drain-case: build-ktm-true-helper
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  KTM     Building exec_drain (FASE44) ($(KTM_EXEC_DRAIN_BIN))"
	@$(MUSL_CC) $(KTM_USERDEV_MUSL_FLAGS) \
		-o $(KTM_EXEC_DRAIN_BIN) $(KTM_EXEC_DRAIN_SRC)
	@file $(KTM_EXEC_DRAIN_BIN) | grep -q ELF
	@echo "✓ build-ktm-exec-drain-case OK"

build-ktm-reap-drain-case:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  KTM     Building reap_drain (FASE44 init-exit analogue) ($(KTM_REAP_DRAIN_BIN))"
	@$(MUSL_CC) $(KTM_USERDEV_MUSL_FLAGS) \
		-o $(KTM_REAP_DRAIN_BIN) $(KTM_REAP_DRAIN_SRC)
	@file $(KTM_REAP_DRAIN_BIN) | grep -q ELF
	@echo "✓ build-ktm-reap-drain-case OK"

build-ktm-init-exit-drain-case:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  KTM     Building init_exit_drain (FASE44 PID1 _exit) ($(KTM_INIT_EXIT_DRAIN_BIN))"
	@$(MUSL_CC) $(KTM_USERDEV_MUSL_FLAGS) \
		-o $(KTM_INIT_EXIT_DRAIN_BIN) $(KTM_INIT_EXIT_DRAIN_SRC)
	@file $(KTM_INIT_EXIT_DRAIN_BIN) | grep -q ELF
	@echo "✓ build-ktm-init-exit-drain-case OK"

ktm-userdev-exec-drain-run: build-ktm-exec-drain-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_EXEC_DRAIN_BIN) \
		--inject $(KTM_TRUE_HELPER_BIN):bin/f41true \
		--log /tmp/ktm-userdev-exec-drain.log --timeout 360 \
		--done KTM_USERDEV_EXEC_DRAIN_OK \
		--require 'TEST_END|exec_drain|PASS' \
		--require KTM_USERDEV_EXEC_DRAIN_OK
	@echo "✓ ktm-userdev-exec-drain-run (FASE44 exec-drain → KTM; share payload)"

ktm-userdev-exec-drain-virtfs-run: build-ktm-exec-drain-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_EXEC_DRAIN_BIN) \
		--inject $(KTM_TRUE_HELPER_BIN):bin/f41true \
		--log /tmp/ktm-userdev-exec-drain-virtfs.log --timeout 360 \
		--done KTM_USERDEV_EXEC_DRAIN_OK \
		--require 'TEST_END|exec_drain|PASS' \
		--require KTM_USERDEV_EXEC_DRAIN_OK \
		--require KTM_HOSTSHARE_REPORT_OK \
		--host-file ktm_exec_drain.txt \
		--host-grep KTM_USERDEV_EXEC_DRAIN_OK
	@echo "✓ ktm-userdev-exec-drain-virtfs-run (exec-drain + virtio-9p)"

ktm-userdev-exec-drain-runit-run: build-ktm-exec-drain-case build-runit kernel-x64-userspace.iso
	@echo "  SMOKE   exec-drain under runit PID1..."
	@chmod +x scripts/ktm_userdev_runit_run.sh; \
	scripts/ktm_userdev_runit_run.sh \
		--init $(KTM_EXEC_DRAIN_BIN) \
		--inject $(KTM_TRUE_HELPER_BIN):bin/f41true \
		--log /tmp/ktm-userdev-exec-drain-runit-run.log \
		--timeout 360 \
		--done KTM_USERDEV_EXEC_DRAIN_OK \
		--require 'TEST_END|exec_drain|PASS' \
		--require KTM_USERDEV_EXEC_DRAIN_OK \
		--require KTM_HOSTSHARE_REPORT_OK \
		--host-file ktm_exec_drain.txt \
		--host-grep KTM_USERDEV_EXEC_DRAIN_OK
	@echo "✓ ktm-userdev-exec-drain-runit-run (runit PID1 + 9p + f41true)"

build-ktm-session-stress-case:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  KTM     Building session_stress ($(KTM_SESSION_STRESS_BIN))"
	@$(MUSL_CC) $(KTM_USERDEV_MUSL_FLAGS) \
		-o $(KTM_SESSION_STRESS_BIN) $(KTM_SESSION_STRESS_SRC)
	@file $(KTM_SESSION_STRESS_BIN) | grep -q ELF
	@echo "✓ build-ktm-session-stress-case OK"

# Packed ISD disk (product BusyBox applets). Hostshare stub replaces runit
# as /sbin/init; the case runs from virtio-9p and execs ISD /bin/* applets.
# Does not inject fase48 stubs. PROFILE=development unless overridden.
ktm-userdev-session-stress-run: build-ktm-session-stress-case build-init-hostshare-exec kernel-x64-userspace.iso
	@chmod +x scripts/ktm_prepare_isd_session_disk.sh
	@ISD_DISK=$$(PROFILE=$(KTM_SESSION_ISD_PROFILE) \
		IR0_PRODUCT_PROFILE=$(KTM_SESSION_ISD_PROFILE) \
		scripts/ktm_prepare_isd_session_disk.sh); \
	python3 scripts/ktm_userdev_runner.py \
		--disk "$$ISD_DISK" \
		--init $(KTM_SESSION_STRESS_BIN) \
		--inject $(HOSTSHARE_EXEC_STUB):sbin/init \
		--log /tmp/ktm-userdev-session-stress.log --timeout 600 \
		--mem 512M \
		--done KTM_USERDEV_SESSION_STRESS_OK \
		--require HOSTSHARE_EXEC_MOUNT_OK \
		--require 'TEST_END|session_stress|PASS' \
		--require KTM_USERDEV_SESSION_STRESS_OK
	@echo "✓ ktm-userdev-session-stress-run (ISD BusyBox echo/cat/true/uname/ls)"

ktm-userdev-reap-drain-run: build-ktm-reap-drain-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_REAP_DRAIN_BIN) \
		--log /tmp/ktm-userdev-reap-drain.log --timeout 120 \
		--done KTM_USERDEV_REAP_DRAIN_OK \
		--require 'TEST_END|reap_drain|PASS' \
		--require KTM_USERDEV_REAP_DRAIN_OK
	@echo "✓ ktm-userdev-reap-drain-run (FASE44 reap-drain → KTM; share payload)"

ktm-userdev-reap-drain-virtfs-run: build-ktm-reap-drain-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_REAP_DRAIN_BIN) \
		--log /tmp/ktm-userdev-reap-drain-virtfs.log --timeout 120 \
		--done KTM_USERDEV_REAP_DRAIN_OK \
		--require 'TEST_END|reap_drain|PASS' \
		--require KTM_USERDEV_REAP_DRAIN_OK \
		--require KTM_HOSTSHARE_REPORT_OK \
		--host-file ktm_reap_drain.txt \
		--host-grep KTM_USERDEV_REAP_DRAIN_OK
	@echo "✓ ktm-userdev-reap-drain-virtfs-run (reap-drain + virtio-9p)"

ktm-userdev-reap-drain-runit-run: build-ktm-reap-drain-case build-runit kernel-x64-userspace.iso
	@echo "  SMOKE   reap-drain under runit PID1..."
	@chmod +x scripts/ktm_userdev_runit_run.sh; \
	scripts/ktm_userdev_runit_run.sh \
		--init $(KTM_REAP_DRAIN_BIN) \
		--log /tmp/ktm-userdev-reap-drain-runit-run.log \
		--timeout 120 \
		--done KTM_USERDEV_REAP_DRAIN_OK \
		--require 'TEST_END|reap_drain|PASS' \
		--require KTM_USERDEV_REAP_DRAIN_OK \
		--require KTM_HOSTSHARE_REPORT_OK \
		--host-file ktm_reap_drain.txt \
		--host-grep KTM_USERDEV_REAP_DRAIN_OK
	@echo "✓ ktm-userdev-reap-drain-runit-run (runit PID1 + 9p)"

ktm-userdev-init-exit-drain-run: build-ktm-init-exit-drain-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_INIT_EXIT_DRAIN_BIN) \
		--log /tmp/ktm-userdev-init-exit-drain.log --timeout 120 \
		--done KTM_INIT_EXIT_DRAIN_OK \
		--require 'TEST_END|init_exit_drain|PASS' \
		--require KTM_INIT_EXIT_DRAIN_OK \
		--require FASE44_INIT_EXIT_DRAIN
	@echo "✓ ktm-userdev-init-exit-drain-run (FASE44 PID1 _exit → KTM)"

ktm-userdev-init-exit-drain-virtfs-run: build-ktm-init-exit-drain-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_INIT_EXIT_DRAIN_BIN) \
		--log /tmp/ktm-userdev-init-exit-drain-virtfs.log --timeout 120 \
		--done KTM_INIT_EXIT_DRAIN_OK \
		--require 'TEST_END|init_exit_drain|PASS' \
		--require KTM_INIT_EXIT_DRAIN_OK \
		--require FASE44_INIT_EXIT_DRAIN \
		--require KTM_HOSTSHARE_REPORT_OK \
		--host-file ktm_init_exit_drain.txt \
		--host-grep KTM_INIT_EXIT_DRAIN_OK
	@echo "✓ ktm-userdev-init-exit-drain-virtfs-run (PID1 _exit + virtio-9p)"

build-ktm-posix-pseudofs-case:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  KTM     Building posix_pseudofs (FASE53B) ($(KTM_POSIX_PSEUDOFS_BIN))"
	@$(MUSL_CC) $(KTM_USERDEV_MUSL_FLAGS) \
		-o $(KTM_POSIX_PSEUDOFS_BIN) $(KTM_POSIX_PSEUDOFS_SRC)
	@file $(KTM_POSIX_PSEUDOFS_BIN) | grep -q ELF
	@echo "✓ build-ktm-posix-pseudofs-case OK"

build-ktm-input-det-case:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  KTM     Building input_det (FASE54C) ($(KTM_INPUT_DET_BIN))"
	@$(MUSL_CC) $(KTM_USERDEV_MUSL_FLAGS) \
		-o $(KTM_INPUT_DET_BIN) $(KTM_INPUT_DET_SRC)
	@file $(KTM_INPUT_DET_BIN) | grep -q ELF
	@echo "✓ build-ktm-input-det-case OK"

build-ktm-nic-reach-case:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  KTM     Building nic_reach (F8-1) ($(KTM_NIC_REACH_BIN))"
	@$(MUSL_CC) $(KTM_USERDEV_MUSL_FLAGS) \
		-o $(KTM_NIC_REACH_BIN) $(KTM_NIC_REACH_SRC)
	@file $(KTM_NIC_REACH_BIN) | grep -q ELF
	@echo "✓ build-ktm-nic-reach-case OK"

build-ktm-tcp-guest-case:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  KTM     Building tcp_guest (F8-2) ($(KTM_TCP_GUEST_BIN))"
	@$(MUSL_CC) $(KTM_USERDEV_MUSL_FLAGS) \
		-o $(KTM_TCP_GUEST_BIN) $(KTM_TCP_GUEST_SRC)
	@file $(KTM_TCP_GUEST_BIN) | grep -q ELF
	@echo "✓ build-ktm-tcp-guest-case OK"

ktm-userdev-posix-pseudofs-run: build-ktm-posix-pseudofs-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_POSIX_PSEUDOFS_BIN) \
		--log /tmp/ktm-userdev-posix-pseudofs.log --timeout 120 \
		--done KTM_USERDEV_POSIX_PSEUDOFS_OK \
		--require 'TEST_END|posix_pseudofs|PASS' \
		--require KTM_USERDEV_POSIX_PSEUDOFS_OK \
		--require KTM_GETDENTS_DEV_CURSOR_OK
	@echo "✓ ktm-userdev-posix-pseudofs-run (FASE53B → KTM)"

ktm-userdev-posix-pseudofs-virtfs-run: build-ktm-posix-pseudofs-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_POSIX_PSEUDOFS_BIN) \
		--log /tmp/ktm-userdev-posix-pseudofs-virtfs.log --timeout 120 \
		--done KTM_USERDEV_POSIX_PSEUDOFS_OK \
		--require 'TEST_END|posix_pseudofs|PASS' \
		--require KTM_USERDEV_POSIX_PSEUDOFS_OK \
		--require KTM_GETDENTS_DEV_CURSOR_OK \
		--require KTM_HOSTSHARE_REPORT_OK \
		--host-file ktm_posix_pseudofs.txt \
		--host-grep KTM_USERDEV_POSIX_PSEUDOFS_OK
	@echo "✓ ktm-userdev-posix-pseudofs-virtfs-run (53B + virtio-9p)"

ktm-userdev-posix-pseudofs-runit-run: build-ktm-posix-pseudofs-case build-runit kernel-x64-userspace.iso
	@echo "  SMOKE   posix-pseudofs under runit PID1..."
	@chmod +x scripts/ktm_userdev_runit_run.sh; \
	scripts/ktm_userdev_runit_run.sh \
		--init $(KTM_POSIX_PSEUDOFS_BIN) \
		--log /tmp/ktm-userdev-posix-pseudofs-runit-run.log \
		--timeout 120 \
		--done KTM_USERDEV_POSIX_PSEUDOFS_OK \
		--require 'TEST_END|posix_pseudofs|PASS' \
		--require KTM_USERDEV_POSIX_PSEUDOFS_OK \
		--require KTM_GETDENTS_DEV_CURSOR_OK \
		--require KTM_HOSTSHARE_REPORT_OK \
		--host-file ktm_posix_pseudofs.txt \
		--host-grep KTM_USERDEV_POSIX_PSEUDOFS_OK
	@echo "✓ ktm-userdev-posix-pseudofs-runit-run (runit PID1 + 9p)"

ktm-userdev-input-det-run: build-ktm-input-det-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_INPUT_DET_BIN) \
		--log /tmp/ktm-userdev-input-det.log --timeout 120 \
		--done KTM_USERDEV_INPUT_DET_OK \
		--require 'TEST_END|input_det|PASS' \
		--require KTM_USERDEV_INPUT_DET_OK
	@echo "✓ ktm-userdev-input-det-run (FASE54C → KTM)"

ktm-userdev-input-det-virtfs-run: build-ktm-input-det-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_INPUT_DET_BIN) \
		--log /tmp/ktm-userdev-input-det-virtfs.log --timeout 120 \
		--done KTM_USERDEV_INPUT_DET_OK \
		--require 'TEST_END|input_det|PASS' \
		--require KTM_USERDEV_INPUT_DET_OK \
		--require KTM_HOSTSHARE_REPORT_OK \
		--host-file ktm_input_det.txt \
		--host-grep KTM_USERDEV_INPUT_DET_OK
	@echo "✓ ktm-userdev-input-det-virtfs-run (54C + virtio-9p)"

ktm-userdev-input-det-runit-run: build-ktm-input-det-case build-runit kernel-x64-userspace.iso
	@echo "  SMOKE   input-det under runit PID1..."
	@chmod +x scripts/ktm_userdev_runit_run.sh; \
	scripts/ktm_userdev_runit_run.sh \
		--init $(KTM_INPUT_DET_BIN) \
		--log /tmp/ktm-userdev-input-det-runit-run.log \
		--timeout 120 \
		--done KTM_USERDEV_INPUT_DET_OK \
		--require 'TEST_END|input_det|PASS' \
		--require KTM_USERDEV_INPUT_DET_OK \
		--require KTM_HOSTSHARE_REPORT_OK \
		--host-file ktm_input_det.txt \
		--host-grep KTM_USERDEV_INPUT_DET_OK
	@echo "✓ ktm-userdev-input-det-runit-run (runit PID1 + 9p)"

.PHONY: smoke-ktm-drains-runit
smoke-ktm-drains-runit:
	@echo "  SMOKE   KTM drains/storms under runit (init-exit stays stub)..."
	@$(MAKE) -s ktm-userdev-fork-storm-runit-run
	@$(MAKE) -s ktm-userdev-exec-drain-runit-run
	@$(MAKE) -s ktm-userdev-reap-drain-runit-run
	@$(MAKE) -s ktm-userdev-posix-pseudofs-runit-run
	@$(MAKE) -s ktm-userdev-input-det-runit-run
	@$(MAKE) -s ktm-userdev-init-exit-drain-virtfs-run
	@echo "✓ smoke-ktm-drains-runit passed"

ktm-userdev-nic-reach-run: build-ktm-nic-reach-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_NIC_REACH_BIN) \
		--log /tmp/ktm-userdev-nic-reach.log --timeout 120 \
		--done F8_NIC_REACH_OK \
		--require 'TEST_END|nic_reach|PASS' \
		--require F8_NIC_REACH_OK \
		--qemu-arg=-netdev --qemu-arg=user,id=net0 \
		--qemu-arg=-device --qemu-arg=rtl8139,netdev=net0
	@echo "✓ ktm-userdev-nic-reach-run (F8-1 NIC probe)"

ktm-userdev-nic-reach-virtfs-run: build-ktm-nic-reach-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_NIC_REACH_BIN) \
		--log /tmp/ktm-userdev-nic-reach-virtfs.log --timeout 120 \
		--done F8_NIC_REACH_OK \
		--require 'TEST_END|nic_reach|PASS' \
		--require F8_NIC_REACH_OK \
		--require KTM_HOSTSHARE_REPORT_OK \
		--host-file ktm_nic_reach.txt \
		--host-grep F8_NIC_REACH_OK \
		--qemu-arg=-netdev --qemu-arg=user,id=net0 \
		--qemu-arg=-device --qemu-arg=rtl8139,netdev=net0
	@echo "✓ ktm-userdev-nic-reach-virtfs-run (F8-1 + virtio-9p)"

# Alias ship/docs name for F8-1
smoke-nic-reach: ktm-userdev-nic-reach-virtfs-run

.PHONY: ktm-userdev-nic-reach-virtio-run smoke-nic-reach-virtio
ktm-userdev-nic-reach-virtio-run: build-ktm-nic-reach-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_NIC_REACH_BIN) \
		--log /tmp/ktm-userdev-nic-reach-virtio.log --timeout 120 \
		--done F8_NIC_REACH_OK \
		--require 'TEST_END|nic_reach|PASS' \
		--require F8_NIC_REACH_OK \
		--require KTM_HOSTSHARE_REPORT_OK \
		--host-file ktm_nic_reach.txt \
		--host-grep F8_NIC_REACH_OK \
		--qemu-arg=-netdev --qemu-arg=user,id=net0 \
		--qemu-arg=-device --qemu-arg=virtio-net-pci,netdev=net0,disable-modern=on
	@echo "✓ ktm-userdev-nic-reach-virtio-run (F8-1 + virtio-net legacy)"

smoke-nic-reach-virtio: ktm-userdev-nic-reach-virtio-run

ktm-userdev-tcp-guest-run: build-ktm-tcp-guest-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@rm -f /tmp/f8-tcp-guest-host.ok
	@python3 scripts/tcp_wire_host_listener.py --port 8889 --expect GUESTTCP \
		--reply 'GUESTECHO\n' --timeout 150 --out /tmp/f8-tcp-guest-host.ok & \
	LPID=$$!; sleep 1; \
	python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_TCP_GUEST_BIN) \
		--log /tmp/ktm-userdev-tcp-guest.log --timeout 180 \
		--done F8_TCP_GUEST_OK \
		--require 'TEST_END|tcp_guest|PASS' \
		--require F8_TCP_GUEST_OK \
		--require F8_TCP_GUEST_CONNECT_OK \
		--require F8_TCP_GUEST_SENDRECV_OK \
		--qemu-arg=-netdev --qemu-arg=user,id=net0 \
		--qemu-arg=-device --qemu-arg=rtl8139,netdev=net0; \
	RC=$$?; kill $$LPID 2>/dev/null || true; wait $$LPID 2>/dev/null || true; \
	test -f /tmp/f8-tcp-guest-host.ok && exit $$RC; exit 1
	@echo "✓ ktm-userdev-tcp-guest-run (F8-2 guest TCP wire)"

ktm-userdev-tcp-guest-virtfs-run: build-ktm-tcp-guest-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@rm -f /tmp/f8-tcp-guest-host.ok
	@python3 scripts/tcp_wire_host_listener.py --port 8889 --expect GUESTTCP \
		--reply 'GUESTECHO\n' --timeout 150 --out /tmp/f8-tcp-guest-host.ok & \
	LPID=$$!; sleep 1; \
	python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_TCP_GUEST_BIN) \
		--log /tmp/ktm-userdev-tcp-guest-virtfs.log --timeout 180 \
		--done F8_TCP_GUEST_OK \
		--require 'TEST_END|tcp_guest|PASS' \
		--require F8_TCP_GUEST_OK \
		--require F8_TCP_GUEST_CONNECT_OK \
		--require F8_TCP_GUEST_SENDRECV_OK \
		--require KTM_HOSTSHARE_REPORT_OK \
		--host-file ktm_tcp_guest.txt \
		--host-grep F8_TCP_GUEST_OK \
		--qemu-arg=-netdev --qemu-arg=user,id=net0 \
		--qemu-arg=-device --qemu-arg=rtl8139,netdev=net0; \
	RC=$$?; kill $$LPID 2>/dev/null || true; wait $$LPID 2>/dev/null || true; \
	test -f /tmp/f8-tcp-guest-host.ok && exit $$RC; exit 1
	@echo "✓ ktm-userdev-tcp-guest-virtfs-run (F8-2 wire + virtio-9p)"

# Alias ship/docs name for F8-2
smoke-tcp-guest: ktm-userdev-tcp-guest-virtfs-run

build-ktm-tcp-wire-case:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  KTM     Building tcp_wire (F8-3) ($(KTM_TCP_WIRE_BIN))"
	@$(MUSL_CC) $(KTM_USERDEV_MUSL_FLAGS) \
		-o $(KTM_TCP_WIRE_BIN) $(KTM_TCP_WIRE_SRC)
	@file $(KTM_TCP_WIRE_BIN) | grep -q ELF
	@echo "✓ build-ktm-tcp-wire-case OK"

F8_TCP_WIRE_HOST_OK = /tmp/f8-tcp-wire-host.ok
F8_TCP_WIRE_NETDEV = user,id=net0

ktm-userdev-tcp-wire-virtfs-run: build-ktm-tcp-wire-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@rm -f $(F8_TCP_WIRE_HOST_OK)
	@python3 scripts/tcp_wire_host_listener.py --port 8888 --expect WIRETCP \
		--reply 'WIREECHO\n' --timeout 150 --out $(F8_TCP_WIRE_HOST_OK) & \
	LPID=$$!; sleep 1; \
	RC=0; \
	python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_TCP_WIRE_BIN) \
		--log /tmp/ktm-userdev-tcp-wire-virtfs.log --timeout 180 \
		--done F8_TCP_WIRE_OK \
		--require 'TEST_END|tcp_wire|PASS' \
		--require F8_TCP_WIRE_OK \
		--require F8_TCP_WIRE_CONNECT_OK \
		--require F8_TCP_WIRE_SENDRECV_OK \
		--require KTM_HOSTSHARE_REPORT_OK \
		--host-file ktm_tcp_wire.txt \
		--host-grep F8_TCP_WIRE_OK \
		--qemu-arg=-netdev --qemu-arg=$(F8_TCP_WIRE_NETDEV) \
		--qemu-arg=-device --qemu-arg=rtl8139,netdev=net0 \
		|| RC=$$?; \
	kill $$LPID 2>/dev/null || true; wait $$LPID 2>/dev/null || true; \
	if [ ! -f $(F8_TCP_WIRE_HOST_OK) ]; then \
		echo "✗ F8-3 host listener did not receive WIRETCP"; \
		exit 1; \
	fi; \
	exit $$RC
	@echo "✓ ktm-userdev-tcp-wire-virtfs-run (F8-3 wire TCP guest→host 10.0.2.2:8888)"

ktm-userdev-tcp-wire-run: build-ktm-tcp-wire-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@rm -f $(F8_TCP_WIRE_HOST_OK)
	@python3 scripts/tcp_wire_host_listener.py --port 8888 --expect WIRETCP \
		--reply 'WIREECHO\n' --timeout 150 --out $(F8_TCP_WIRE_HOST_OK) & \
	LPID=$$!; sleep 1; \
	RC=0; \
	python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_TCP_WIRE_BIN) \
		--log /tmp/ktm-userdev-tcp-wire.log --timeout 180 \
		--done F8_TCP_WIRE_OK \
		--require 'TEST_END|tcp_wire|PASS' \
		--require F8_TCP_WIRE_OK \
		--require F8_TCP_WIRE_CONNECT_OK \
		--require F8_TCP_WIRE_SENDRECV_OK \
		--qemu-arg=-netdev --qemu-arg=$(F8_TCP_WIRE_NETDEV) \
		--qemu-arg=-device --qemu-arg=rtl8139,netdev=net0 \
		|| RC=$$?; \
	kill $$LPID 2>/dev/null || true; wait $$LPID 2>/dev/null || true; \
	if [ ! -f $(F8_TCP_WIRE_HOST_OK) ]; then \
		echo "✗ F8-3 host listener did not receive WIRETCP"; \
		exit 1; \
	fi; \
	exit $$RC
	@echo "✓ ktm-userdev-tcp-wire-run (F8-3 wire TCP)"

smoke-tcp-wire: ktm-userdev-tcp-wire-virtfs-run

build-ktm-tcp-peer-cc-case:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  KTM     Building tcp_peer_cc (F8 peer loss) ($(KTM_TCP_PEER_CC_BIN))"
	@$(MUSL_CC) $(KTM_USERDEV_MUSL_FLAGS) \
		-o $(KTM_TCP_PEER_CC_BIN) $(KTM_TCP_PEER_CC_SRC)
	@file $(KTM_TCP_PEER_CC_BIN) | grep -q ELF
	@echo "✓ build-ktm-tcp-peer-cc-case OK"

F8_TCP_PEER_CC_HOST_OK = /tmp/f8-tcp-peer-cc-host.ok

.PHONY: ktm-userdev-tcp-peer-cc-virtfs-run smoke-tcp-peer-cc build-ktm-tcp-peer-cc-case
ktm-userdev-tcp-peer-cc-virtfs-run: build-ktm-tcp-peer-cc-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@rm -f $(F8_TCP_PEER_CC_HOST_OK)
	@python3 scripts/tcp_wire_host_listener.py --port 8890 --expect PEERCC \
		--reply 'PEERECHO\n' --timeout 150 --out $(F8_TCP_PEER_CC_HOST_OK) & \
	LPID=$$!; sleep 1; \
	RC=0; \
	python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_TCP_PEER_CC_BIN) \
		--log /tmp/ktm-userdev-tcp-peer-cc-virtfs.log --timeout 180 \
		--done F8_TCP_PEER_CC_OK \
		--require 'TEST_END|tcp_peer_cc|PASS' \
		--require F8_TCP_PEER_CC_OK \
		--require F8_TCP_PEER_CC_CONNECT_OK \
		--require F8_TCP_PEER_CC_SENDRECV_OK \
		--require F8_TCP_PEER_REXMIT_OK \
		--require KTM_HOSTSHARE_REPORT_OK \
		--host-file ktm_tcp_peer_cc.txt \
		--host-grep F8_TCP_PEER_CC_OK \
		--qemu-arg=-netdev --qemu-arg=$(F8_TCP_WIRE_NETDEV) \
		--qemu-arg=-device --qemu-arg=rtl8139,netdev=net0 \
		|| RC=$$?; \
	kill $$LPID 2>/dev/null || true; wait $$LPID 2>/dev/null || true; \
	if [ ! -f $(F8_TCP_PEER_CC_HOST_OK) ]; then \
		echo "✗ peer-cc host listener did not receive PEERCC"; \
		exit 1; \
	fi; \
	exit $$RC
	@echo "✓ ktm-userdev-tcp-peer-cc-virtfs-run (loss probe + rexmit, no synthetic CC)"

smoke-tcp-peer-cc: ktm-userdev-tcp-peer-cc-virtfs-run

# F8 honest MVP battery (loopback separate: smoke-stream-sock)
.PHONY: smoke-f8-net
smoke-f8-net:
	@echo "  SMOKE   F8 net battery (nic-reach + tcp-guest + tcp-wire + tcp-listen)..."
	@$(MAKE) -s smoke-nic-reach
	@$(MAKE) -s smoke-tcp-guest
	@$(MAKE) -s smoke-tcp-wire
	@$(MAKE) -s smoke-tcp-listen
	@echo "✓ smoke-f8-net passed"
.PHONY: smoke-tcp-listen build-init-tcp-listen-smoke
F8_TCP_LISTEN_HOST_OK = /tmp/f8-tcp-listen-host.ok
F8_TCP_LISTEN_SMOKE_LOG = /tmp/tcp-listen-smoke.log
F8_TCP_LISTEN_HOSTPORT = 18777
F8_TCP_LISTEN_NETDEV = user,id=net0,hostfwd=tcp::$(F8_TCP_LISTEN_HOSTPORT)-:7777

build-init-tcp-listen-smoke:
	@if [ -z "$(MUSL_CC)" ]; then echo "✗ musl cc missing"; exit 1; fi
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) setup/pid1/init_tcp_listen_smoke.c
	@echo "✓ build-init-tcp-listen-smoke OK"

smoke-tcp-listen: kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@echo "  SMOKE   F8 wire TCP listen+accept (host→guest :7777)..."
	@$(MAKE) -s build-init-tcp-listen-smoke
	@ATTEMPT=1; \
	while [ $$ATTEMPT -le 2 ]; do \
	  DISK=$$(mktemp /tmp/ir0-tcp-listen.XXXXXX.img); \
	  cp -f disk.img $$DISK; \
	  python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	  rm -f $(F8_TCP_LISTEN_HOST_OK) $(F8_TCP_LISTEN_SMOKE_LOG); \
	  python3 scripts/tcp_wire_host_connector.py --port $(F8_TCP_LISTEN_HOSTPORT) \
		--payload 'LISTENOK\n' --timeout 75 --out $(F8_TCP_LISTEN_HOST_OK) & \
	  HPID=$$!; \
	  $(SMOKE_QEMU_RUN) --log $(F8_TCP_LISTEN_SMOKE_LOG) --timeout 90 \
		--done 'F8_TCP_LISTEN_OK' -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-netdev $(F8_TCP_LISTEN_NETDEV) \
		-device rtl8139,netdev=net0 \
		-serial stdio -display none -m 256M -no-reboot || true; \
	  kill $$HPID 2>/dev/null || true; wait $$HPID 2>/dev/null || true; \
	  rm -f $$DISK; \
	  if grep -q "F8_TCP_LISTEN_BOUND_OK" $(F8_TCP_LISTEN_SMOKE_LOG) && \
	     grep -q "F8_TCP_LISTEN_ACCEPT_OK" $(F8_TCP_LISTEN_SMOKE_LOG) && \
	     grep -q "F8_TCP_LISTEN_EOF_OK" $(F8_TCP_LISTEN_SMOKE_LOG) && \
	     grep -q "F8_TCP_LISTEN_OK" $(F8_TCP_LISTEN_SMOKE_LOG) && \
	     test -f $(F8_TCP_LISTEN_HOST_OK); then \
		echo "✓ smoke-tcp-listen passed (guest accept/recv/EOF + host connector)"; \
		exit 0; \
	  fi; \
	  echo "⚠ smoke-tcp-listen attempt $$ATTEMPT failed (RTL8139 TX flake possible); retrying..."; \
	  ATTEMPT=$$((ATTEMPT + 1)); \
	done; \
	echo "✗ smoke-tcp-listen FAILED"; \
	grep -E 'F8_TCP_LISTEN_|panic' $(F8_TCP_LISTEN_SMOKE_LOG) | tail -60; \
	exit 1

ktm-check: kernel-x64.bin arch-guard
	@$(MAKE) -s -C tests/host run
	@python3 scripts/ktm_classify_selftest.py
	@python3 scripts/ktm_panic_inventory.py --check
	@python3 scripts/ktm_syscall_manifest.py --tier1 || true
	@echo "✓ KTM check complete (tier-1 manifest gaps are informational)"

ktm-classify-selftest:
	@python3 scripts/ktm_classify_selftest.py

ktm-manifest:
	@python3 scripts/ktm_syscall_manifest.py --tier1

ktm-classify:
	@python3 scripts/ktm_log_classify.py $(or $(LOG),/tmp/runit-ash-smoke.log)

ktm-report:
	@python3 scripts/ktm_report.py $(or $(LOG),/tmp/runit-ash-smoke.log)

ifdef IR0_LEGACY_SMOKE
include setup/make/legacy-smokes.mk
endif

clean:
	@echo "Cleaning build artifacts..."
	@find . -name "*.o" -type f -delete
	@find . -name "*.d" -type f -delete
	@find . -name "*.bin" -type f -delete
	@find . -name "*.su" -type f -delete
	@rm -f kernel-x64.iso kernel-x64-test.iso
	@rm -f kernel-x64.map kernel-arm64.map kernel-x64.disasm compile_commands.json
	@rm -rf iso iso_test
	@$(MAKE) -C tests/kernel_memsafe clean 2>/dev/null || true
	@echo "Clean done."

# TEST SUITE — Compila todos los artefactos de test (estilo kernels de producción).
# - Código kernel en host para Valgrind (make kernel-memsafe)
# - Kernel con tests in-kernel (ktest) para make kernel-tests
# - Análisis del binario de arranque (make kernel-analyze → kernel-x64.bin)
tests: kernel-x64-test.bin
	@echo "  TEST    Building kernel-memsafe (kernel code under Valgrind)..."
	@$(MAKE) -C tests/kernel_memsafe KERNEL_ROOT=$(KERNEL_ROOT) all
	@echo "  TEST    Running host test suite..."
	@$(MAKE) -C tests/host run
	@echo "✓ Tests built: tests/kernel_memsafe/ir0_kernel_memsafe, kernel-x64-test.bin"

# Valgrind sobre código del kernel compilado para host (resource_registry, etc.)
kernel-memsafe:
	@echo "  KERNEL-MEMSAFE  Building kernel code for host and running Valgrind..."
	@$(MAKE) -C tests/kernel_memsafe KERNEL_ROOT=$(KERNEL_ROOT) memsafe
	@echo "✓ kernel-memsafe passed"

# Batería in-kernel al estilo KUnit: tests se ejecutan al arranque (no dependen de la shell).
# QEMU headless, sin red. El kernel (kernel-x64-test.bin) llama kernel_test_run_all() en boot.
kernel-tests: kernel-x64-test.iso disk.img
	@echo "  KTEST   Running in-kernel test suite at boot (KUnit-style)..."
	@$(SMOKE_QEMU_RUN) --log /tmp/ktest.log --timeout 60 --done "test(s) passed" \
		--fail-regex 'Some tests FAILED|not ok ' -- \
		$(QEMU) -cdrom kernel-x64-test.iso -drive file=disk.img,format=raw,if=ide,index=0 -serial stdio -display none -m 128M -no-reboot -net none; \
	grep -q "All .* test(s) passed" /tmp/ktest.log && ! grep -q "Some tests FAILED" /tmp/ktest.log && ! grep -q "not ok " /tmp/ktest.log && ! grep -q "# SKIP need process" /tmp/ktest.log; \
	if [ $$? -eq 0 ]; then echo "✓ kernel-tests passed"; exit 0; else echo "✗ kernel-tests FAILED"; exit 1; fi

smoke-multiuser-perms: kernel-tests
	@grep -q "MULTIUSER_PERMS_OK" /tmp/ktest.log || \
		(echo "✗ smoke-multiuser-perms FAILED (tag missing)"; exit 1)
	@echo "✓ smoke-multiuser-perms passed"

.PHONY: smoke-multiuser-perms smoke-musl-pthread smoke-musl-pthread-libc smoke-setuid-exec smoke-passwd build-musl-pthread-smoke build-musl-pthread-libc-smoke build-setuid-exec-smoke build-passwd-smoke

MUSL_PTHREAD_LIBC_SMOKE_SRC = setup/pid1/musl_pthread_libc_smoke.c
MUSL_PTHREAD_LIBC_SMOKE_BIN = setup/pid1/musl_pthread_libc_smoke
MUSL_PTHREAD_LIBC_SMOKE_LOG = /tmp/userspace-musl-pthread-libc.log

build-musl-pthread-libc-smoke:
	@echo "  MUSL    Building pthread libc smoke ($(MUSL_PTHREAD_LIBC_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(MUSL_PTHREAD_LIBC_SMOKE_BIN) $(MUSL_PTHREAD_LIBC_SMOKE_SRC) -lpthread
	@file $(MUSL_PTHREAD_LIBC_SMOKE_BIN) | grep -q ELF
	@strings $(MUSL_PTHREAD_LIBC_SMOKE_BIN) 2>/dev/null | grep -q "MUSL_PTHREAD_LIBC_OK" || \
		(echo "✗ pthread libc smoke missing MUSL_PTHREAD_LIBC_OK string"; exit 1)
	@echo "✓ build-musl-pthread-libc-smoke OK"

smoke-musl-pthread-libc: build-musl-pthread-libc-smoke kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@echo "  SMOKE   musl pthread_create/join (-lpthread)..."
	@DISK=$$(mktemp /tmp/ir0-musl-pthread-libc.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(MUSL_PTHREAD_LIBC_SMOKE_BIN) sbin/init && \
	rm -f $(MUSL_PTHREAD_LIBC_SMOKE_LOG); \
	$(SMOKE_QEMU_RUN) --log $(MUSL_PTHREAD_LIBC_SMOKE_LOG) --profile musl-pthread \
		--done MUSL_PTHREAD_LIBC_OK -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK; \
	grep -q "MUSL_PTHREAD_LIBC_OK" $(MUSL_PTHREAD_LIBC_SMOKE_LOG) && \
		echo "✓ smoke-musl-pthread-libc passed" || \
		(echo "✗ smoke-musl-pthread-libc FAILED"; \
		 grep -E 'MUSL_|pthread|panic|errno' $(MUSL_PTHREAD_LIBC_SMOKE_LOG) | tail -40; exit 1)

SETUID_EXEC_TAGS = SETID_SUID_OK SETID_SGID_OK SETID_PLAIN_OK SETID_NNP_OK \
	SETID_ENOENT_OK SETID_SCRIPT_OK SETID_NOEXEC_OK SETUID_EXEC_ALL_OK

PASSWD_SMOKE_TAGS = PASSWD_SETUP_OK PASSWD_CHANGE_OK PASSWD_WRONG_OLD_OK \
	PASSWD_OTHER_DENIED_OK PASSWD_ALL_OK

# passwd(1) runs against the product rootfs: it needs /etc/{passwd,shadow,group}
# and the set-user-ID /bin/passwd installed by install-to-disk.sh.
smoke-passwd: build-passwd-smoke load-userspace-runit kernel-x64-userspace.iso
	@echo "  SMOKE   passwd(1) shadow update, wrong old password, other user..."
	@DISK=$$(mktemp /tmp/ir0-passwd-smoke.XXXXXX.img); \
	cp -f disk.img $$DISK && \
	python3 scripts/inject_init_minix.py $$DISK $(PASSWD_SMOKE_BIN) sbin/init && \
	python3 scripts/verify_minix_rootfs.py $$DISK /sbin/init /bin/passwd \
		/etc/passwd /etc/shadow /etc/group && \
	$(SMOKE_QEMU_RUN) --log $(PASSWD_SMOKE_LOG) --timeout 90 --stale-sec 25 \
		--done PASSWD_ALL_OK --fail-regex 'PASSWD_SMOKE_FAIL|KERNEL PANIC' -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@for tag in $(PASSWD_SMOKE_TAGS); do \
		grep -q "$$tag" $(PASSWD_SMOKE_LOG) || \
			{ echo "✗ smoke-passwd FAILED (missing $$tag)"; \
			  grep -E 'PASSWD_|passwd:|AUTH' $(PASSWD_SMOKE_LOG) | tail -20; exit 1; }; \
	done
	@echo "✓ smoke-passwd passed"

smoke-setuid-exec: build-setuid-exec-smoke kernel-x64-userspace.iso
	@echo "  SMOKE   setuid/setgid on exec, saved IDs, no_new_privs, exec DAC..."
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	dd if=/dev/zero of=$$DISK bs=1M count=64 status=none && \
	python3 scripts/inject_init_minix.py --format-large $$DISK && \
	python3 scripts/inject_init_minix.py $$DISK $(SETUID_EXEC_SMOKE_BIN) sbin/init && \
	python3 scripts/inject_init_minix.py --mode 4755 $$DISK $(SETID_HELPER_BIN) bin/setid_suid && \
	python3 scripts/inject_init_minix.py --mode 2755 $$DISK $(SETID_HELPER_BIN) bin/setid_sgid && \
	python3 scripts/inject_init_minix.py --mode 755 $$DISK $(SETID_HELPER_BIN) bin/setid_plain && \
	python3 scripts/inject_init_minix.py --mode 4700 $$DISK $(SETID_HELPER_BIN) bin/setid_priv && \
	python3 scripts/inject_init_minix.py --mode 4755 $$DISK $(SETID_SCRIPT_SRC) bin/setid_script && \
	python3 scripts/verify_minix_rootfs.py $$DISK /sbin/init /bin/setid_suid /bin/setid_sgid && \
	$(SMOKE_QEMU_RUN) --log $(SETUID_EXEC_SMOKE_LOG) --profile musl-arch-prctl \
		--done SETUID_EXEC_ALL_OK --fail-regex 'SETID_HELPER_FAIL|SETID_CASE_FAIL|SETUID_EXEC_FAIL' -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 128M -no-reboot -net none; \
	rm -f $$DISK;
	@for tag in $(SETUID_EXEC_TAGS); do \
		grep -q "$$tag" $(SETUID_EXEC_SMOKE_LOG) || \
			{ echo "✗ smoke-setuid-exec FAILED (missing $$tag)"; \
			  grep -E 'SETID_|SETUID_' $(SETUID_EXEC_SMOKE_LOG) | tail -20; exit 1; }; \
	done
	@echo "✓ smoke-setuid-exec passed"

CHROOT_SMOKE_TAGS = CHROOT_OPEN_INSIDE_OK CHROOT_OUTSIDE_DENIED CHROOT_GETCWD_OK \
	CHROOT_FORK_INHERIT_OK CHROOT_OK

.PHONY: build-chroot-smoke smoke-chroot
smoke-chroot: build-chroot-smoke kernel-x64-userspace.iso
	@echo "  SMOKE   chroot(2) jail remap + fork inherit..."
	@DISK=$$(mktemp /tmp/ir0-chroot-smoke.XXXXXX.img); \
	dd if=/dev/zero of=$$DISK bs=1M count=64 status=none && \
	python3 scripts/inject_init_minix.py --format-large $$DISK && \
	python3 scripts/inject_init_minix.py $$DISK $(CHROOT_SMOKE_BIN) sbin/init && \
	python3 scripts/verify_minix_rootfs.py $$DISK /sbin/init && \
	$(SMOKE_QEMU_RUN) --log $(CHROOT_SMOKE_LOG) --timeout 60 --stale-sec 20 \
		--done CHROOT_OK --fail-regex 'CHROOT_FAIL|KERNEL PANIC' -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 128M -no-reboot -net none; \
	rm -f $$DISK;
	@for tag in $(CHROOT_SMOKE_TAGS); do \
		grep -q "$$tag" $(CHROOT_SMOKE_LOG) || \
			{ echo "✗ smoke-chroot FAILED (missing $$tag)"; \
			  grep -E 'CHROOT_' $(CHROOT_SMOKE_LOG) | tail -30; exit 1; }; \
	done
	@echo "✓ smoke-chroot passed"

smoke-musl-pthread: build-musl-pthread-smoke kernel-x64-userspace.iso
	@echo "  SMOKE   musl pthread_create + join..."
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	dd if=/dev/zero of=$$DISK bs=1M count=64 status=none && \
	python3 scripts/inject_init_minix.py --format $$DISK && \
	python3 scripts/inject_init_minix.py $$DISK $(MUSL_PTHREAD_SMOKE_BIN) sbin/init && \
	python3 scripts/verify_minix_rootfs.py $$DISK /sbin/init && \
	$(SMOKE_QEMU_RUN) --log $(MUSL_PTHREAD_SMOKE_LOG) --profile musl-pthread \
		--done MUSL_PTHREAD_OK -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 128M -no-reboot -net none; \
	rm -f $$DISK;
	@grep -q "MUSL_PTHREAD_OK" $(MUSL_PTHREAD_SMOKE_LOG) && \
		echo "✓ smoke-musl-pthread passed" || \
		(echo "✗ smoke-musl-pthread FAILED"; exit 1)

# Análisis del binario del kernel: secciones (size -A), símbolos no definidos, kmain.
kernel-analyze: kernel-x64.bin
	@echo "  ANALYZE kernel-x64.bin"
	@echo "----------------------------------------"
	@echo "Section sizes (size -A):"
	@size -A kernel-x64.bin
	@echo "----------------------------------------"
	@echo "Undefined symbols (nm --undefined-only, first 20):"
	@nm --undefined-only kernel-x64.bin 2>/dev/null | head -20 || true
	@UNDEF=$$(nm --undefined-only kernel-x64.bin 2>/dev/null | wc -l); echo "Undefined symbol count: $$UNDEF"
	@echo "----------------------------------------"
	@echo "Entry symbol:"
	@nm kernel-x64.bin 2>/dev/null | grep -E ' [Tt] kmain$$' || true
	@if nm kernel-x64.bin 2>/dev/null | grep -q ' [Tt] kmain$$'; then echo "✓ kernel-analyze passed (kmain present)"; else echo "✗ kernel-analyze FAILED (kmain not found)"; exit 1; fi

# H6: kernel .text regression gate (raise KERNEL_TEXT_BUDGET only with documented reason).
KERNEL_TEXT_BUDGET ?= 960000

kernel-text-budget: kernel-x64.bin
	@TEXT=$$(size -A kernel-x64.bin | awk '/^\.text/{print $$2}'); \
	echo "kernel .text = $$TEXT bytes (budget $(KERNEL_TEXT_BUDGET))"; \
	if [ -z "$$TEXT" ] || [ "$$TEXT" -gt "$(KERNEL_TEXT_BUDGET)" ]; then \
		echo "✗ kernel-text-budget FAILED"; exit 1; \
	fi; \
	echo "✓ kernel-text-budget passed"

# Salud del sistema: ejecuta toda la batería (kernel-analyze, kernel-memsafe, kernel-tests).
# Útil para CI o comprobar que el árbol está sano antes de un commit.
health: kernel-analyze kernel-text-budget
	@echo ""
	@echo "  HEALTH  Running full test suite..."
	@$(MAKE) kernel-memsafe && $(MAKE) kernel-tests
	@echo ""
	@echo "✓ health passed (kernel-analyze, kernel-text-budget, kernel-memsafe, kernel-tests)"

# Minimal permanent build matrix for modular configs.
build-matrix-min:
	@$(MAKE) -s config-wiring-check
	@$(MAKE) -s arch-config-check
	@echo "  MATRIX  defconfig"
	@$(MAKE) defconfig >/dev/null
	@$(MAKE) -s kernel-x64.bin >/dev/null
	@echo "  MATRIX  tiny preset"
	@python3 $(KERNEL_ROOT)/scripts/kconfig/menuconfig.py --preset tiny >/dev/null
	@$(MAKE) -s kernel-x64.bin >/dev/null
	@echo "  MATRIX  networking disabled"
	@$(MAKE) defconfig >/dev/null
	@python3 $(KERNEL_ROOT)/scripts/kconfig/menuconfig.py --set ENABLE_NETWORKING=n INIT_NETWORK_STACK=n >/dev/null
	@$(MAKE) -s kernel-x64.bin >/dev/null
	@echo "  MATRIX  storage fully disabled"
	@$(MAKE) defconfig >/dev/null
	@python3 $(KERNEL_ROOT)/scripts/kconfig/menuconfig.py --set ENABLE_STORAGE_ATA=n ENABLE_STORAGE_ATA_BLOCK=n INIT_STORAGE_ATA=n INIT_STORAGE_ATA_BLOCK=n >/dev/null
	@$(MAKE) -s kernel-x64.bin >/dev/null
	@echo "  MATRIX  storage core without block layer"
	@$(MAKE) defconfig >/dev/null
	@python3 $(KERNEL_ROOT)/scripts/kconfig/menuconfig.py --set ENABLE_STORAGE_ATA=y ENABLE_STORAGE_ATA_BLOCK=n INIT_STORAGE_ATA=y INIT_STORAGE_ATA_BLOCK=n >/dev/null
	@$(MAKE) -s kernel-x64.bin >/dev/null
	@echo "  MATRIX  tmpfs-only root"
	@$(MAKE) defconfig >/dev/null
	@python3 $(KERNEL_ROOT)/scripts/kconfig/menuconfig.py --set ENABLE_FS_MINIX=n ENABLE_FS_TMPFS=y ROOT_FILESYSTEM=tmpfs >/dev/null
	@$(MAKE) -s kernel-x64.bin >/dev/null
	@echo "  MATRIX  scheduler policy 1"
	@$(MAKE) defconfig >/dev/null
	@python3 $(KERNEL_ROOT)/scripts/kconfig/menuconfig.py --set SCHEDULER_POLICY=1 >/dev/null
	@$(MAKE) -s kernel-x64.bin >/dev/null
	@echo "  MATRIX  scheduler policy 2"
	@$(MAKE) defconfig >/dev/null
	@python3 $(KERNEL_ROOT)/scripts/kconfig/menuconfig.py --set SCHEDULER_POLICY=2 >/dev/null
	@$(MAKE) -s kernel-x64.bin >/dev/null
	@echo "  MATRIX  arm64 config scaffold"
	@$(MAKE) defconfig >/dev/null
	@python3 $(KERNEL_ROOT)/scripts/kconfig/menuconfig.py --set ARCH_X86_64=n ARCH_ARM64=y DRV_NIC_RTL8139=n DRV_NIC_E1000=n >/dev/null
	@$(MAKE) -s arch-config-check >/dev/null
	@$(MAKE) defconfig >/dev/null
	@echo "  MATRIX  USB host enabled"
	@$(MAKE) defconfig >/dev/null
	@python3 $(KERNEL_ROOT)/scripts/kconfig/menuconfig.py --set ENABLE_USB_HOST=y INIT_USB_HOST=y >/dev/null
	@$(MAKE) -s kernel-x64.bin >/dev/null
	@echo "  MATRIX  Bluetooth disabled"
	@$(MAKE) defconfig >/dev/null
	@python3 $(KERNEL_ROOT)/scripts/kconfig/menuconfig.py --set ENABLE_BLUETOOTH=n INIT_BLUETOOTH_DRIVER=n >/dev/null
	@$(MAKE) -s kernel-x64.bin >/dev/null
	@echo "  MATRIX  lazy MM disabled (eager mmap/brk bisect)"
	@$(MAKE) defconfig >/dev/null
	@python3 $(KERNEL_ROOT)/scripts/kconfig/menuconfig.py --set LAZY_ANON_MMAP=n LAZY_BRK_HEAP=n >/dev/null
	@$(MAKE) -s kernel-x64.bin >/dev/null
	@$(MAKE) defconfig >/dev/null
	@$(MAKE) -s arch-guard
	@echo "✓ build-matrix-min passed"

config-sim:
	@python3 $(KERNEL_ROOT)/scripts/kconfig/config_sim.py

config-wiring-check:
	@if [ -f "$(KERNEL_ROOT)/.config" ] && [ ! -f "$(KERNEL_ROOT)/includes/generated/autoconf.h" ]; then \
		echo "✗ config-wiring-check: includes/generated/autoconf.h missing"; \
		exit 1; \
	fi
	@echo "✓ config-wiring-check passed"

arch-config-check:
	@if [ "$(CONFIG_ARCH_X86_64)" = "y" ] && [ "$(CONFIG_ARCH_ARM64)" = "y" ]; then \
		echo "✗ arch-config-check: both CONFIG_ARCH_X86_64 and CONFIG_ARCH_ARM64 are enabled"; \
		exit 1; \
	fi
	@if [ "$(CONFIG_ARCH_X86_64)" != "y" ] && [ "$(CONFIG_ARCH_ARM64)" != "y" ]; then \
		echo "✗ arch-config-check: no architecture selected"; \
		exit 1; \
	fi
	@echo "✓ arch-config-check passed"

runtime-net-check:
	@echo "  RUNTIME  network smoke (QEMU user-net)"
	@$(MAKE) -s defconfig >/dev/null
	@python3 $(KERNEL_ROOT)/scripts/kconfig/config_sim.py \
		--symbols ENABLE_NETWORKING,INIT_NETWORK_STACK \
		--max-cases 2 \
		--build-cmd "make -s kernel-x64.iso disk.img" \
		--runtime-cmd "python3 $(KERNEL_ROOT)/scripts/net_runtime_smoke.py --timeout-sec 25"
	@echo "✓ runtime-net-check passed"

runtime-mount-check: kernel-tests
	@echo "  RUNTIME  mount contract smoke (QEMU)"
	@grep -q "ok .* - mount_proc_contract" /tmp/ktest.log && \
	 grep -q "ok .* - mount_tmpfs_contract" /tmp/ktest.log && \
	 grep -q "ok .* - mount_multi_fs_contract" /tmp/ktest.log && \
	 grep -q "ok .* - mount_longest_prefix_contract" /tmp/ktest.log && \
	 grep -q "ok .* - block_hda_read_contract" /tmp/ktest.log
	@echo "✓ runtime-mount-check passed"

arch-guard:
	@python3 $(KERNEL_ROOT)/scripts/architecture_guard.py

repo-hygiene-guard:
	@python3 $(KERNEL_ROOT)/scripts/repo_hygiene_guard.py

mandocs:
	@python3 $(KERNEL_ROOT)/scripts/build_mandocs.py

mandocs-en:
	@python3 $(KERNEL_ROOT)/scripts/build_mandocs.py --lang en

mandocs-es:
	@python3 $(KERNEL_ROOT)/scripts/build_mandocs.py --lang es

# Pre-render IR0 section-7 pages for guest BusyBox man (cat7 ASCII).
# Output: build/guest-man/usr/share/man/cat7/IR0-*.7
# Injected by load-userspace-runit / install-to-disk (IR0_GUEST_MANDOCS=1 default).
prepare-guest-mandocs:
	@chmod +x $(KERNEL_ROOT)/scripts/prepare_guest_mandocs.sh
	@$(KERNEL_ROOT)/scripts/prepare_guest_mandocs.sh

check-guest-mandocs: prepare-guest-mandocs
	@test -s build/guest-man/usr/share/man/cat7/IR0-boot.7
	@test -s build/guest-man/usr/share/man/cat7/IR0-uspace.7
	@test -s build/guest-man/usr/share/man/cat7/IR0-onboard.7
	@! grep -qE '^\.Sh[[:space:]]' build/guest-man/usr/share/man/cat7/IR0-boot.7
	@grep -qi boot build/guest-man/usr/share/man/cat7/IR0-boot.7
	@if [ -f disk.img ]; then \
		python3 scripts/verify_minix_rootfs.py --gate disk.img \
			/usr/share/man/cat7/IR0-boot.7 \
			/usr/share/man/cat7/IR0-uspace.7 \
			/usr/share/man/cat7/IR0-onboard.7 \
			/etc/man.conf; \
		echo "✓ check-guest-mandocs OK (ASCII cat7 + disk.img)"; \
	else \
		echo "✓ check-guest-mandocs OK (ASCII cat7; disk.img not present yet)"; \
	fi

mandocs-uninstall:
	@python3 $(KERNEL_ROOT)/scripts/build_mandocs.py --uninstall \
		$(if $(MANDOC_LANG),--lang $(MANDOC_LANG),) \
		$(if $(MANDOC_PREFIX),--prefix $(MANDOC_PREFIX),)

mandocs-view:
	@lang=$${MANDOC_LANG:-$$(cat build/mandoc/last-lang 2>/dev/null || echo en)}; \
	if [ "$$lang" = "es" ] && man -w ir0-krnl-es >/dev/null 2>&1; then \
		man ir0-krnl-es; \
	elif man -w ir0-krnl >/dev/null 2>&1; then \
		man ir0-krnl; \
	elif [ -f build/mandoc/$$lang/ir0-krnl.7 ]; then \
		man -l build/mandoc/$$lang/ir0-krnl.7; \
	else \
		echo "missing manual — run: make mandocs MANDOC_LANG=$$lang"; \
		exit 1; \
	fi

ai-dev-rules-install:
	@python3 $(KERNEL_ROOT)/scripts/sync_ai_dev_rules.py install

build-matrix-full:
	@$(MAKE) -s build-matrix-min
	@echo "  MATRIX  boolean config simulation (24 cases)"
	@python3 $(KERNEL_ROOT)/scripts/kconfig/config_sim.py --max-cases 24 --build-cmd "make -s kernel-x64.bin"
	@echo "  MATRIX  runtime network smoke checks"
	@$(MAKE) -s runtime-net-check
	@echo "  MATRIX  runtime mount smoke checks"
	@$(MAKE) -s runtime-mount-check
	@echo "  MATRIX  architecture guardrails"
	@$(MAKE) -s arch-guard
	@echo "  MATRIX  repository hygiene guardrails"
	@$(MAKE) -s repo-hygiene-guard
	@echo "✓ build-matrix-full passed"

smoke-qemu:
	@echo "  SMOKE   qemu baseline"
	@$(MAKE) -s kernel-tests
	@$(MAKE) -s runtime-net-check
	@$(MAKE) -s runtime-mount-check
	@echo "✓ smoke-qemu passed"

smoke-real-hw:
	@echo "  SMOKE   real hardware checklist"
	@bash $(KERNEL_ROOT)/tests/smoke/run_real_hw_smoke.sh
	@echo "✓ smoke-real-hw checklist generated"

smoke-all:
	@$(MAKE) -s smoke-qemu
	@$(MAKE) -s smoke-real-hw
	@echo "✓ smoke-all completed"

roadmap-phase1-stability:
	@echo "  ROADMAP phase1 stability gates"
	@$(MAKE) -s kernel-x64.bin
	@$(MAKE) -s tests
	@$(MAKE) -s kernel-tests
	@$(MAKE) -s kernel-memsafe
	@$(MAKE) -s kernel-analyze
	@$(MAKE) -s build-matrix-min
	@$(MAKE) -s arch-guard
	@$(MAKE) -s smoke-mm-cow-lazy
	@echo "✓ roadmap phase1 ready"

roadmap-phase2-driver-expansion: roadmap-phase1-stability
	@echo "  ROADMAP phase2 driver expansion gate"
	@$(MAKE) -s runtime-net-check
	@$(MAKE) -s runtime-mount-check
	@echo "✓ roadmap phase2 ready (modern net/storage driver work can proceed)"

.PHONY: smoke-p1-storage
smoke-p1-storage:
	@echo "  SMOKE   P1-storage bundle (FAT/EXT2/GPT/AHCI/NVMe)"
	@$(MAKE) -s smoke-fat16-mount
	@$(MAKE) -s smoke-ext2-mount
	@$(MAKE) -s smoke-gpt-partition
	@$(MAKE) -s smoke-ahci-read
	@$(MAKE) -s smoke-nvme-read
	@echo "✓ smoke-p1-storage passed"

roadmap-phase3-core-features: roadmap-phase2-driver-expansion
	@echo "  ROADMAP phase3 core feature gate"
	@echo "✓ roadmap phase3 ready (network/process semantic expansion can proceed)"

scale-readiness-gate:
	@$(MAKE) -s build-matrix-full
	@echo "✓ scale-readiness-gate passed"

.PHONY: test test-list test-run

test:
	@python3 scripts/test_tui.py

test-list:
	@python3 scripts/test_runner.py --list

test-run:
	@test -n "$(SUITE)" || (echo "usage: make test-run SUITE=host,ktest|ctr|tier1" >&2; exit 2)
	@python3 scripts/test_runner.py --suites "$(SUITE)" $(if $(filter 1,$(IR0_DEV_PERSIST)),--persist,)

# Deprecated FASE build aliases (init_fase* → ktm_* rename). Docs/scripts may still invoke these.
.PHONY: build-init-fase41-reclaim build-init-fase42-pt-reclaim build-init-fase42-exec-storm \
	build-init-fase42-fork-exit-storm build-init-fase43-fork-exit-storm \
	build-init-fase43-fork-wait-storm build-init-fase43-exec-loop \
	build-init-fase44-fork-wait-drain build-init-fase44-exec-drain \
	build-init-fase44-init-exit-drain build-init-fase45-fork-rollback-storm \
	build-init-fase45-fork-mem-touch build-init-fase46-fork-no-recursion \
	build-init-fase46-fork-heap build-init-fase48-ipc build-init-fase49-pipe \
	build-init-fase50-busybox build-init-fase50-exec-only build-init-fase50-programs \
	build-init-fase51-shell build-init-fase52-tcc build-init-fase53a-fs-dev \
	build-init-fase53b-posix-pseudofs build-init-fase54a-fbdev build-init-fase54b-input \
	build-init-fase54c-input-deterministic build-init-fase55a-doom-prereq \
	build-init-fase58c-boot-halt build-init-fase58c-fbdev \
	build-fase41-true 	build-fase48-busybox build-fase48-echo build-fase48-cat \
	build-fase48-ipc-bins build-fase50-hello build-fase58l-busybox-smoke \
	build-fase58c-boot-halt build-fase58c-fbdev build-fase55e-doom-interactive
build-init-fase41-reclaim: build-ktm-reclaim-exit-smoke
build-init-fase42-pt-reclaim: build-ktm-pt-reclaim-smoke
build-init-fase42-exec-storm: build-ktm-exec-storm-smoke
build-init-fase42-fork-exit-storm: build-ktm-fork-exit-storm-smoke
build-init-fase43-fork-exit-storm: build-ktm-fork-exit-storm-deep-smoke
build-init-fase43-fork-wait-storm: build-ktm-fork-wait-storm-smoke
build-init-fase43-exec-loop: build-ktm-exec-loop-smoke
build-init-fase44-fork-wait-drain: build-ktm-fork-wait-drain-smoke
build-init-fase44-exec-drain: build-ktm-exec-drain-smoke
build-init-fase44-init-exit-drain: build-ktm-init-exit-drain-smoke
build-init-fase45-fork-rollback-storm: build-ktm-fork-rollback-smoke
build-init-fase45-fork-mem-touch: build-ktm-fork-mem-touch-smoke
build-init-fase46-fork-no-recursion: build-ktm-fork-no-recursion-smoke
build-init-fase46-fork-heap: build-ktm-fork-heap-smoke
build-init-fase48-ipc: build-ktm-ipc-smoke
build-init-fase49-pipe: build-ktm-pipe-smoke
build-init-fase50-busybox: build-ktm-busybox-smoke
build-init-fase50-exec-only: build-ktm-exec-only-smoke
build-init-fase50-programs: build-ktm-programs-smoke
build-init-fase51-shell: build-ktm-shell-smoke
build-init-fase52-tcc: build-ktm-tcc-smoke
build-init-fase53a-fs-dev: build-ktm-fs-dev-smoke
build-init-fase53b-posix-pseudofs: build-ktm-posix-pseudofs-smoke
build-init-fase54a-fbdev: build-ktm-fbdev-smoke
build-init-fase54b-input: build-ktm-input-smoke
build-init-fase54c-input-deterministic: build-ktm-input-det-smoke
build-init-fase55a-doom-prereq: build-ktm-doom-prereq-smoke
build-init-fase58c-boot-halt: build-ktm-boot-halt-bin
build-init-fase58c-fbdev: build-ktm-fbdev-gui-bin
build-fase41-true: build-ktm-true-helper
build-fase48-busybox build-fase48-echo build-fase48-cat build-fase48-ipc-bins: build-ktm-ipc-helpers
build-fase50-hello: build-ktm-hello-helper
build-fase58l-busybox-smoke: build-ktm-busybox-manifest-smoke
build-fase58c-boot-halt: build-ktm-boot-halt-bin
build-fase58c-fbdev: build-ktm-fbdev-gui-bin
build-fase55e-doom-interactive: build-ktm-doom-interactive

ifdef IR0_LEGACY_SMOKE
include setup/make/legacy-smokes.mk
endif
