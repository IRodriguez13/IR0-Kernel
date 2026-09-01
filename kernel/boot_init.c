/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: boot_init.c
 * Description: Boot-phase implementations; kmain only sequences these calls.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/vga.h>
#include <ir0/oops.h>
#include <ir0/logging.h>
#include <ir0/cmdline.h>
#include <ir0/boot_log_hostshare.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <ir0/kmem.h>
#include <mm/pmm.h>
#include <mm/paging.h>
#include <init.h>
#include <ir0/arch_port.h>
#include <config.h>
#include <ir0/version.h>
#include <ir0/driver.h>
#include <kernel/elf_loader.h>
#include <ir0/clock.h>
#include <ir0/init_drv.h>
#include <ir0/blockdev.h>
#include <ir0/video_backend.h>
#include <ir0/console_backend.h>
#include <ir0/console.h>
#include <ir0/multiboot.h>
#include <ir0/ktm/ktm.h>
#include <ir0/boot_log.h>
#include <ir0/vfs.h>
#include <ir0/open_flags.h>
#include <ir0/stat.h>
#include "ipc.h"
#include "syscalls.h"
#include <ir0/sched.h>
#include "process.h"
#include "rootfs_base.h"
#include "kernel.h"
#include "boot_init.h"

void boot_early(uint32_t multiboot_info)
{
#if !CONFIG_ENABLE_VBE
	(void)multiboot_info;
#endif

	set_boot_params((void *)(uintptr_t)multiboot_info);
	ir0_cmdline_apply_log_profile();
	early_init();
	heap_init();

	/*
	 * VBE framebuffer from Multiboot. Must run before first print() so
	 * that print() uses framebuffer when gfxpayload=1024x768x32 in grub.
	 */
#if CONFIG_ENABLE_VBE
	if (video_backend_init_from_multiboot(multiboot_info) != 0)
		video_backend_init_fallback();
#endif
	console_backend_init();
	if (console_backend_uses_framebuffer())
		console_backend_clear(0x0F);
}

void boot_memory_serial(uint32_t multiboot_info)
{
	/*
	 * Physical Memory Manager: frames in [32MB, 512MB).
	 * Heap occupies [8MB, 32MB); PMM must stay disjoint.
	 */
	pmm_init(PMM_PHYS_BASE, PMM_PHYS_SIZE);

	/*
	 * Pin boot CR3 while still on the identity map from asm bootstrap.
	 * COW/clear_highpage switch here after process tables use pte_none
	 * holes (Linux kmap / copy_user_highpage analogue).
	 */
	paging_pin_kernel_cr3(get_current_page_directory());

	logging_init();
	(void)klog_promote_normal_ring();
	ir0_driver_registry_init();
	serial_init();

	klog_set_boot_phase(KLOG_BOOT_EARLY_ARCH);
	ir0_boot_serial_ready();
	ir0_boot_arch(
#if defined(__x86_64__)
		"x86_64 bootstrap entry"
#elif defined(__aarch64__)
		"aarch64 bootstrap entry"
#else
		"bootstrap entry"
#endif
	);
	ir0_boot_info("SMP", "UP (1 CPU online)");

	klog_set_boot_phase(KLOG_BOOT_MEMORY);
	klog_info("PMM", "physical memory manager online");
	klog_set_boot_phase(KLOG_BOOT_PLATFORM);
	{
		char hv_vendor[16];

		if (hypervisor_present() &&
		    hypervisor_vendor(hv_vendor, sizeof(hv_vendor)) == 0)
		{
			if (strncmp(hv_vendor, "TCGTCGTCGTCG", 12) == 0)
				ir0_boot_info("HYPERVISOR",
					      "detected vendor=TCG (QEMU TCG)");
			else if (strncmp(hv_vendor, "KVMKVMKVM", 9) == 0)
				ir0_boot_info("HYPERVISOR",
					      "detected vendor=KVM");
			else
				log_info_fmt("HYPERVISOR", "detected vendor=%s",
					     hv_vendor);
		}
		else
		{
			ir0_boot_info("HYPERVISOR", "none detected");
			ir0_boot_notice("PLATFORM",
					"Bare-metal execution confirmed!");
		}
	}

#if CONFIG_ENABLE_VBE
	{
		uint32_t w = 0, h = 0, bpp = 0;

		if (console_backend_uses_framebuffer() &&
		    video_backend_get_info(&w, &h, &bpp))
		{
			log_info_fmt("BOOT", "Console: framebuffer %ux%ux%u",
				     (unsigned)w, (unsigned)h, (unsigned)bpp);
		}
		else
		{
#if DEBUG_BOOT
			log_info_fmt(
				"BOOT",
				"Console: VGA text (80x25)%s vbe_fail_reason=%u",
				video_backend_is_available()
					? " [vbe fallback - may not be visible in graphics mode]"
					: "",
				(unsigned)video_backend_fail_reason());

			if (multiboot_info)
			{
				const struct multiboot_info *mb =
					(const struct multiboot_info *)(uintptr_t)
						multiboot_info;
				log_info_fmt(
					"BOOT",
					"Multiboot flags=0x%x (bit12=FB:%u) addr=0x%x w=%u h=%u bpp=%u",
					(unsigned)mb->flags,
					(mb->flags & (1u << 12)) ? 1u : 0u,
					(unsigned)(mb->framebuffer_addr &
						   0xFFFFFFFFu),
					(unsigned)mb->framebuffer_width,
					(unsigned)mb->framebuffer_height,
					(unsigned)mb->framebuffer_bpp);
			}
			else
				log_info("BOOT", "Multiboot info is NULL");
#else
			(void)multiboot_info;
#endif
		}
	}
#else
	(void)multiboot_info;
#endif

	log_subsystem_ok("CORE");
}

void boot_drivers_rootfs(void)
{
	klog_set_boot_phase(KLOG_BOOT_DRIVERS);
	init_all_drivers();

	klog_set_boot_phase(KLOG_BOOT_STORAGE);
	if (!ir0_block_name_is_present(CONFIG_ROOT_BLOCK_DEVICE))
	{
		log_warn("BOOT", "Configured root block device not detected");
		log_warn("BOOT", "Filesystem initialization may fail");
	}
#if DEBUG_BOOT
	else
	{
		log_info("BOOT",
			 "Configured root block device detected, proceeding with filesystem init");
	}
#endif

	klog_set_boot_phase(KLOG_BOOT_ROOTFS);
	vfs_init_root();
	log_subsystem_ok("FILESYSTEM");

	if (!hypervisor_present())
	{
		stat_t st;
		struct vfs_file *f = NULL;

		if (vfs_stat("/etc/ir0-baremetal-booted", &st) == 0)
		{
			klog_info("BOOT", "bare-metal milestone already recorded");
			klog_smoke("FIRST_BAREMETAL_BOOT_SKIP");
		}
		else if (vfs_open("/etc/ir0-baremetal-booted",
				  IR0_O_WRONLY | IR0_O_CREAT | IR0_O_TRUNC, 0644,
				  &f) == 0)
		{
			static const char mark[] = "IR0 first bare-metal boot\n";

			(void)vfs_write(f, mark, sizeof(mark) - 1);
			(void)vfs_close(f);
			klog_event(KLOG_EVENT_FIRST_BAREMETAL_BOOT, 0,
				   KLOG_LEVEL_NOTICE, "BOOT",
				   "first bare-metal boot recorded");
			klog_smoke("FIRST_BAREMETAL_BOOT");
		}
		else
		{
			klog_warn("BOOT",
				  "bare-metal milestone sentinel write failed");
			klog_smoke("FIRST_BAREMETAL_BOOT_FAIL");
		}
	}
}

void boot_runtime(void)
{
	process_init();
	log_subsystem_ok("PROCESSES");

	ipc_init();
	log_subsystem_ok("IPC");

	klog_set_boot_phase(KLOG_BOOT_TIME);
	clock_system_init();
	klog_info("CLOCK", "monotonic clock online");
#if CONFIG_SCHEDULER_POLICY == 2
	{
		extern int priority_sched_selftest(void);

		if (priority_sched_selftest() == 0)
			klog_smoke("SCHED_PRIO_OK");
		else
			klog_smoke("SCHED_PRIO_FAIL");
	}
#endif
	klog_info_fmt("SCHED", "SCHED_POLICY=%s", sched_active_policy_name());

	syscall_init();
	syscalls_init();
	log_subsystem_ok("SYSCALLS");

	klog_set_boot_phase(KLOG_BOOT_INTERRUPTS);
	irq_init();
	boot_irq_unmask();
	enable_interrupts();
#if DEBUG_BOOT
	log_info("BOOT", "Interrupts enabled globally (sti)");
#endif
	log_subsystem_ok("INTERRUPTS");

	klog_set_boot_phase(KLOG_BOOT_READY);
	klog_notice("BOOT", "kernel core initialization complete");
}

void boot_diagnostics(void)
{
#if CONFIG_KTM
	ktm_core_init();
	KTM_CHECKPOINT(KTM_CP_BOOT_READY);
#if CONFIG_KTM_TEST
	ktm_scenarios_run_boot();
#endif
	klog_notice("BOOT", "KTM validation complete");
#endif

	(void)ir0_boot_log_hostshare_try();

#if defined(IR0_KERNEL_TESTS)
	{
		extern void kernel_test_run_all(void);
		process_t *saved = current_process;
		process_t *holder;
		pid_t holder_pid;

		holder_pid = spawn_kernel(kernel_idle_loop, "ktest-ctx");
		holder = (holder_pid > 0) ? process_find_by_pid(holder_pid)
					 : NULL;
		if (holder)
		{
			current_process = holder;
			kernel_test_run_all();
			current_process = saved;
		}
		else
			klog_error("KTEST", "no process context; suite skipped");
	}
#endif
}

void boot_enter_userspace(void)
{
	pid_t init_pid;
	char *argv_init[] = { "/sbin/init", NULL };

	klog_set_boot_phase(KLOG_BOOT_USERSPACE);
	klog_notice("BOOT", "system ready for userspace");
	klog_event(KLOG_EVENT_USERSPACE_HANDOFF, 0, KLOG_LEVEL_INFO, "INIT",
		   "exec /sbin/init");
	ir0_rootfs_prepare_userspace_base();
	process_prepare_pid1_for_init();
	init_pid = kexecve("/sbin/init", argv_init, NULL);
	if (init_pid < 0)
	{
		log_error("BOOT", "FAILED to load /sbin/init");
		panic("Failed to load /sbin/init");
	}
	klog_info_fmt("INIT", "/sbin/init loaded (PID %d), scheduling",
		      init_pid);

	if (spawn_kernel(kernel_idle_loop, "idle") < 0)
		klog_notice("BOOT",
			    "idle task spawn failed; UP idle heuristic only");

	ir0_console_on_userspace_attach();
	sched_schedule_next();
	panic("sched_schedule_next returned after userspace init");
}
