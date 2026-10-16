#ifndef SMP_H
#define SMP_H

/* Discover firmware CPU topology. Secondary CPU startup is a later phase. */
int smp_initialize(void);
unsigned int smp_cpu_count(void);

#endif
