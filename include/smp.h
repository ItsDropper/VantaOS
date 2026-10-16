#ifndef SMP_H
#define SMP_H

/* Discover firmware CPU topology. Secondary CPU startup is a later phase. */
int smp_initialize(void);
/* Install pinned idle contexts after the process manager is initialized. */
int smp_scheduler_initialize(void);
unsigned int smp_cpu_count(void);
int smp_cpu_is_bsp(uint32_t logical_id);

#endif
