#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>

void scheduler_initialize(void);
int scheduler_is_initialized(void);
uint64_t scheduler_tick(uint64_t current_stack);
/* Force a scheduling decision from an interrupt-safe saved stack frame. */
uint64_t scheduler_reschedule(uint64_t current_stack);
/* Start an AP from its bootstrap stack on its CPU-affine idle process. */
uint64_t scheduler_start_cpu(uint64_t interrupted_stack);
void scheduler_yield(void);

#endif
