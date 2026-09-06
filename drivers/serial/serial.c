/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: serial.c
 * Description: Serial port driver for debugging output (x86 COM1 16550).
 *
 * ARCH=arm64 does not link this TU (see Makefile DRIVER_OBJS); PL011 lives in
 * arch/arm64/sources/serial_io_arm64.c. Guarding body avoids accidental COM1.
 */

#include <stdint.h>
#include <stdbool.h>
#include <ir0/arch_port.h>
#include "serial.h"
#include <ir0/driver.h>
#include <ir0/logging.h>
#include <string.h>

#if defined(__x86_64__) || defined(ARCH_X86_64)

/* Compiler optimization hints */
#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)

static bool serial_initialized = false;

/* Internal hardware initialization function */
static int32_t serial_hw_init(void)
{
    /* Disable interrupts */
    outb(SERIAL_PORT_COM1 + SERIAL_INT_EN_REG, SERIAL_IER_DISABLE);

    /* Set baud rate divisor (38400 baud) */
    outb(SERIAL_PORT_COM1 + SERIAL_LINE_CTRL_REG, SERIAL_LCR_DLAB);     /* Enable DLAB */
    outb(SERIAL_PORT_COM1 + SERIAL_DATA_REG, SERIAL_BAUD_38400);        /* Divisor low byte */
    outb(SERIAL_PORT_COM1 + SERIAL_INT_EN_REG, 0x00);                   /* Divisor high byte */

    /* Configure: 8 bits, no parity, one stop bit */
    outb(SERIAL_PORT_COM1 + SERIAL_LINE_CTRL_REG, SERIAL_LCR_8N1);

    /* Enable FIFO, clear them, with 14-byte threshold */
    outb(SERIAL_PORT_COM1 + SERIAL_FIFO_CTRL_REG, SERIAL_FCR_CONFIG);

    /* Enable IRQs, set RTS/DSR */
    outb(SERIAL_PORT_COM1 + SERIAL_MODEM_CTRL_REG, SERIAL_MCR_CONFIG);

    serial_initialized = true;
    return 0;
}

/* Driver registration structures */
static ir0_driver_ops_t serial_ops = {
    .init = serial_hw_init,
    .shutdown = NULL
};

static ir0_driver_info_t serial_info = {
    .name = "Serial UART",
    .version = "1.0",
    .author = "Iván Rodriguez",
    .description = "Standard 16550 UART Serial Driver",
    .language = IR0_DRIVER_LANG_C
};

/**
 * serial_init - register serial port driver
 */
void serial_init(void)
{
    LOG_INFO("SERIAL", "Registering Serial UART (COM1) driver...");
    ir0_register_driver(&serial_info, &serial_ops);
}

/**
 * serial_is_transmit_empty - check if transmitter is ready
 *
 * Returns true if transmitter holding register is empty.
 */
static int serial_is_transmit_empty(void)
{
    return inb(SERIAL_PORT_COM1 + SERIAL_LINE_STATUS_REG) & SERIAL_LSR_THRE;
}

/**
 * serial_putchar - write a character to the serial port
 *
 * Bounded wait: if the host side of -serial stdio stops draining (full pipe
 * while the user watches only the GTK window), an infinite THRE spin freezes
 * the guest — including getty after LOGIN_USER_READ, before "Password:" is
 * painted on the framebuffer.
 */
void serial_putchar(char c)
{
    unsigned spins = 0;

    while (serial_is_transmit_empty() == 0)
    {
        if (++spins > 100000u)
            return;
    }

    outb(SERIAL_PORT_COM1, (uint8_t)c);
}

/**
 * serial_print - print a string to the serial port
 */
void serial_print(const char *str)
{
    if (unlikely(!str))
    {
        return;
    }

    while (*str)
    {
        if (unlikely(*str == '\n'))
        {
            serial_putchar('\r');
        }
        serial_putchar(*str++);
    }
}

/**
 * serial_print_hex32 - print a 32-bit hex value to the serial port
 */
void serial_print_hex32(uint32_t val)
{
    char buf[9];
    const char *hex = "0123456789ABCDEF";

    for (int i = 7; i >= 0; i--)
    {
        buf[i] = hex[val & 0xF];
        val >>= 4;
    }
    buf[8] = '\0';
    serial_print(buf);
}

/**
 * serial_print_hex64 - print a 64-bit hex value to the serial port
 */
void serial_print_hex64(uint64_t val)
{
    char buf[17];
    const char *hex = "0123456789ABCDEF";

    for (int i = 15; i >= 0; i--)
    {
        buf[i] = hex[val & 0xF];
        val >>= 4;
    }
    buf[16] = '\0';
    serial_print(buf);
}

/**
 * serial_is_receive_ready - check if receiver has data
 *
 * Returns true if data is available in receiver buffer.
 */
static int serial_is_receive_ready(void)
{
    /* Check Line Status Register bit 0 (Data Ready) */
    return inb(SERIAL_PORT_COM1 + SERIAL_LINE_STATUS_REG) & 0x01;
}

/**
 * serial_read_char - read a character from the serial port
 *
 * Blocks until data is available.
 * 
 * Returns: Character read from serial port
 */
int serial_try_read_char(void)
{
    if (serial_is_receive_ready() == 0)
        return -1;

    return (int)(uint8_t)inb(SERIAL_PORT_COM1 + SERIAL_DATA_REG);
}

char serial_read_char(void)
{
    int ch;

    while ((ch = serial_try_read_char()) < 0)
    {
        /* Wait for receiver buffer to have data */
    }

    return (char)ch;
}

#else /* !x86_64 — COM1 not applicable; use arch serial_io (PL011 on arm64). */

void serial_init(void)
{
}

void serial_putchar(char c)
{
	(void)c;
}

void serial_print(const char *str)
{
	(void)str;
}

void serial_print_hex32(uint32_t val)
{
	(void)val;
}

void serial_print_hex64(uint64_t val)
{
	(void)val;
}

int serial_try_read_char(void)
{
	return -1;
}

char serial_read_char(void)
{
	return 0;
}

#endif /* __x86_64__ || ARCH_X86_64 */
