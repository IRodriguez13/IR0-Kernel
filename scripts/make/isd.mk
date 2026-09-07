# SPDX-License-Identifier: GPL-3.0-only
#
# ISD bridge — product distribution lives in the sibling ISD repo.
# Included from the IR0 Makefile (index only; recipes stay here).
#
# Public targets:
#   check-isd clone-isd isd-defconfig isdconfig
#   isd isd-rootfs isd-image isd-clean first-boot
#
# Canonical interface: PROFILE=minimal|development|desktop|appliance
# Compat: IR0_PRODUCT_PROFILE, IR0_USERSPACE_ROOT/URL, bootstrap-userspace

ifndef _IR0_ISD_MK
_IR0_ISD_MK := 1

# --- paths / URLs ------------------------------------------------------------

# Defaults (origin → file when unset). CLI/env IR0_ISD_* already win over ?=.
IR0_ISD_ROOT ?= $(abspath $(KERNEL_ROOT)/../ISD)
IR0_ISD_URL  ?= https://github.com/IRodriguez13/ISD.git

# Capture before we rewrite aliases (for deprecation notes).
_IR0_USERSPACE_ROOT_ORIGIN := $(origin IR0_USERSPACE_ROOT)
_IR0_USERSPACE_URL_ORIGIN := $(origin IR0_USERSPACE_URL)

# Deprecated IR0_USERSPACE_* aliases — keep in sync for CLI and environment.
# Priority: CLI IR0_ISD_* > CLI IR0_USERSPACE_* > env IR0_ISD_* >
#           env IR0_USERSPACE_* > file default above.
ifeq ($(origin IR0_USERSPACE_ROOT),command line)
  ifneq ($(origin IR0_ISD_ROOT),command line)
    IR0_ISD_ROOT := $(IR0_USERSPACE_ROOT)
  endif
else ifeq ($(origin IR0_USERSPACE_ROOT),environment)
  ifeq ($(origin IR0_ISD_ROOT),file)
    IR0_ISD_ROOT := $(IR0_USERSPACE_ROOT)
  endif
endif
ifeq ($(origin IR0_USERSPACE_URL),command line)
  ifneq ($(origin IR0_ISD_URL),command line)
    IR0_ISD_URL := $(IR0_USERSPACE_URL)
  endif
else ifeq ($(origin IR0_USERSPACE_URL),environment)
  ifeq ($(origin IR0_ISD_URL),file)
    IR0_ISD_URL := $(IR0_USERSPACE_URL)
  endif
endif
# Legacy recipes still read IR0_USERSPACE_*; always alias to the ISD path.
IR0_USERSPACE_ROOT := $(IR0_ISD_ROOT)
IR0_USERSPACE_URL := $(IR0_ISD_URL)

ISD_ARCH ?= x86_64

# Resolve ISD product profile without clobbering kernel deptest PROFILE defaults.
# Command-line PROFILE=minimal|development|desktop|appliance → ISD profile.
# Otherwise IR0_PRODUCT_PROFILE, else minimal.
ISD_PROFILE := minimal
ifdef IR0_PRODUCT_PROFILE
  ISD_PROFILE := $(IR0_PRODUCT_PROFILE)
endif
ifeq ($(origin PROFILE),command line)
  ifneq ($(filter minimal development desktop appliance,$(PROFILE)),)
    ISD_PROFILE := $(PROFILE)
  endif
endif
# Keep IR0_PRODUCT_PROFILE in sync for scripts that still read it.
IR0_PRODUCT_PROFILE := $(ISD_PROFILE)
export IR0_PRODUCT_PROFILE

IR0_ISD_MAKE = $(MAKE) -C "$(IR0_ISD_ROOT)" \
	IR0_ROOT="$(KERNEL_ROOT)" \
	ARCH="$(ISD_ARCH)" \
	PROFILE="$(ISD_PROFILE)"

# Disk owned by ISD (not copied into IR0/ by default)
IR0_ISD_DISK = $(IR0_ISD_ROOT)/out/$(ISD_ARCH)/images/$(ISD_PROFILE)/disk.img

# Mutable guest state lives outside ISD out/. Rebuilding a package or rootfs may
# recreate IR0_ISD_DISK, but must never overwrite an installed machine.
IR0_MACHINE ?= default
IR0_MACHINE_ROOT ?= $(abspath $(IR0_ISD_ROOT)/../IR0-machines)
IR0_MACHINE_DIR = $(IR0_MACHINE_ROOT)/$(ISD_ARCH)/$(ISD_PROFILE)/$(IR0_MACHINE)
IR0_MACHINE_DISK = $(IR0_MACHINE_DIR)/disk.img
IR0_MACHINE_VMDK = $(IR0_MACHINE_DIR)/ir0-$(ISD_PROFILE).vmdk

IR0_USERSPACE_OUT = $(IR0_ISD_ROOT)/out
IR0_USERSPACE_MAKE = $(MAKE) -s -C $(IR0_ISD_ROOT) IR0_ROOT=$(KERNEL_ROOT) ARCH=$(ISD_ARCH)

.PHONY: check-isd clone-isd isd-defconfig isdconfig isd isd-rootfs isd-image \
	isd-clean first-boot bootstrap-userspace check-userspace \
	warn-userspace-deprecated ensure-isd-disk run-isd machine-create \
	machine-reset machine-info machine-update-kernel image-vmware poweron

warn-userspace-deprecated:
	@case "$(_IR0_USERSPACE_ROOT_ORIGIN)" in \
		command\ line|environment) \
			echo "note: IR0_USERSPACE_ROOT is deprecated; use IR0_ISD_ROOT=$(IR0_ISD_ROOT)" ;; \
	esac
	@case "$(_IR0_USERSPACE_URL_ORIGIN)" in \
		command\ line|environment) \
			echo "note: IR0_USERSPACE_URL is deprecated; use IR0_ISD_URL=$(IR0_ISD_URL)" ;; \
	esac

check-isd:
	@if [ ! -f "$(IR0_ISD_ROOT)/Makefile" ]; then \
		echo "✗ ISD not found at $(IR0_ISD_ROOT)"; \
		echo "  First time:  make first-boot PROFILE=$(ISD_PROFILE)"; \
		echo "  Or clone:    make clone-isd"; \
		echo "  Or set:      IR0_ISD_ROOT=/path/to/ISD"; \
		exit 1; \
	fi
	@case "$(ISD_ARCH)" in \
		x86_64|x86-64) ;; \
		*) echo "✗ first-boot/ISD product path currently supports ISD_ARCH=x86_64 (got $(ISD_ARCH))"; exit 1 ;; \
	esac

clone-isd:
	@if [ -f "$(IR0_ISD_ROOT)/Makefile" ]; then \
		echo "  CLONE    ISD already present at $(IR0_ISD_ROOT)"; \
	else \
		parent="$(dir $(IR0_ISD_ROOT))"; \
		mkdir -p "$$parent"; \
		echo "  CLONE    $(IR0_ISD_URL) → $(IR0_ISD_ROOT)"; \
		git clone --depth 1 "$(IR0_ISD_URL)" "$(IR0_ISD_ROOT)"; \
	fi

isd-defconfig: check-isd
	+@$(IR0_ISD_MAKE) isd-defconfig

# '+' forwards the jobserver; keep the caller's TTY for the interactive menu.
isdconfig: check-isd
	+@$(IR0_ISD_MAKE) isdconfig

isd: check-isd
	+@$(IR0_ISD_MAKE) build

isd-rootfs: check-isd
	+@$(IR0_ISD_MAKE) rootfs-tree

isd-image: check-isd
	+@$(IR0_ISD_MAKE) image-minix
	@echo "✓ isd-image $(IR0_ISD_DISK)"

isd-clean: check-isd
	+@$(IR0_ISD_MAKE) clean

# Alias: old check-userspace name
check-userspace: check-isd

# Ensure per-PROFILE disk is up to date with stamps / .isdconfig.
# Always runs ISD image-minix (incremental): rebuilds when config or packages change.
ensure-isd-disk: check-isd
	@echo "ISD disk    PROFILE=$(ISD_PROFILE) → $(IR0_ISD_DISK)"
	@echo "            (first pack or format-large can take 1–3 min; stamps skip work when clean)"
	+@$(IR0_ISD_MAKE) fetch
	+@$(IR0_ISD_MAKE) image-minix
	@test -f "$(IR0_ISD_DISK)" || { \
		echo "✗ failed to create $(IR0_ISD_DISK)"; \
		exit 1; \
	}
	@echo "  DISK     $(IR0_ISD_DISK)"

# Deprecated alias → new bootstrap
bootstrap-userspace:
	@echo "note: bootstrap-userspace is deprecated; use make first-boot PROFILE=$(ISD_PROFILE)"
	+@$(MAKE) first-boot PROFILE=$(ISD_PROFILE)

# '+' so nested make -C ISD inside bootstrap inherits the jobserver.
first-boot:
	+@chmod +x "$(KERNEL_ROOT)/scripts/bootstrap-isd.sh"
	+@PROFILE="$(ISD_PROFILE)" \
		IR0_PRODUCT_PROFILE="$(ISD_PROFILE)" \
		IR0_ISD_ROOT="$(IR0_ISD_ROOT)" \
		IR0_ISD_URL="$(IR0_ISD_URL)" \
		IR0_USERSPACE_ROOT="$(IR0_ISD_ROOT)" \
		IR0_USERSPACE_URL="$(IR0_ISD_URL)" \
		ISD_ARCH="$(ISD_ARCH)" \
		"$(KERNEL_ROOT)/scripts/bootstrap-isd.sh"
	+@$(MAKE) -s machine-create PROFILE=$(ISD_PROFILE) IR0_MACHINE=$(IR0_MACHINE)
	@echo "  POWERON  make poweron PROFILE=$(ISD_PROFILE) IR0_MACHINE=$(IR0_MACHINE)"

machine-create: ensure-isd-disk
	@chmod +x "$(KERNEL_ROOT)/scripts/isd_machine_disk.sh"
	@IR0_MACHINE_BASE_DISK="$(IR0_ISD_DISK)" \
		IR0_MACHINE_DISK="$(IR0_MACHINE_DISK)" \
		"$(KERNEL_ROOT)/scripts/isd_machine_disk.sh" create

machine-reset: ensure-isd-disk
	@chmod +x "$(KERNEL_ROOT)/scripts/isd_machine_disk.sh"
	@IR0_MACHINE_BASE_DISK="$(IR0_ISD_DISK)" \
		IR0_MACHINE_DISK="$(IR0_MACHINE_DISK)" \
		CONFIRM_RESET="$(CONFIRM_RESET)" \
		"$(KERNEL_ROOT)/scripts/isd_machine_disk.sh" reset

machine-info:
	@echo "PROFILE       $(ISD_PROFILE)"
	@echo "MACHINE       $(IR0_MACHINE)"
	@echo "BASE DISK     $(IR0_ISD_DISK)"
	@echo "MACHINE DISK  $(IR0_MACHINE_DISK)"
	@echo "VMWARE DISK   $(IR0_MACHINE_VMDK)"

# Refresh only the boot ISO. The mutable machine disk is never a dependency.
machine-update-kernel: check-isd
	@if pgrep -f '^qemu-system-x86_64 .*$(IR0_MACHINE_DISK)' >/dev/null 2>&1; then \
		echo "✗ machine $(IR0_MACHINE) is running; power it off cleanly first"; \
		exit 2; \
	fi
	+@$(MAKE) -s kernel-x64-userspace.iso
	@echo "✓ machine kernel updated: $(KERNEL_ROOT)/kernel-x64-userspace.iso"
	@echo "  DISK preserved: $(IR0_MACHINE_DISK)"

image-vmware:
	@chmod +x "$(KERNEL_ROOT)/scripts/isd_machine_disk.sh"
	@IR0_MACHINE_BASE_DISK="$(IR0_ISD_DISK)" \
		IR0_MACHINE_DISK="$(IR0_MACHINE_DISK)" \
		IR0_MACHINE_VMDK="$(IR0_MACHINE_VMDK)" FORCE="$(FORCE)" \
		"$(KERNEL_ROOT)/scripts/isd_machine_disk.sh" export-vmdk
	@echo "  Attach $(KERNEL_ROOT)/kernel-x64-userspace.iso as the boot CD."

# Product session: boot existing artifacts only. first-boot/machine-create are
# the explicit provisioning paths; rebuilding the kernel remains explicit.
poweron: check-isd
	@test -f "$(KERNEL_ROOT)/kernel-x64-userspace.iso" || { \
		echo "✗ missing $(KERNEL_ROOT)/kernel-x64-userspace.iso"; \
		echo "  Run make first-boot PROFILE=$(ISD_PROFILE) first."; \
		exit 2; \
	}
	@chmod +x "$(KERNEL_ROOT)/scripts/isd_machine_disk.sh"
	@IR0_MACHINE_BASE_DISK="$(IR0_ISD_DISK)" \
		IR0_MACHINE_DISK="$(IR0_MACHINE_DISK)" \
		"$(KERNEL_ROOT)/scripts/isd_machine_disk.sh" create
	@echo "Running persistent IR0 + ISD machine ($(IR0_MACHINE))"
	@echo "  DISK     $(IR0_MACHINE_DISK)"
	qemu-system-x86_64 -cdrom kernel-x64-userspace.iso \
		-drive "file=$(IR0_MACHINE_DISK),format=raw,if=ide,index=0" \
		$(QEMU_NET_ALL) $(QEMU_AUDIO_ALL) $(QEMU_SERIAL_COM1) $(QEMU_ISA_DEBUG_EXIT) \
		$(QEMU_DENNIS_9P) \
		-m 512M -no-reboot \
		$(QEMU_DISPLAY)

# Product run: boot ISD-owned disk for PROFILE (no per-binary inject).
# Auto-builds the disk if missing (e.g. after ISD make clean).
run-isd: kernel-x64-userspace.iso ensure-isd-disk
	@echo "Running IR0 + ISD (PROFILE=$(ISD_PROFILE))"
	@echo "  DISK     $(IR0_ISD_DISK)"
	@echo "  ISO      kernel-x64-userspace.iso"
	@echo "  Ungrab:  Ctrl+Alt+G"
	@echo "  Clipboard: make clip-send (host) → ir0-paste in guest (9p dennis)"
	@echo "  Guest:   first-boot wizard or login (development = autologin root)"
	qemu-system-x86_64 -cdrom kernel-x64-userspace.iso \
		-drive file=$(IR0_ISD_DISK),format=raw,if=ide,index=0 \
		$(QEMU_NET_ALL) $(QEMU_AUDIO_ALL) $(QEMU_SERIAL_COM1) $(QEMU_ISA_DEBUG_EXIT) \
		$(QEMU_DENNIS_9P) \
		-m 512M -no-reboot \
		$(QEMU_DISPLAY)

endif
