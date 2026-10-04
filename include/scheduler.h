#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>

void scheduler_initialize(void);
int scheduler_is_initialized(void);
uint64_t scheduler_tick(uint64_t current_stack);
void scheduler_yield(void);

#endif
