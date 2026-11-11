/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "apic_x2apic.h"
#include <assert.h>
#include <stdint.h>
int main(void)
{
    assert(apic_x2apic_msr_for_offset(0x020U) == 0x802U);
    assert(apic_x2apic_msr_for_offset(0x080U) == 0x808U);
    assert(apic_x2apic_msr_for_offset(0x0B0U) == 0x80BU);
    assert(apic_x2apic_msr_for_offset(0x0F0U) == 0x80FU);
    assert(apic_x2apic_msr_for_offset(0x300U) == 0x830U);
    assert(apic_x2apic_msr_for_offset(0x320U) == 0x832U);
    assert(apic_x2apic_msr_for_offset(0x350U) == 0x835U);
    assert(apic_x2apic_msr_for_offset(0x360U) == 0x836U);
    assert(apic_x2apic_msr_for_offset(0x380U) == 0x838U);
    assert(apic_x2apic_msr_for_offset(0x390U) == 0x839U);
    assert(apic_x2apic_msr_for_offset(0x3E0U) == 0x83EU);
    assert(apic_x2apic_msr_for_offset(0x021U) == 0U);
    assert(apic_x2apic_msr_for_offset(0x400U) == 0U);
    uint64_t icr = apic_x2apic_encode_icr(0x12345678U, 0x00000608U);
    assert((uint32_t)(icr >> 32U) == 0x12345678U);
    assert((uint32_t)icr == 0x00000608U);
    assert(!apic_id_requires_x2apic(0U));
    assert(!apic_id_requires_x2apic(254U));
    assert(!apic_id_requires_x2apic(255U));
    assert(apic_id_requires_x2apic(256U));
    return 0;
}
