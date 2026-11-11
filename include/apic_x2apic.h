/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef APIC_X2APIC_H
#define APIC_X2APIC_H
#include <stdint.h>
#define APIC_X2APIC_MSR_BASE 0x800U
#define APIC_X2APIC_MSR_ICR 0x830U
static inline uint32_t apic_x2apic_msr_for_offset(uint32_t offset)
{
    if ((offset & 0x0FU) != 0U || offset > 0x3F0U)
        return 0U;
    return APIC_X2APIC_MSR_BASE + (offset >> 4U);
}
static inline uint64_t apic_x2apic_encode_icr(uint32_t destination,
                                               uint32_t command)
{
    return ((uint64_t)destination << 32U) | command;
}
static inline int apic_id_requires_x2apic(uint32_t apic_id)
{
    return apic_id > 255U;
}
#endif
