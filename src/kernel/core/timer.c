#include "timer.h"
#include <stdint.h>

#define PIT_COMMAND_PORT 0x43
#define PIT_DATA_PORT_0  0x40
#define PIT_BASE_FREQ    1193182

static volatile uint32_t ticks = 0;
static uint32_t timer_freq = 100; // Default 100 Hz

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

void timer_initialize(uint32_t frequency) {
    timer_freq = frequency;
    uint32_t divisor = PIT_BASE_FREQ / frequency;

    // Command byte: Channel 0, Access mode lobyte/hibyte, Square wave mode
    outb(PIT_COMMAND_PORT, 0x36);

    // Send divisor bytes
    outb(PIT_DATA_PORT_0, (uint8_t)(divisor & 0xFF));
    outb(PIT_DATA_PORT_0, (uint8_t)((divisor >> 8) & 0xFF));
}

void timer_handle_interrupt(void) {
    ticks++;
}

uint32_t timer_get_ticks(void) {
    return ticks;
}

void sleep_ms(uint32_t ms) {
    uint32_t target_ticks = ticks + (ms * timer_freq) / 1000;
    while (ticks < target_ticks) {
        __asm__ volatile ("hlt");
    }
}