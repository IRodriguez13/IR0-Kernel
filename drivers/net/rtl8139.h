/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025 Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: rtl8139.h
 * Description: RTL8139 network card driver definitions
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

/* PCI configuration space access constants */
#define PCI_CONFIG_ADDRESS_PORT 0xCF8
#define PCI_CONFIG_DATA_PORT    0xCFC
#define PCI_ENABLE_BIT          0x80000000

/* PCI configuration register offsets */
#define PCI_REG_COMMAND         0x04
#define PCI_REG_BAR0            0x10
#define PCI_REG_INTERRUPT_LINE  0x3C  /* Interrupt Line (IRQ number) */

/* PCI command register bits */
#define PCI_CMD_IO_SPACE        (1 << 0)
#define PCI_CMD_BUS_MASTER      (1 << 2)

/* RTL8139 size definitions */
#define RTL8139_RX_BUF_SIZE     8192
#define RTL8139_RX_BUF_PADDING  (16 + 1500)
#define RTL8139_MAX_TX_SIZE     1792

/* PCI definitions for RTL8139 */
#define RTL8139_VENDOR_ID 0x10EC
#define RTL8139_DEVICE_ID 0x8139

/* RTL8139 Registers */
#define RTL8139_REG_MAC0 0x00    /* MAC Address */
#define RTL8139_REG_MAR0 0x08    /* Multicast Filter High */
#define RTL8139_REG_MAR4 0x0C    /* Multicast Filter Low */
#define RTL8139_REG_TSD0 0x10    /* Transmit Status Descriptor 0 */
#define RTL8139_REG_TSAD0 0x20   /* Transmit Start Address Descriptor 0 */
#define RTL8139_REG_RBSTART 0x30 /* Receive Buffer Start Address */
#define RTL8139_REG_CR 0x37      /* Command Register */
#define RTL8139_REG_CAPR 0x38    /* Current Address of Packet Read */
#define RTL8139_REG_CBR 0x3A     /* Current Buffer Address (RX write ptr) */
#define RTL8139_REG_IMR 0x3C     /* Interrupt Mask Register */
#define RTL8139_REG_ISR 0x3E     /* Interrupt Status Register */
#define RTL8139_REG_TCR 0x40     /* Transmit Configuration Register */
#define RTL8139_REG_RCR 0x44     /* Receive Configuration Register */
#define RTL8139_REG_CONFIG1 0x52 /* Configuration Register 1 */
#define RTL8139_REG_MSR     0x58 /* Media Status Register */

/* Media Status Register bits */
#define RTL8139_MSR_LINKB   (1 << 2) /* LinkFail: 1=down, 0=up (Realtek MSR) */
#define RTL8139_MSR_SPEED   (1 << 3) /* Speed (0=10M, 1=100M) */
#define RTL8139_MSR_AUX     (1 << 4) /* Aux. Power Status */

/* Command Register bits */
#define RTL8139_CR_BUFE (1 << 0) /* Buffer Empty */
#define RTL8139_CR_TE (1 << 2)   /* Transmitter Enable */
#define RTL8139_CR_RE (1 << 3)   /* Receiver Enable */
#define RTL8139_CR_RST (1 << 4)  /* Software Reset */

/* Interrupt Status/Mask Register bits */
#define RTL8139_INT_ROK (1 << 0)     /* Receive OK */
#define RTL8139_INT_RER (1 << 1)     /* Receive Error */
#define RTL8139_INT_TOK (1 << 2)     /* Transmit OK */
#define RTL8139_INT_TER (1 << 3)     /* Transmit Error */
#define RTL8139_INT_RXOVW (1 << 4)   /* RX Buffer Overflow */
#define RTL8139_INT_PUN (1 << 5)     /* Packet Underrun */
#define RTL8139_INT_FIFOOVW (1 << 6) /* FIFO Overflow */

/* Receive Configuration Register bits */
#define RTL8139_RCR_AAP (1 << 0)  /* Accept All Packets */
#define RTL8139_RCR_APM (1 << 1)  /* Accept Physical Match */
#define RTL8139_RCR_AM (1 << 2)   /* Accept Multicast */
#define RTL8139_RCR_AB (1 << 3)   /* Accept Broadcast */
#define RTL8139_RCR_WRAP (1 << 7) /* Wrap around */

/* Transmit Status Descriptor bits (QEMU TxHostOwns / Realtek OWN) */
#define RTL8139_TSD_SIZE_MASK   0x1FFF /* Packet size mask */
/* Bit 13: host owns descriptor when set (idle / ready). Driver clears it
 * (writes size only) to start DMA; hardware sets it again when DMA done. */
#define RTL8139_TSD_OWN         (1 << 13)
#define RTL8139_TSD_ERTX_64     (1U << 16) /* Early TX threshold field (not OWN) */
#define RTL8139_TSD_TOK         (1U << 15)   /* Transmit OK (valid when host owns) */

/* Receive Status bits (from packet header) */
#define RTL8139_RX_STAT_ROK     (1 << 0) /* Receive OK */

/* Public API */
int rtl8139_init(void);
int rtl8139_send(void *data, size_t len);
void rtl8139_handle_interrupt(void);
void rtl8139_poll(void);  /* Poll for received packets (fallback when interrupts don't work) */
int rtl8139_get_irq_line(void);
void rtl8139_get_mac(uint8_t mac[6]);
void rtl8139_get_stats(uint64_t *rx_pkts, uint64_t *tx_pkts,
                        uint64_t *rx_errs, uint64_t *tx_errs);
