/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_runner.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"
#include <stdio.h>

int _ir0_test_failed;
int _ir0_test_count;
int _ir0_test_pass;

extern void test_harness_smoke(void);
extern void test_example_asserts(void);
extern void test_pseudo_fs_register_lookup_read(void);
extern void test_pseudo_fs_proc_registry_paths(void);
extern void test_pseudo_fs_path_has_children_and_collect(void);
extern void test_usb_host_disabled_controller_count(void);
extern void test_bt_scan_sync_stub(void);
extern void test_vfs_backend_contract(void);
extern void test_stat_user_abi(void);
extern void test_named_fifo_supervise(void);
extern void test_path_resolve_at(void);
extern void test_path_chroot(void);
extern void test_ktm_poll_arch_resume_matrix(void);
extern void test_class_b_ctx_invariant_matrix(void);
extern void test_musl_mmap_contract(void);
extern void test_mmap_null_placement(void);
extern void test_signal_rt_sigaction_abi(void);
extern void test_sigreturn_sleep_eintr_frame_abi(void);
extern void test_elf_initial_brk_abi(void);
extern void test_musl_cred_abi(void);
extern void test_blockdev_facade_contract(void);
extern void test_blockdev_readonly_allows_read(void);
extern void test_arch_irq_facade_nested(void);
extern void test_arch_mm_pte_facade(void);
extern void test_ps2_set1_ctrl_x_then_a(void);
extern void test_ps2_set1_left_right_ctrl_independent(void);
extern void test_ps2_set1_shift_a(void);
extern void test_ps2_set1_caps_toggle(void);
extern void test_ps2_set1_arrows_e0(void);
extern void test_ps2_set1_autorepeat_ctrl(void);
extern void test_ps2_set1_esc_emits_ascii_escape(void);
extern void test_ps2_mouse_pkt_resync_and_complete(void);
extern void test_ps2_mouse_interleaved_does_not_touch_kbd_mods(void);
extern void test_ps2_set1_alt_independent(void);
extern void test_pipe_close_end_last_ref_frees_once(void);
extern void test_pipe_pipeline_two_closes_destroy_once(void);
extern void test_rtc_calendar(void);
extern void test_arch_task_contract(void);
extern void test_pseudo_fs_contract(void);
extern void test_mm_mirror_contract(void);
extern void test_netdev_contract(void);
extern void test_sched_backend_contract(void);
extern void test_block_backend_contract(void);
extern void test_usercopy_no_raw_user_touch(void);
extern void test_matrix_capture_suite(void);

static void (*test_functions[])(void) = {
	test_harness_smoke,
	test_example_asserts,
	test_pseudo_fs_register_lookup_read,
	test_pseudo_fs_proc_registry_paths,
	test_pseudo_fs_path_has_children_and_collect,
	test_usb_host_disabled_controller_count,
	test_bt_scan_sync_stub,
	test_vfs_backend_contract,
	test_stat_user_abi,
	test_named_fifo_supervise,
	test_path_resolve_at,
	test_path_chroot,
	test_ktm_poll_arch_resume_matrix,
	test_class_b_ctx_invariant_matrix,
	test_musl_mmap_contract,
	test_mmap_null_placement,
	test_signal_rt_sigaction_abi,
	test_sigreturn_sleep_eintr_frame_abi,
	test_elf_initial_brk_abi,
	test_musl_cred_abi,
	test_blockdev_facade_contract,
	test_blockdev_readonly_allows_read,
	test_arch_irq_facade_nested,
	test_arch_mm_pte_facade,
	test_ps2_set1_ctrl_x_then_a,
	test_ps2_set1_left_right_ctrl_independent,
	test_ps2_set1_shift_a,
	test_ps2_set1_caps_toggle,
	test_ps2_set1_arrows_e0,
	test_ps2_set1_autorepeat_ctrl,
	test_ps2_set1_esc_emits_ascii_escape,
	test_ps2_mouse_pkt_resync_and_complete,
	test_ps2_mouse_interleaved_does_not_touch_kbd_mods,
	test_ps2_set1_alt_independent,
	test_pipe_close_end_last_ref_frees_once,
	test_pipe_pipeline_two_closes_destroy_once,
	test_rtc_calendar,
	test_arch_task_contract,
	test_pseudo_fs_contract,
	test_mm_mirror_contract,
	test_netdev_contract,
	test_sched_backend_contract,
	test_block_backend_contract,
	test_usercopy_no_raw_user_touch,
	test_matrix_capture_suite,
	NULL
};

int main(void)
{
	_ir0_test_count = 0;
	_ir0_test_pass = 1;

	fprintf(stderr, "[TEST] IR0 kernel test suite\n");
	fprintf(stderr, "[TEST] --------------------\n");

	int n = 0;
	for (int i = 0; test_functions[i] != NULL; i++) {
		test_functions[i]();
		n++;
	}
	_ir0_test_count = n;  /* Asegurar total para TEST_EXIT */
	TEST_EXIT();
	return 1;
}
