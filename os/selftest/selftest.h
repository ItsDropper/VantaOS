/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef VANTAOS_SELFTEST_H
#define VANTAOS_SELFTEST_H

/* Entry points compiled and called only in explicit self-test builds. */
void os_selftest_initialize(void);
void os_selftest_run_smp_probes(void);
void filesystem_selftest_initialize(void);
void memory_selftest_initialize(void);

#endif
