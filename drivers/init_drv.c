/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: init_drv.c
 * Description: IR0 kernel source/header file
 */

/**
 * IR0 Kernel — Driver Initialization
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: drivers/init_drv.c
 * Description: Multi-language driver initialization and hardware driver init
 */

#include "init_drv.h"
#include <ir0/driver_bootstrap.h>
#include <ir0/driver.h>
#include <ir0/logging.h>
#include <ir0/ktm/klog.h>
#include <string.h>
#include <config.h>
#include <drivers/multilang_drivers.h>
#include <drivers/IO/ps2.h>
#include <ir0/irq.h>
#if CONFIG_ENABLE_PC_SPEAKER
#include <drivers/IO/pc_speaker.h>
#endif
#if CONFIG_ENABLE_STORAGE_ATA
#include <drivers/storage/ata.h>
#endif
#include <drivers/storage/ahci.h>
#include <drivers/storage/nvme.h>
#include <ir0/block_dev.h>
#include <drivers/serial/serial.h>
#include <ir0/resource_registry.h>

#if CONFIG_ENABLE_MOUSE
#include <drivers/IO/ps2_mouse.h>
#endif

#if CONFIG_ENABLE_SOUND
#include <drivers/audio/sound_blaster.h>
#include <drivers/audio/adlib.h>
#include <drivers/dma/dma.h>
#endif

#if CONFIG_ENABLE_BLUETOOTH
#include <ir0/bluetooth.h>
#endif

#if CONFIG_ENABLE_USB_HOST
#include <ir0/usb_host.h>
#endif

#include <ir0/virtio_9p.h>

#if CONFIG_ENABLE_NETWORKING
#include <ir0/net.h>
#endif

static int g_registry_ready = 0;
static int g_bootstrap_done = 0;

#if CONFIG_ENABLE_STORAGE_ATA_BLOCK && CONFIG_INIT_STORAGE_ATA_BLOCK
void ata_block_register(void);
#endif

static int boot_init_ps2_controller(void)
{
#if CONFIG_INIT_PS2_CONTROLLER
    ps2_init();
#endif
    return 0;
}

static int boot_init_keyboard(void)
{
#if CONFIG_INIT_PS2_CONTROLLER
    irq_keyboard_init();
#endif
    return 0;
}

static int boot_init_mouse(void)
{
#if CONFIG_ENABLE_MOUSE && CONFIG_INIT_MOUSE_DRIVER
    ps2_mouse_init();
#endif
    return 0;
}

static int boot_init_pc_speaker(void)
{
#if CONFIG_ENABLE_PC_SPEAKER && CONFIG_INIT_PC_SPEAKER
    pc_speaker_init();
#endif
    return 0;
}

static int boot_init_sound(void)
{
#if CONFIG_ENABLE_SOUND && CONFIG_INIT_SOUND_DRIVERS
    sb16_init();
    adlib_init();
    resource_register_ioport(DMA1_PORT_START, DMA1_PORT_END, "dma1");
#endif
    return 0;
}

static int boot_init_storage_ata(void)
{
#if CONFIG_ENABLE_STORAGE_ATA && CONFIG_INIT_STORAGE_ATA
    ata_init();
#endif
    return 0;
}

static int boot_init_storage_block(void)
{
#if CONFIG_ENABLE_STORAGE_ATA_BLOCK && CONFIG_INIT_STORAGE_ATA_BLOCK
    ata_block_register();
#endif
    ahci_probe();
    nvme_probe();
    return 0;
}

static int boot_init_network(void)
{
#if CONFIG_ENABLE_NETWORKING && CONFIG_INIT_NETWORK_STACK
    return init_net_stack();
#else
    return 0;
#endif
}

static int boot_init_bluetooth(void)
{
#if CONFIG_ENABLE_BLUETOOTH && CONFIG_INIT_BLUETOOTH_DRIVER
    return ir0_bluetooth_register_driver();
#else
    return 0;
#endif
}

static int boot_init_usb_host(void)
{
#if CONFIG_ENABLE_USB_HOST && CONFIG_INIT_USB_HOST
    return ir0_usb_host_init();
#else
    return 0;
#endif
}

static int boot_init_hostshare_9p(void)
{
	/* Optional: absent without QEMU -virtfs; non-fatal for normal boots. */
	(void)virtio_9p_init();
	return 0;
}

static void register_bootstrap_plan(void)
{
    driver_bootstrap_reset();
    driver_bootstrap_register(DRIVER_BOOT_STAGE_INPUT, "ps2_controller", boot_init_ps2_controller,
                              CONFIG_INIT_PS2_CONTROLLER);
    driver_bootstrap_register(DRIVER_BOOT_STAGE_INPUT, "ps2_keyboard", boot_init_keyboard,
                              CONFIG_INIT_PS2_CONTROLLER);
    driver_bootstrap_register(DRIVER_BOOT_STAGE_INPUT, "ps2_mouse", boot_init_mouse,
                              (CONFIG_ENABLE_MOUSE && CONFIG_INIT_MOUSE_DRIVER));
    driver_bootstrap_register(DRIVER_BOOT_STAGE_PLATFORM, "pc_speaker", boot_init_pc_speaker,
                              (CONFIG_ENABLE_PC_SPEAKER && CONFIG_INIT_PC_SPEAKER));
    driver_bootstrap_register(DRIVER_BOOT_STAGE_PLATFORM, "hostshare_9p", boot_init_hostshare_9p,
			      1);
    driver_bootstrap_register(DRIVER_BOOT_STAGE_STORAGE, "ata_core", boot_init_storage_ata,
                              (CONFIG_ENABLE_STORAGE_ATA && CONFIG_INIT_STORAGE_ATA));
    driver_bootstrap_register(DRIVER_BOOT_STAGE_STORAGE, "ata_block", boot_init_storage_block,
                              (CONFIG_ENABLE_STORAGE_ATA_BLOCK && CONFIG_INIT_STORAGE_ATA_BLOCK));
    driver_bootstrap_register(DRIVER_BOOT_STAGE_AUDIO, "sound_stack", boot_init_sound,
                              (CONFIG_ENABLE_SOUND && CONFIG_INIT_SOUND_DRIVERS));
    driver_bootstrap_register(DRIVER_BOOT_STAGE_NETWORK, "network_stack", boot_init_network,
                              (CONFIG_ENABLE_NETWORKING && CONFIG_INIT_NETWORK_STACK));
    driver_bootstrap_register(DRIVER_BOOT_STAGE_NETWORK, "bluetooth_stack", boot_init_bluetooth,
                              (CONFIG_ENABLE_BLUETOOTH && CONFIG_INIT_BLUETOOTH_DRIVER));
    driver_bootstrap_register(DRIVER_BOOT_STAGE_PLATFORM, "usb_host", boot_init_usb_host,
                              (CONFIG_ENABLE_USB_HOST && CONFIG_INIT_USB_HOST));
}

/**
 * Initialize multi-language driver subsystem.
 * Called during kernel boot after heap is initialized.
 */
static void driver_registry_prepare(void)
{
    if (g_registry_ready)
        return;

    if (!ir0_driver_registry_is_initialized())
        ir0_driver_registry_init();
    log_subsystem_ok("DRIVER_REGISTRY");

#if KERNEL_ENABLE_EXAMPLE_DRIVERS
    register_multilang_example_drivers();
    log_subsystem_ok("MULTI_LANG_DRIVERS");
#endif

    g_registry_ready = 1;
}


/**
 * init_all_drivers - Inicializa todos los drivers de hardware.
 *
 * Llama cada init directamente en el orden que necesita el kernel.
 * IRQ y recursos se registran tras los inits.
 */
void init_all_drivers(void)
{
    if (g_bootstrap_done)
        return;

    LOG_INFO("DRIVERS", "Initializing all hardware drivers...");
    driver_registry_prepare();
    register_bootstrap_plan();
    if (driver_bootstrap_run_all() != 0)
        LOG_WARNING("DRIVERS", "One or more selectable boot drivers failed");

    log_subsystem_ok("PS2_KEYBOARD");
#if CONFIG_ENABLE_MOUSE && CONFIG_INIT_MOUSE_DRIVER
    log_subsystem_ok("PS2_MOUSE");
#endif
#if CONFIG_ENABLE_NETWORKING && CONFIG_INIT_NETWORK_STACK
    log_subsystem_ok("NETWORK_STACK");
#endif
#if CONFIG_ENABLE_BLUETOOTH && CONFIG_INIT_BLUETOOTH_DRIVER
    log_subsystem_ok("BLUETOOTH_STACK");
#endif
#if CONFIG_ENABLE_USB_HOST && CONFIG_INIT_USB_HOST
    log_subsystem_ok("USB_HOST");
#endif
    g_bootstrap_done = 1;
    {
	unsigned ready = 0, absent = 0, deferred = 0;
	unsigned unsupported = 0, failed = 0;
	char summary[96];

	ir0_driver_boot_emit_probe_results();
	ir0_driver_boot_counts(&ready, &absent, &deferred, &unsupported,
			       &failed);
	snprintf(summary, sizeof(summary),
		 "summary ready=%u absent=%u deferred=%u unsupported=%u failed=%u",
		 ready, absent, deferred, unsupported, failed);
	klog_event(KLOG_EVENT_DRIVER_SUMMARY, 0, KLOG_LEVEL_NOTICE, "DRIVERS",
		   summary);
	klog_smoke("DRIVER_SUMMARY_OK");
    }
}

void drivers_init(void)
{
    /*
     * Compat entrypoint:
     * keep existing callers working while centralizing all init in init_all_drivers().
     */
    init_all_drivers();
}

