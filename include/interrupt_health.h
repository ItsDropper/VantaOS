/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef INTERRUPT_HEALTH_H
#define INTERRUPT_HEALTH_H

#include <stdint.h>

/*
 * Boot-time check that the timer interrupt actually reaches the CPU, with
 * automatic recovery. Real firmware can leave the interrupt path in a state
 * the emulator never produces (masked LINT0, undescribed ISA IRQ0 routing),
 * which otherwise leaves the desktop idling in HLT forever.
 */
typedef enum {
    INTERRUPT_HEALTH_UNCHECKED = 0,
    INTERRUPT_HEALTH_OK,          /* IRQ0 arrived on the first routing tried */
    INTERRUPT_HEALTH_RECOVERED,   /* IRQ0 arrived after virtual-wire fallback */
    INTERRUPT_HEALTH_DEAD         /* no IRQ0: run in polling mode */
} interrupt_health_state_t;

/* Call once with interrupts enabled, before the main desktop loop. */
void interrupt_health_verify(void);

interrupt_health_state_t interrupt_health_state(void);

/* Nonzero when the idle loop must poll instead of sleeping in HLT. */
int interrupt_health_polling_required(void);

/* Short routing description for diagnostics: "ioapic", "virtual-wire", "pic". */
const char* interrupt_health_route_name(void);

#endif
