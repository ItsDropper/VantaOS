/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef VANTAOS_RTL8139_REGS_H
#define VANTAOS_RTL8139_REGS_H

#include <stdint.h>

/* RTL8139 Transmit Status of Descriptor (TSD) status bits. */
#define RTL8139_TSD_TOK 0x00008000U
#define RTL8139_TSD_TUN 0x00004000U
#define RTL8139_TSD_OUT_OF_WINDOW 0x20000000U
#define RTL8139_TSD_ABORTED 0x40000000U
#define RTL8139_TSD_CARRIER_LOST 0x80000000U

#define RTL8139_TSD_ERROR_MASK \
    (RTL8139_TSD_TUN | RTL8139_TSD_OUT_OF_WINDOW | \
     RTL8139_TSD_ABORTED | RTL8139_TSD_CARRIER_LOST)

static inline int rtl8139_tsd_is_terminal(uint32_t status)
{
    return (status & (RTL8139_TSD_TOK | RTL8139_TSD_ERROR_MASK)) != 0U;
}

static inline int rtl8139_tsd_is_success(uint32_t status)
{
    return (status & RTL8139_TSD_TOK) != 0U &&
           (status & RTL8139_TSD_ERROR_MASK) == 0U;
}

#endif
