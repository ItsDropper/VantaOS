#ifndef SMP_H
#define SMP_H

#include <stdint.h>

typedef enum
{
    SMP_INIT_FAILED = -1,
    SMP_INIT_UNAVAILABLE = 0,
    SMP_INIT_OK = 1
} smp_init_result_t;

/*
 * Initialize the BSP/AP topology. UNAVAILABLE permits the legacy
 * uniprocessor fallback; FAILED is a fatal partial/invalid initialization.
 */
smp_init_result_t smp_initialize(void);
/* Install pinned idle contexts after the process manager is initialized. */
int smp_scheduler_initialize(void);
unsigned int smp_cpu_count(void);
int smp_cpu_is_bsp(uint32_t logical_id);

#endif
