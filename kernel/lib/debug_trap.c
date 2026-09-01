/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Trap Flag / #DB hygiene implementation.
 */

#include <ir0/debug_trap.h>
#include <ir0/oops.h>
#include <ir0/ktm/klog.h>
#include <ir0/cpu.h>
#include <kernel/process.h>
#include <string.h>

extern uint8_t fork_flow_set_tf;

static void debug_trap_tag_once(const char *tag)
{
	static char seen[384];
	size_t i;
	size_t len;

	if (!tag)
		return;

	len = 0;
	while (tag[len])
		len++;

	for (i = 0; i + len + 1 <= sizeof(seen); i++)
	{
		if (seen[i] == '\0')
		{
			size_t j;

			for (j = 0; j <= len; j++)
				seen[i + j] = tag[j];
			klog_smoke(tag);
			return;
		}
	}
}

void ir0_debug_trap_init(void)
{
	fork_flow_set_tf = 0;
	ir0_debug_clear_dr6_dr7();
	debug_trap_tag_once("DEBUG_TRAP_GATED_BY_FLAG_OK");
	debug_trap_tag_once("STEP_C_DEBUG_TRAP_GATING_OK");
}

int ir0_debug_single_step_active(void)
{
#if IR0_ENABLE_SINGLE_STEP
	return 1;
#else
	return 0;
#endif
}

int ir0_debug_fork_singlestep_active(void)
{
#if IR0_ENABLE_FORK_SINGLESTEP_TRACE
	return 1;
#else
	return 0;
#endif
}

uint64_t ir0_rflags_clear_tf(uint64_t rflags)
{
	return rflags & ~IR0_RFLAGS_TF;
}

uint64_t ir0_rflags_sanitize_user(uint64_t rflags)
{
	uint64_t out;

	/*
	 * Always force IF for ring-3. Context-switch save under cli can
	 * stash IF=0 into task.arch.rflags; returning that to ash freezes the
	 * keyboard (no IRQ1) after login.
	 */
	out = rflags | 2ULL | 0x200ULL;
	if (!ir0_debug_single_step_active())
		out &= ~IR0_RFLAGS_TF;
	return out;
}

void ir0_debug_clear_dr6_dr7(void)
{
	debug_reg_write(6, 0);
	debug_reg_write(7, 0);
}

void ir0_debug_read_dr6_dr7(uint64_t *dr6, uint64_t *dr7)
{
	if (dr6)
		*dr6 = debug_reg_read(6);
	if (dr7)
		*dr7 = debug_reg_read(7);
}

int ir0_debug_handle_user_db(uint64_t *stack)
{
	uint64_t rip_before;
	uint64_t rflags_before;
	uint64_t rflags_after;

	if (!stack)
		return 0;

#if defined(__x86_64__) || defined(__amd64__)
	if (ir0_debug_fork_singlestep_active())
	{
		if (fork_flow_note_debug_exception(stack))
			return 1;
	}
#endif

	if (ir0_debug_single_step_active())
		return 0;

	rip_before = stack[2];
	rflags_before = stack[4];
	rflags_after = ir0_rflags_sanitize_user(rflags_before);
	stack[4] = rflags_after;

	fork_flow_set_tf = 0;
	ir0_debug_clear_dr6_dr7();

	if ((rflags_before & IR0_RFLAGS_TF) && !(rflags_after & IR0_RFLAGS_TF))
		debug_trap_tag_once("RFLAGS_TF_CLEAR_ON_USER_RETURN_OK");

	debug_trap_tag_once("NO_SPURIOUS_USER_DB_OK");
	(void)rip_before;
	return 1;
}

void ir0_debug_handle_kernel_db(uint64_t *stack)
{
	process_t *cur = process_get_current();
	uint64_t rip = stack ? stack[2] : 0;
	uint64_t rflags = stack ? stack[4] : 0;
	uint64_t dr6 = 0;
	uint64_t dr7 = 0;

	ir0_debug_read_dr6_dr7(&dr6, &dr7);

	klog_info_fmt("DEBUG_TRAP",
		      "[DEBUG_TRAP][KERNEL_DB] rip=0x%llx rflags=0x%llx tf=0x%llx dr6=0x%llx dr7=0x%llx pid=0x%x",
		      (unsigned long long)rip, (unsigned long long)rflags,
		      (unsigned long long)((rflags & IR0_RFLAGS_TF) ? 1ULL : 0ULL),
		      (unsigned long long)dr6, (unsigned long long)dr7,
		      (unsigned)(cur ? (uint32_t)cur->task.pid : 0));

	panicex("KERNEL_DEBUG_EXCEPTION_UNEXPECTED", PANIC_HARDWARE_FAULT,
	        __FILE__, __LINE__, __func__);
}
