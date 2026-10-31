/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "timer.h"
#include "timer_math.h"
#include "watchdog.h"
#include "process.h"
#include "scheduler.h"

#define PIT_COMMAND_PORT 0x43
#define PIT_DATA_PORT_0  0x40

static volatile uint64_t ticks = 0;
static volatile uint32_t timer_freq = 100; // Default 100 Hz

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

int timer_initialize(uint32_t frequency) {
    uint16_t divisor;
    if (!vanta_timer_pit_divisor(frequency, &divisor))
        return 0;
    __atomic_store_n(&timer_freq, frequency, __ATOMIC_RELEASE);

    // Command byte: Channel 0, Access mode lobyte/hibyte, Square wave mode
    outb(PIT_COMMAND_PORT, 0x36);

    // Send divisor bytes
    outb(PIT_DATA_PORT_0, (uint8_t)(divisor & 0xFF));
    outb(PIT_DATA_PORT_0, (uint8_t)((divisor >> 8) & 0xFF));
    return 1;
}

void timer_handle_interrupt(void) {
    uint64_t now = __atomic_add_fetch(&ticks, 1ULL, __ATOMIC_RELAXED);
    kernel_watchdog_timer_tick((uint32_t)now);
    process_account_runtime_tick(process_current_pid());
    process_wake_expired(now);
}

uint32_t timer_get_ticks(void) {
    return __atomic_load_n(&ticks, __ATOMIC_ACQUIRE);
}

uint64_t timer_get_ticks64(void) {
    return __atomic_load_n(&ticks, __ATOMIC_ACQUIRE);
}

uint32_t timer_get_frequency(void) {
    return __atomic_load_n(&timer_freq, __ATOMIC_ACQUIRE);
}

uint64_t timer_get_uptime_ms(void) {
    return vanta_timer_ticks_to_ms(timer_get_ticks64(),
                                   timer_get_frequency());
}

void sleep_ms(uint32_t ms) {
    uint64_t interrupt_flags;
    __asm__ volatile ("pushfq; popq %0" : "=r"(interrupt_flags));
    /* Sleeping with IF clear would halt forever because the PIT cannot wake us. */
    if ((interrupt_flags & (1ULL << 9)) == 0U)
        return;

    uint64_t remaining = vanta_timer_ms_to_ticks(ms, timer_get_frequency());
    if (remaining == 0U)
        return;

    uint64_t start_tick = timer_get_ticks64();
    uint64_t deadline = vanta_timer_deadline_after(start_tick, remaining);

    /*
     * Scheduled processes block instead of consuming a CPU in an HLT polling
     * loop. The PIT wakes the process at its absolute deadline. Keep the
     * polling fallback for early boot and non-process kernel contexts.
     */
    if (scheduler_is_initialized() && process_current_pid() != 0U)
    {
        while (timer_get_ticks64() < deadline)
        {
            if (!process_sleep_current_until(deadline))
                break;
            scheduler_request_reschedule();
            /* STI;HLT is atomic with respect to maskable IRQ delivery. */
            __asm__ volatile ("sti; hlt" ::: "memory");
        }
        if (timer_get_ticks64() >= deadline)
            return;
    }

    while (timer_get_ticks64() < deadline)
        __asm__ volatile ("hlt" ::: "memory");
}
