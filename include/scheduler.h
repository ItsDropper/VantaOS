#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>

void scheduler_initialize(void);
int scheduler_is_initialized(void);

/*
 * Called from the timer interrupt with the address of the PUSHA frame.
 * The returned value is the stack pointer the IRQ stub must restore.
 */
uint64_t scheduler_tick(uint64_t current_stack);

/*
 * Mark the current task as ready and request a switch on the next
 * scheduler tick.
 */
void scheduler_yield(void);

#endif
