#ifndef VANTAOS_SELFTEST_H
#define VANTAOS_SELFTEST_H

/* Entry points compiled and called only in explicit self-test builds. */
void os_selftest_initialize(void);
void os_selftest_run_smp_probes(void);
void filesystem_selftest_initialize(void);
void memory_selftest_initialize(void);

#endif
