/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ktest_decl.h
 * Description: Prototypes for in-kernel ktest_* functions.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

void ktest_boot_ok(void);
void ktest_resource_registry(void);
void ktest_allocator(void);
void ktest_path(void);
void ktest_string(void);
void ktest_syscall_getpid(void);
void ktest_syscall_open_close(void);
void ktest_syscall_proc_read(void);
void ktest_syscall_pipe(void);
void ktest_procfs_uptime(void);
void ktest_procfs_pid_status(void);
void ktest_process_current(void);
void ktest_wait4_status(void);
void ktest_wait4_specific_reaps_requested_child(void);
void ktest_wait4_minus_one_reaps_any_child(void);
void ktest_wait4_specific_no_leak_after_destroy(void);
void ktest_wait4_wnohang_specific(void);
void ktest_wait4_wnohang_echild_after_reap(void);
void ktest_wait4_pgrp(void);
void ktest_arch_prctl_set_fs_rejects_nonuser(void);
void ktest_kill_sigterm_wait_status(void);
void ktest_proc_blockdevices_contract(void);
void ktest_proc_cpuinfo_contract(void);
void ktest_proc_version_contract(void);
void ktest_proc_uptime_contract(void);
void ktest_sysfs_hostname_contract(void);
void ktest_proc_netinfo_contract(void);
void ktest_dev_net_contract(void);
void ktest_mount_proc_contract(void);
void ktest_mount_tmpfs_contract(void);
void ktest_mount_multi_fs_contract(void);
void ktest_mount_longest_prefix_contract(void);
void ktest_mount_umount_remount_contract(void);
void ktest_block_hda_read_contract(void);
void ktest_cred_access_contract(void);
void ktest_devfs_hci_open_contract(void);
void ktest_mmap_null_placement(void);
void ktest_signal_segv_deliver_irq_frame(void);
void ktest_brk_post_exec(void);
void ktest_brk_linux_fail_returns_current(void);
void ktest_tty_canon_read_immediate(void);
void ktest_tty_canon_block_wake(void);
void ktest_process_reset_blocked_syscall_state(void);
