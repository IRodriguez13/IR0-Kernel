/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: isr_handlers.c
 * Description: Interrupt Service Routine handlers for exceptions, hardware interrupts, and system calls
 */

#include "idt.h"
#include "pic.h"
#include "io.h"
#include <ir0/vga.h>
#include <ir0/signals.h>
#include <ir0/oops.h>
#include <ir0/ktm/klog.h>
#include <ir0/debug_trap.h>
#include <kernel/process.h>
#include <ir0/sched.h>
#include <config.h>
#include <ir0/input_backend.h>
#include <string.h>
#if CONFIG_ENABLE_NETWORKING
#include <ir0/net.h>
#endif

/* Declaraciones externas para el nuevo driver de teclado */
extern void keyboard_handler64(void);
extern void increment_pit_ticks(void);
#ifdef __x86_64__

extern void page_fault_handler_x64(uint64_t *stack);
extern void gpf_audit_from_isr(uint64_t *stack);
extern uint64_t get_current_page_directory(void);
extern uint64_t iretq_checkpoint_buf[40];
extern uint64_t isr_abi_entry_intno;
extern uint64_t isr_abi_entry_has_err;
extern uint64_t isr_abi_entry_rsp;
extern uint64_t isr_abi_entry_qwords[16];

static int is_exception_with_hw_error_code(uint64_t int_no)
{
    switch (int_no)
    {
        case 8:
        case 10:
        case 11:
        case 12:
        case 13:
        case 14:
        case 17:
            return 1;
        default:
            return 0;
    }
}

static void dump_qwords16(const char *tag, const uint64_t *q)
{
    char buf[384];
    size_t off;
    int i;

    if (!q)
        return;

    off = (size_t)snprintf(buf, sizeof(buf), "%s q0=",
			   tag ? tag : "[ISRABI]");
    for (i = 0; i < 16 && off < sizeof(buf) - 24; i++)
    {
        off += (size_t)snprintf(buf + off, sizeof(buf) - off, "%s%llx",
				i ? "," : "",
				(unsigned long long)q[i]);
    }
    klog_debug("ISR", buf);
}

#if CONFIG_DEBUG_ISRABI
static void isr_abi_audit(uint64_t interrupt_number, uint64_t *stack)
{
    uint64_t c_qwords[16] = {0};
    uint64_t c_rip;
    uint64_t c_cs;
    uint64_t c_rflags;
    uint64_t c_rsp;
    uint64_t c_ss;
    uint64_t old_rip;
    uint64_t old_cs;
    uint64_t old_rsp;
    uint64_t old_ss;
    int hw_err_expected;
    int i;

    if (!stack)
        return;

    for (i = 0; i < 16; i++)
        c_qwords[i] = stack[i];

    hw_err_expected = is_exception_with_hw_error_code(interrupt_number);
    c_rip = stack[2];
    c_cs = stack[3];
    c_rflags = stack[4];
    c_rsp = stack[5];
    c_ss = stack[6];
    old_rip = stack[0];
    old_cs = stack[3];
    old_rsp = stack[5];
    old_ss = stack[6];

    klog_debug_fmt("ISR", "[ISRABI] int=%llx asm_int=%llx asm_has_err=%llx hw_err_expected=%llx asm_rsp=%llx c_stack=%llx", (unsigned long long)(interrupt_number), (unsigned long long)(isr_abi_entry_intno), (unsigned long long)(isr_abi_entry_has_err), (unsigned long long)((uint64_t)hw_err_expected), (unsigned long long)(isr_abi_entry_rsp), (unsigned long long)((uint64_t)(uintptr_t)stack));

    dump_qwords16("[ISRABI][ASM_ENTRY]", isr_abi_entry_qwords);
    dump_qwords16("[ISRABI][C_FRAME]", c_qwords);

    klog_debug_fmt("ISR", "[ISRABI][LAYOUT] int=%llx real_rip=stack[2]=%llx real_cs=stack[3]=%llx real_rflags=stack[4]=%llx real_rsp=stack[5]=%llx real_ss=stack[6]=%llx", (unsigned long long)(interrupt_number), (unsigned long long)(c_rip), (unsigned long long)(c_cs), (unsigned long long)(c_rflags), (unsigned long long)(c_rsp), (unsigned long long)(c_ss));

    klog_debug_fmt("ISR", "[ISRABI][LEGACY_MAP] int=%llx stack[0]=%llx stack[3]=%llx stack[5]=%llx stack[6]=%llx", (unsigned long long)(interrupt_number), (unsigned long long)(old_rip), (unsigned long long)(old_cs), (unsigned long long)(old_rsp), (unsigned long long)(old_ss));
}
#endif

static void isr_contract_single_check(uint64_t interrupt_number, uint64_t *stack)
{
    uint64_t marker_b;
    uint64_t expected_rip;
    uint64_t expected_cs;
    uint64_t expected_rflags;
    uint64_t expected_rsp;
    uint64_t expected_ss;
    uint64_t actual_fault_rip;
    uint64_t actual_fault_cs;
    uint64_t actual_fault_rsp;
    const char *classification = "CONTEXT_RESTORE_BROKEN";

    if (!stack || interrupt_number >= 32)
        return;

    if (ir0_panic_in_progress())
        return;

    marker_b = iretq_checkpoint_buf[8];
    expected_rip = iretq_checkpoint_buf[9];
    expected_cs = iretq_checkpoint_buf[10];
    expected_rflags = iretq_checkpoint_buf[11];
    expected_rsp = iretq_checkpoint_buf[12];
    expected_ss = iretq_checkpoint_buf[13];

    actual_fault_rip = stack[2];
    actual_fault_cs = stack[3];
    actual_fault_rsp = stack[5];

    /*
     * Marker unset (zeros): contract never armed — common on lazy user #PF.
     * Do not classify as CONTEXT_LIFETIME_BROKEN or dump RAW (serial storm).
     */
    if (marker_b != 0xBBB1ULL)
        return;

    if ((expected_cs & 0x3ULL) != 0x3ULL && (actual_fault_cs & 0x3ULL) == 0x0ULL)
        classification = "CONTEXT_RESTORE_BROKEN";
    else if ((expected_cs & 0x3ULL) == 0x3ULL && (actual_fault_cs & 0x3ULL) == 0x0ULL)
        classification = "IRET_TRANSITION_BROKEN";
    else if (expected_cs != actual_fault_cs && (actual_fault_cs & 0x3ULL) == 0x3ULL)
        classification = "CONTEXT_LAYOUT_MISMATCH";
    else if (expected_rip != actual_fault_rip || expected_rsp != actual_fault_rsp)
        classification = "CONTEXT_COPY_BROKEN";

    klog_debug_fmt("ISR", "PRE_IRET_RAW marker=%llx rip=%llx cs=%llx rflags=%llx rsp=%llx ss=%llx", (unsigned long long)(marker_b), (unsigned long long)(expected_rip), (unsigned long long)(expected_cs), (unsigned long long)(expected_rflags), (unsigned long long)(expected_rsp), (unsigned long long)(expected_ss));

    klog_debug_fmt("ISR", "POST_FAULT_RAW int=%llx rip=%llx cs=%llx rsp=%llx", (unsigned long long)(interrupt_number), (unsigned long long)(actual_fault_rip), (unsigned long long)(actual_fault_cs), (unsigned long long)(actual_fault_rsp));

    klog_debug_fmt("ISR", "COMPARE expected_rip=%llx actual_fault_rip=%llx expected_cs=%llx actual_fault_cs=%llx expected_rsp=%llx actual_fault_rsp=%llx", (unsigned long long)(expected_rip), (unsigned long long)(actual_fault_rip), (unsigned long long)(expected_cs), (unsigned long long)(actual_fault_cs), (unsigned long long)(expected_rsp), (unsigned long long)(actual_fault_rsp));

    klog_debug_fmt("ISR", "CLASSIFY %s", classification);
}

static int is_user_exception_frame(uint64_t *stack)
{
    if (!stack)
        return 0;
    return ((stack[3] & 0x3U) == 0x3U);
}

/* Handler de interrupciones para 64-bit */
void isr_handler64(uint64_t interrupt_number, uint64_t *stack)
{
#if defined(__x86_64__) || defined(__amd64__)
    if (interrupt_number == 1 && stack)
    {
        if (is_user_exception_frame(stack))
        {
            if (ir0_debug_handle_user_db(stack))
                return;
        }
        else
        {
            ir0_debug_handle_kernel_db(stack);
            return;
        }
    }
#endif

    if (interrupt_number < 32 && stack)
        isr_contract_single_check(interrupt_number, stack);

    if (interrupt_number < 32 && stack)
#if CONFIG_DEBUG_ISRABI
        isr_abi_audit(interrupt_number, stack);
#endif

    if (interrupt_number < 32 && stack)
    {
        process_t *current = process_get_current();
        uint64_t user = (uint64_t)is_user_exception_frame(stack);
        uint64_t fault_rip = stack[2];
        uint64_t fault_cs = stack[3];
        uint64_t fault_rsp = stack[5];

        klog_debug_fmt("ISR", "[ISR] int=%llx current=%llx cr3=%llx rip=%llx cs=%llx rsp=%llx ss=%llx frame=%llx user=%llx", (unsigned long long)(interrupt_number), (unsigned long long)((uint64_t)(uintptr_t)current), (unsigned long long)(get_current_page_directory()), (unsigned long long)(fault_rip), (unsigned long long)(fault_cs), (unsigned long long)(fault_rsp), (unsigned long long)(stack[6]), (unsigned long long)((uint64_t)(uintptr_t)stack), (unsigned long long)(user));

        /*
         * Desk-session flake: #UD/#GP with RIP in non-text (data/bss).
         * Fault RIP 0x20E52A was nearest-symbol noise on event_queue; class
         * is KERNEL_EXECUTE_BSS for any RIP past _etext.
         */
	{
		extern char _etext[];
		extern char _end[];
		uint64_t et = (uint64_t)(uintptr_t)_etext;
		uint64_t en = (uint64_t)(uintptr_t)_end;

		if (!user && fault_rip >= et && fault_rip < en)
		{
			char rspbuf[512];
			size_t off;
			size_t i;

			klog_notice_fmt("ISR",
					"CLASSIFY KERNEL_EXECUTE_BSS rip=%llx "
					"_etext=%llx _end=%llx current=%llx "
					"comm=%s rsp_qwords=",
					(unsigned long long)fault_rip,
					(unsigned long long)et,
					(unsigned long long)en,
					(unsigned long long)((uint64_t)(uintptr_t)current),
					current ? current->comm : "(none)");
			off = 0;
			for (i = 0; i < 24 && off < sizeof(rspbuf) - 24; i++)
			{
				uint64_t v = ((uint64_t *)(uintptr_t)fault_rsp)[i];

				off += (size_t)snprintf(rspbuf + off, sizeof(rspbuf) - off,
							"%s%llx", i ? " " : "",
							(unsigned long long)v);
			}
			klog_notice("ISR", rspbuf);
		}
	}
    }

    /* Manejar excepciones del CPU (0-31) */
    if (interrupt_number < 32)
    {
        process_t *current = process_get_current();
        int signal_to_send = 0;

        if (interrupt_number == 14)
        {
            page_fault_handler_x64(stack);
            return;
        }

        if (interrupt_number == 13 && stack)
        {
            gpf_audit_from_isr(stack);
        }

        /* Map CPU exceptions to signals for user-space faults */
        switch (interrupt_number)
        {
            case 0:  /* Divide by Zero */
                signal_to_send = SIGFPE;
                break;
            case 4:  /* Overflow */
                signal_to_send = SIGFPE;
                break;
            case 6:  /* Invalid Opcode */
                signal_to_send = SIGILL;
                break;
            case 8:  /* Double Fault - severe error */
                signal_to_send = SIGSEGV;
                break;
            case 11: /* Segment Not Present */
                signal_to_send = SIGSEGV;
                break;
            case 13: /* General Protection Fault */
                signal_to_send = SIGSEGV;
                break;
            case 14: /* Page Fault */
                signal_to_send = SIGSEGV;
                break;
            case 19: /* SIMD FPU Exception */
                signal_to_send = SIGFPE;
                break;
            default:
                signal_to_send = SIGSEGV;
                break;
        }

        /*
         * User-mode exceptions are converted to signals.
         * Kernel exceptions are fatal to avoid endless fault loops.
         */
        if (is_user_exception_frame(stack) && current && signal_to_send != 0)
        {
            klog_notice_fmt("ISR",
                            "user_exception int=%llx -> sig=%x pid=%x rip=%llx cs=%llx err=%llx",
                            (unsigned long long)interrupt_number,
                            (unsigned)signal_to_send,
                            (unsigned)((uint32_t)current->task.pid),
                            (unsigned long long)stack[2],
                            (unsigned long long)stack[3],
                            (unsigned long long)stack[1]);
            send_signal(current->task.pid, signal_to_send);
            return;
        }

	/*
	 * Frame at stack: [int_no, err, RIP, CS, RFLAGS, RSP, SS].
	 * Printing stack[0] as RIP was wrong (always showed the vector).
	 */
	print("[ISR64] int=");
	print_hex64(interrupt_number);
	print(" current=");
	print_hex64((uint64_t)(uintptr_t)current);
	print(" user=");
	print_hex((uintptr_t)is_user_exception_frame(stack));
	print(" cs=");
	print_hex64(stack[3]);
	print(" rip=");
	print_hex64(stack[2]);
	print(" err=");
	print_hex64(stack[1]);
	print(" rsp=");
	print_hex64(stack[5]);
	print("\n");

	panicex("Unhandled kernel CPU exception", PANIC_KERNEL_BUG, __FILE__,
		__LINE__, __func__);
    }

    /* Manejar syscall (0x80) */
    if (interrupt_number == 128)
    {
        return;
    }

    /* Manejar IRQs del PIC (32-47) */
    if (interrupt_number >= 32 && interrupt_number <= 47)
    {
        uint8_t irq = interrupt_number - 32;

        switch (irq)
        {
        case 0: /* Timer */
        {
            irq_save_user_frame(stack);
            increment_pit_ticks();
            break;
        }

        case 1: /* Keyboard */
            keyboard_handler64();
            break;

        case 12: /* PS/2 Mouse */
        {
            input_mouse_handle_interrupt();
            break;
        }

        default:
            break;
        }

#if CONFIG_ENABLE_NETWORKING
        net_stack_handle_irq(irq);
#endif

        /* Enviar EOI para IRQs */
        pic_send_eoi64(irq);
	/*
	 * Safe preempt after wake (TTY/timer flags). Must run with the IRQ
	 * frame still intact; never schedule from keyboard_handler itself.
	 */
	(void)sched_irq_preempt_from_frame(stack);
        return;
    }

}

#endif
