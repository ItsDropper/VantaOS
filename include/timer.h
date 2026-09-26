#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

/**
 * @brief Configures IRQ0 to fire at the specified frequency in Hz.
 */
void timer_initialize(uint32_t frequency);

/**
 * @brief Called by IRQ0 handler in interrupts.c.
 */
void timer_handle_interrupt(void);

/**
 * @brief Blocks execution for the specified duration in milliseconds.
 */
void sleep_ms(uint32_t ms);

/**
 * @brief Returns total ticks since boot.
 */
uint32_t timer_get_ticks(void);

#endif // TIMER_H