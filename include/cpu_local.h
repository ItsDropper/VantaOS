#ifndef CPU_LOCAL_H
#define CPU_LOCAL_H

#include <stdint.h>

#define CPU_LOCAL_MAX 32U

typedef struct cpu_local
{
    /* Must remain first: cpu_local_current reads this through GS:0. */
    struct cpu_local* self;
    uint32_t logical_id;
    uint32_t apic_id;
    volatile uint32_t online;
    volatile uint32_t current_pid;
    volatile uint32_t interrupt_depth;
    uint32_t reserved;
} cpu_local_t;

/* Installs the current CPU's GS base and initializes its private state. */
int cpu_local_initialize(uint32_t logical_id, uint32_t apic_id);
cpu_local_t* cpu_local_current(void);
const cpu_local_t* cpu_local_get(uint32_t logical_id);
unsigned int cpu_local_capacity(void);

#endif
