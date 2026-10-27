/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "kernel_init.h"
#include "ktrace.h"

#include "apic.h"
#include "cpu_features.h"
#include "drivers_init.h"
#include "gdt.h"
#include "heap.h"
#include "interrupts.h"
#include "paging.h"
#include "panic.h"
#include "pci.h"
#include "pmm.h"
#include "serial.h"
#include "smp.h"
#ifdef VANTAOS_SELF_TEST
#include "selftest/selftest.h"
#endif

static inline unsigned long long read_tsc(void)
{
    unsigned int low;
    unsigned int high;

    __asm__ volatile (
        "rdtsc"
        : "=a"(low), "=d"(high)
    );

    return ((unsigned long long)high << 32) | low;
}

static unsigned long long boot_gdt;
static unsigned long long boot_interrupts;

void kernel_initialize(const vanta_boot_info_t* boot_info)
{
    ktrace_emit(KTRACE_EVENT_BOOT_PHASE, KTRACE_CPU_UNKNOWN, 0U, 1U);
    /* Inventory capabilities before any subsystem considers optional ISA. */
    if (!cpu_features_initialize())
        serial_write("VANTAOS_CPU_DISCOVERY_UNAVAILABLE\n");

    /* Phase 1: CPU tables and physical memory must precede paging setup. */
    gdt_initialize();
    boot_gdt = read_tsc();
    ktrace_emit(KTRACE_EVENT_BOOT_PHASE, KTRACE_CPU_UNKNOWN, 1U, 2U);

    pmm_initialize(boot_info);
    ktrace_emit(KTRACE_EVENT_BOOT_PHASE, KTRACE_CPU_UNKNOWN, 2U,
                pmm_is_initialized() ? 1U : 0U);
    interrupts_initialize();
    boot_interrupts = read_tsc();

    /* Phase 2: virtual memory, interrupt controller, and CPU topology. */
    paging_initialize();
    apic_initialize();
    smp_init_result_t smp_result = smp_initialize();
    ktrace_emit(KTRACE_EVENT_BOOT_PHASE, KTRACE_CPU_UNKNOWN, 3U,
                (uint64_t)smp_result);
    if (smp_result == SMP_INIT_FAILED)
    {
        serial_write("VANTAOS_SMP_INIT_FATAL_FAIL\n");
        kernel_panic_halt_all();
    }
#ifdef VANTAOS_SELF_TEST
    kernel_platform_selftest();
#endif

    /* Phase 3: bus discovery and allocation services precede device drivers. */
    pci_initialize();
    ktrace_emit(KTRACE_EVENT_BOOT_PHASE, KTRACE_CPU_UNKNOWN, 4U, 1U);
    heap_initialize();

    /* Phase 4: hand ownership to the bus/device subsystem coordinators. */
    drivers_initialize();
    ktrace_emit(KTRACE_EVENT_DRIVER_INIT, KTRACE_CPU_UNKNOWN, 1U, 0U);
    ktrace_emit(KTRACE_EVENT_BOOT_PHASE, KTRACE_CPU_UNKNOWN, 5U, 1U);
}

unsigned long long kernel_boot_gdt(void)
{
    return boot_gdt;
}

unsigned long long kernel_boot_interrupts(void)
{
    return boot_interrupts;
}
