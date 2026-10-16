#ifndef SMP_H
#define SMP_H

#include <stdint.h>

/* Discover topology and start APs into the kernel's idle process contexts. */
int smp_initialize(void);
unsigned int smp_cpu_count(void);
uint32_t smp_bsp_logical_id(void);
int smp_start_scheduler_cpus(void);

#endif
