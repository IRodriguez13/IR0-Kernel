/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: userdev.c
 * Description: /dev/ktm control plane — ioctl / read / poll.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ktm_internal.h>
#include <config.h>
#include <ir0/copy_user.h>
#include <ir0/devfs.h>
#include <ir0/errno.h>
#include <ir0/ktm/uapi.h>
#include <ir0/ktm/userdev.h>
#include <ir0/serial_io.h>
#include <string.h>
#include <ir0/ktm/klog.h>

#if defined(CONFIG_KTM_USERDEV) && CONFIG_KTM_USERDEV

#define KTM_DEV_ID 48u

static int g_registered;

static int64_t ktm_dev_read(devfs_entry_t *entry, void *buf, size_t count,
			    off_t offset)
{
	ktm_event_t local[16];
	size_t max_ev;
	int n;

	(void)entry;
	(void)offset;
	if (!buf || count < sizeof(ktm_event_t))
		return 0;
	max_ev = count / sizeof(ktm_event_t);
	if (max_ev > 16)
		max_ev = 16;
	n = ktm_event_copy_out(local, max_ev);
	if (n <= 0)
		return 0;
	memcpy(buf, local, (size_t)n * sizeof(ktm_event_t));
	return (int64_t)((size_t)n * sizeof(ktm_event_t));
}

static int ktm_dev_can_read(devfs_entry_t *entry, pid_t pid)
{
	(void)entry;
	(void)pid;
	return ktm_event_pending();
}

static int64_t ktm_ioc_run_scenario(void *arg)
{
	ktm_user_scenario_t sc;
	int ret;

	if (!arg)
		return -EINVAL;
	if (copy_from_user(&sc, arg, sizeof(sc)) != 0)
		return -EFAULT;
	sc.name[sizeof(sc.name) - 1] = '\0';
	ret = ktm_scenario_run(sc.name);
	sc.result = ret;
	if (copy_to_user(arg, &sc, sizeof(sc)) != 0)
		return -EFAULT;
	return ret < 0 ? ret : 0;
}

static int64_t ktm_ioc_run_invariants(void *arg)
{
	ktm_user_invariants_t inv;
	int ret;

	if (!arg)
		return -EINVAL;
	if (copy_from_user(&inv, arg, sizeof(inv)) != 0)
		return -EFAULT;
	ret = ktm_invariants_run(inv.mask ? inv.mask : KTM_INV_ALL);
	inv.result = ret;
	if (copy_to_user(arg, &inv, sizeof(inv)) != 0)
		return -EFAULT;
	return ret < 0 ? ret : 0;
}

static int64_t ktm_ioc_take_snapshot(void *arg)
{
	ktm_ioc_snapshot_t ioc;
	ktm_system_snapshot_t snap;

	if (!arg)
		return -EINVAL;
	if (copy_from_user(&ioc, arg, sizeof(ioc)) != 0)
		return -EFAULT;
	if (ktm_snapshot_take(&snap) != 0)
		return -EIO;
	ioc.total_frames = snap.total_frames;
	ioc.used_frames = snap.used_frames;
	ioc.free_frames = snap.free_frames;
	ioc.processes = snap.processes;
	ioc.zombies = snap.zombies;
	ioc.open_fds = snap.open_fds;
	ioc.pipes = snap.pipes;
	if (copy_to_user(arg, &ioc, sizeof(ioc)) != 0)
		return -EFAULT;
	return 0;
}

static int64_t ktm_ioc_config_fault(void *arg)
{
	ktm_user_fault_t fault;
	int ret;

#if !(defined(CONFIG_KTM_FAULT) && CONFIG_KTM_FAULT)
	(void)arg;
	(void)fault;
	(void)ret;
	return -ENOTSUPP;
#else
	if (!arg)
		return -EINVAL;
	if (copy_from_user(&fault, arg, sizeof(fault)) != 0)
		return -EFAULT;
	fault.name[sizeof(fault.name) - 1] = '\0';
	if (fault.seed)
		ktm_fault_seed(fault.seed);
	ret = ktm_fault_configure(fault.name, (ktm_fault_mode_t)fault.mode,
				  fault.value);
	return ret < 0 ? -EINVAL : 0;
#endif
}

static int64_t ktm_ioc_get_caps(void *arg)
{
	ktm_user_caps_t caps;

	if (!arg)
		return -EINVAL;
	memset(&caps, 0, sizeof(caps));
	caps.version = KTM_UAPI_VERSION;
	caps.caps = KTM_CAP_USERDEV;
#if defined(CONFIG_KTM_EVENTS) && CONFIG_KTM_EVENTS
	caps.caps |= KTM_CAP_EVENTS;
#endif
#if defined(CONFIG_KTM_TEST) && CONFIG_KTM_TEST
	caps.caps |= KTM_CAP_TEST;
#endif
#if defined(CONFIG_KTM_FAULT) && CONFIG_KTM_FAULT
	caps.caps |= KTM_CAP_FAULT;
#endif
	if (copy_to_user(arg, &caps, sizeof(caps)) != 0)
		return -EFAULT;
	return 0;
}

static int64_t ktm_ioc_user_event(void *arg)
{
	ktm_user_event_t uev;
	const char *label;
	uint16_t type;
	uint16_t subsys;

	if (!arg)
		return -EINVAL;
	if (copy_from_user(&uev, arg, sizeof(uev)) != 0)
		return -EFAULT;
	uev.name[sizeof(uev.name) - 1] = '\0';
	label = uev.name[0] ? uev.name : "user";
	type = (uint16_t)uev.type;
	subsys = (uint16_t)(uev.subsystem ? uev.subsystem : KTM_SUBSYS_TEST);

	ktm_event_emit4(type, subsys, uev.arg0, uev.arg1, uev.arg2, uev.arg3);

	if (type == KTM_EVENT_CASE_BEGIN || type == KTM_UAPI_EVENT_CASE_BEGIN)
	{
		ktm_transport_emit("TEST_BEGIN", label, NULL);
		return 0;
	}
	if (type == KTM_EVENT_CASE_END || type == KTM_UAPI_EVENT_CASE_END)
	{
		if (uev.arg0 == 0)
		{
			ktm_transport_emit("TEST_END", label, "PASS");
			ktm_transport_suite_end(1, 0);
			/* Contiguous host autokill tag (userspace write may fragment). */
			klog_smoke("KTM_USERDEV_OK");
		}
		else
		{
			ktm_transport_emit("TEST_END", label, "FAIL");
			ktm_transport_suite_end(0, 1);
			klog_smoke("KTM_USERDEV_FAIL");
		}
		return 0;
	}
	if (type == KTM_EVENT_USER_ASSERT || type == KTM_UAPI_EVENT_USER_ASSERT ||
	    type == KTM_EVENT_ASSERT_PASS || type == KTM_EVENT_ASSERT_FAIL)
	{
		if (uev.arg0)
			ktm_transport_emit("ASSERT_PASS", label, NULL);
		else
			ktm_transport_emit("ASSERT_FAIL", label, NULL);
		return 0;
	}
	if (type == KTM_EVENT_CHECKPOINT)
	{
		ktm_transport_emit("CHECKPOINT", label, NULL);
		return 0;
	}
	ktm_transport_emit("USER_EVENT", label, NULL);
	return 0;
}

static int64_t ktm_dev_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
	(void)entry;
	request = (uint32_t)request;

	switch (request)
	{
	case KTM_IOC_RUN_SCENARIO:
		return ktm_ioc_run_scenario(arg);
	case KTM_IOC_RUN_INVARIANTS:
		return ktm_ioc_run_invariants(arg);
	case KTM_IOC_TAKE_SNAPSHOT:
		return ktm_ioc_take_snapshot(arg);
	case KTM_IOC_CONFIG_FAULT:
		return ktm_ioc_config_fault(arg);
	case KTM_IOC_RESET:
		ktm_suite_reset();
		return 0;
	case KTM_IOC_GET_CAPS:
		return ktm_ioc_get_caps(arg);
	case KTM_IOC_USER_EVENT:
		return ktm_ioc_user_event(arg);
	case KTM_IOC_DUMP_EVENTS:
	{
		/* @arg is a packed scalar, not a user pointer: a failing test
		 * asks for the dump from its own error path, where copying a
		 * struct in is extra surface for no gain. */
		uint64_t packed = (uint64_t)(uintptr_t)arg;

		ktm_event_ring_dump((size_t)(uint32_t)packed,
				    (uint32_t)(packed >> 32));
		return 0;
	}
	default:
		return -ENOTTY;
	}
}

static const devfs_ops_t ktm_ops = {
	.read = ktm_dev_read,
	.write = NULL,
	.ioctl = ktm_dev_ioctl,
	.open = NULL,
	.close = NULL,
	.can_read = ktm_dev_can_read,
	.can_write = NULL,
};

static devfs_node_t dev_ktm = {
	.entry = { .name = "ktm", .mode = 0600, .device_id = KTM_DEV_ID },
	.ops = &ktm_ops,
	.ref_count = 0,
};

void ktm_userdev_register(void)
{
	if (g_registered)
		return;
	if (devfs_register_node(&dev_ktm) == 0)
		g_registered = 1;
}

#else /* !CONFIG_KTM_USERDEV */

void ktm_userdev_register(void)
{
}

#endif
