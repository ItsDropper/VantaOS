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

/*
 * Time base. Before the first IRQ0 reaches the CPU (device drivers initialize
 * with interrupts disabled), ticks are derived from the TSC, calibrated against
 * PIT channel 2, so tick-based timeouts still expire. When the first IRQ0
 * arrives the counter continues from the synthesized value, keeping
 * timer_get_ticks64() monotonic. If IRQ0 never arrives (a mis-routed interrupt
 * path on real hardware), the TSC keeps time and timer_irq_ticks() stays zero
 * so callers can detect and recover from the dead interrupt line.
 */
#define PIT_CHANNEL2_PORT   0x42
#define PIT_GATE_PORT       0x61
#define TSC_CAL_PIT_COUNTS  11932U /* about 10 ms */
#define TSC_CAL_SPIN_LIMIT  400000U

static volatile uint64_t ticks = 0;
static volatile uint32_t timer_freq = 100; // Default 100 Hz
static uint64_t tsc_per_tick;
static uint64_t tsc_origin;
static volatile uint64_t irq_base_ticks;
static volatile uint32_t irq_clock_live;

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb8(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint64_t read_tsc_counter(void) {
    uint32_t low, high;
    __asm__ volatile ("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}

/* Measure the TSC against one bounded PIT channel-2 one-shot. */
static uint64_t calibrate_tsc_per_tick(uint32_t frequency) {
    uint8_t original = inb8(PIT_GATE_PORT);
    outb(PIT_GATE_PORT, (uint8_t)(original & ~0x03U)); /* gate low, speaker off */
    outb(PIT_COMMAND_PORT, 0xB0);                      /* ch2, lo/hi, mode 0 */
    outb(PIT_CHANNEL2_PORT, (uint8_t)TSC_CAL_PIT_COUNTS);
    outb(PIT_CHANNEL2_PORT, (uint8_t)(TSC_CAL_PIT_COUNTS >> 8));
    uint64_t start = read_tsc_counter();
    outb(PIT_GATE_PORT, (uint8_t)((original & ~0x02U) | 0x01U)); /* gate high */
    uint32_t spins = 0;
    while ((inb8(PIT_GATE_PORT) & 0x20U) == 0U && spins++ < TSC_CAL_SPIN_LIMIT)
        __asm__ volatile ("pause" ::: "memory");
    uint64_t end = read_tsc_counter();
    outb(PIT_GATE_PORT, original);
    if (spins >= TSC_CAL_SPIN_LIMIT)
        return 0U;
    return vanta_timer_tsc_per_tick(end - start, TSC_CAL_PIT_COUNTS, frequency);
}

static uint64_t synthetic_ticks(void) {
    if (tsc_per_tick == 0U)
        return 0U;
    return vanta_timer_tsc_to_ticks(read_tsc_counter() - tsc_origin,
                                    tsc_per_tick);
}

int timer_initialize(uint32_t frequency) {
    uint16_t divisor;
    if (!vanta_timer_pit_divisor(frequency, &divisor))
        return 0;
    __atomic_store_n(&timer_freq, frequency, __ATOMIC_RELEASE);

    if (!irq_clock_live) {
        uint64_t per_tick = calibrate_tsc_per_tick(frequency);
        tsc_origin = read_tsc_counter();
        tsc_per_tick = per_tick;
    }

    // Command byte: Channel 0, Access mode lobyte/hibyte, Square wave mode
    outb(PIT_COMMAND_PORT, 0x36);

    // Send divisor bytes
    outb(PIT_DATA_PORT_0, (uint8_t)(divisor & 0xFF));
    outb(PIT_DATA_PORT_0, (uint8_t)((divisor >> 8) & 0xFF));
    return 1;
}

void timer_handle_interrupt(void) {
    if (!__atomic_load_n(&irq_clock_live, __ATOMIC_ACQUIRE)) {
        /* Continue from the TSC-derived time so deadlines stay valid. */
        __atomic_store_n(&irq_base_ticks, synthetic_ticks(), __ATOMIC_RELEASE);
        __atomic_store_n(&irq_clock_live, 1U, __ATOMIC_RELEASE);
    }
    uint64_t now = __atomic_load_n(&irq_base_ticks, __ATOMIC_ACQUIRE) +
                   __atomic_add_fetch(&ticks, 1ULL, __ATOMIC_RELAXED);
    kernel_watchdog_timer_tick((uint32_t)now);
    process_account_runtime_tick(process_current_pid());
    process_wake_expired(now);
}

uint64_t timer_get_ticks64(void) {
    if (!__atomic_load_n(&irq_clock_live, __ATOMIC_ACQUIRE))
        return synthetic_ticks();
    return __atomic_load_n(&irq_base_ticks, __ATOMIC_ACQUIRE) +
           __atomic_load_n(&ticks, __ATOMIC_ACQUIRE);
}

uint32_t timer_get_ticks(void) {
    return (uint32_t)timer_get_ticks64();
}

uint64_t timer_irq_ticks(void) {
    return __atomic_load_n(&ticks, __ATOMIC_ACQUIRE);
}

uint64_t timer_tsc_ticks_per_second(void) {
    return tsc_per_tick * __atomic_load_n(&timer_freq, __ATOMIC_ACQUIRE);
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
