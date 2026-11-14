/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "interrupt_health.h"
#include "apic.h"
#include "serial.h"
#include "timer.h"

#define HEALTH_WAIT_MS   250U
#define HEALTH_SPIN_MAX  4000000U

static interrupt_health_state_t health_state = INTERRUPT_HEALTH_UNCHECKED;

/* Wait for at least one IRQ0 tick; bounded by TSC time and by a spin cap. */
static int wait_for_timer_irq(void)
{
    uint64_t per_second = timer_tsc_ticks_per_second();
    uint32_t frequency = timer_get_frequency();
    uint64_t budget = 0U;
    if (per_second != 0U && frequency != 0U)
        budget = ((uint64_t)HEALTH_WAIT_MS * frequency + 999U) / 1000U;
    uint64_t start = timer_get_ticks64();

    for (uint32_t spins = 0U; spins < HEALTH_SPIN_MAX; ++spins)
    {
        if (timer_irq_ticks() != 0U)
            return 1;
        if (budget != 0U && timer_get_ticks64() - start >= budget)
            break;
        __asm__ volatile ("pause" ::: "memory");
    }
    return timer_irq_ticks() != 0U;
}

void interrupt_health_verify(void)
{
    if (wait_for_timer_irq())
    {
        health_state = INTERRUPT_HEALTH_OK;
        serial_write("VANTAOS_IRQ_TIMER_OK\n");
        return;
    }

    serial_write("VANTAOS_IRQ_TIMER_SILENT\n");
    if (apic_is_enabled() && !apic_uses_legacy_pic() &&
        apic_fall_back_to_virtual_wire() && wait_for_timer_irq())
    {
        health_state = INTERRUPT_HEALTH_RECOVERED;
        serial_write("VANTAOS_IRQ_TIMER_RECOVERED_VIRTUAL_WIRE\n");
        return;
    }

    health_state = INTERRUPT_HEALTH_DEAD;
    serial_write("VANTAOS_IRQ_TIMER_DEAD_POLLING_MODE\n");
}

interrupt_health_state_t interrupt_health_state(void)
{
    return health_state;
}

int interrupt_health_polling_required(void)
{
    return health_state == INTERRUPT_HEALTH_DEAD;
}

const char* interrupt_health_route_name(void)
{
    if (apic_virtual_wire_active())
        return "virtual-wire";
    if (apic_is_enabled())
        return apic_uses_legacy_pic() ? "pic" : "ioapic";
    return "pic";
}
