#include "smp.h"

#include "acpi.h"
#include "cpu_local.h"
#include "apic.h"
#include "gdt.h"
#include "idt.h"
#include "paging.h"
#include "serial.h"
#include "process.h"
#include "scheduler.h"

#include <stddef.h>
#include <stdint.h>

#define AP_TRAMPOLINE_PHYSICAL 0x8000U
#define AP_MAILBOX_PHYSICAL 0x7000U
#define AP_STARTUP_VECTOR 0x08U
#define AP_STACK_SIZE 16384U
#define AP_STARTUP_SPIN_LIMIT 50000000U
#define SMP_WORK_PROBE_ITERATIONS 100000U
#define SMP_LOCAL_TIMER_VECTOR 0xF2U
#define SMP_LOCAL_TIMER_TARGET_HZ 100U
#define SMP_RESCHEDULE_IPI_VECTOR 0xF3U

typedef struct
{
    volatile uint64_t cr3;
    volatile uint64_t entry;
    volatile uint64_t stack_top;
} ap_startup_mailbox_t;

extern uint8_t ap_trampoline_blob_start[];
extern uint8_t ap_trampoline_blob_end[];
extern _Noreturn void smp_ap_entry(uint32_t apic_id);

static unsigned int discovered_cpu_count;
static uint32_t bsp_logical_id;
static uint32_t calibrated_timer_initial_count;
static uint8_t ap_stacks[ACPI_MAX_CPUS][AP_STACK_SIZE]
    __attribute__((aligned(16)));
static volatile uint32_t ap_online[ACPI_MAX_CPUS];

/*
 * APs remain interrupt-disabled during this first execution test. The BSP
 * releases them only after every AP has initialized its own LAPIC and GS
 * state. Each CPU then writes only its own cpu_local work counter.
 */
static volatile uint32_t work_probe_start;
static volatile uint32_t work_probe_done[ACPI_MAX_CPUS];

static int find_cpu_index(const acpi_platform_info_t* info, uint32_t apic_id)
{
    for (uint32_t i = 0; i < info->cpu_count; i++)
        if (info->cpu_apic_ids[i] == apic_id)
            return (int)i;
    return -1;
}

typedef enum
{
    SMP_TOPOLOGY_VALID = 0,
    SMP_TOPOLOGY_INVALID_COUNT,
    SMP_TOPOLOGY_DUPLICATE_APIC_ID,
    SMP_TOPOLOGY_BSP_MISSING,
    SMP_TOPOLOGY_UNSUPPORTED_APIC_ID
} smp_topology_status_t;

/*
 * Validate the complete topology before sending INIT/SIPI. x2APIC is not
 * implemented, so reject IDs that this xAPIC startup path cannot address.
 */
static smp_topology_status_t smp_validate_topology(
    const acpi_platform_info_t* info, uint32_t bsp_apic_id,
    int* bsp_index_out)
{
    if (!info || !bsp_index_out || info->cpu_count == 0U ||
        info->cpu_count > ACPI_MAX_CPUS ||
        info->cpu_count > cpu_local_capacity())
        return SMP_TOPOLOGY_INVALID_COUNT;

    int bsp_index = -1;
    for (uint32_t i = 0; i < info->cpu_count; i++)
    {
        uint32_t apic_id = info->cpu_apic_ids[i];
        if (apic_id > 255U)
            return SMP_TOPOLOGY_UNSUPPORTED_APIC_ID;

        for (uint32_t j = 0; j < i; j++)
            if (info->cpu_apic_ids[j] == apic_id)
                return SMP_TOPOLOGY_DUPLICATE_APIC_ID;

        if (apic_id == bsp_apic_id)
            bsp_index = (int)i;
    }

    if (bsp_index < 0)
        return SMP_TOPOLOGY_BSP_MISSING;

    *bsp_index_out = bsp_index;
    return SMP_TOPOLOGY_VALID;
}

#ifdef VANTAOS_SELF_TEST
static int smp_topology_validation_probe(void)
{
    acpi_platform_info_t test = {0};
    int bsp_index = -1;

    test.cpu_count = 2U;
    test.cpu_apic_ids[0] = 1U;
    test.cpu_apic_ids[1] = 2U;
    if (smp_validate_topology(&test, 1U, &bsp_index) !=
            SMP_TOPOLOGY_VALID || bsp_index != 0)
        goto fail;

    test.cpu_apic_ids[1] = 1U;
    if (smp_validate_topology(&test, 1U, &bsp_index) !=
        SMP_TOPOLOGY_DUPLICATE_APIC_ID)
        goto fail;

    test.cpu_apic_ids[1] = 2U;
    if (smp_validate_topology(&test, 3U, &bsp_index) !=
        SMP_TOPOLOGY_BSP_MISSING)
        goto fail;

    test.cpu_apic_ids[1] = 256U;
    if (smp_validate_topology(&test, 1U, &bsp_index) !=
        SMP_TOPOLOGY_UNSUPPORTED_APIC_ID)
        goto fail;

    test.cpu_count = ACPI_MAX_CPUS + 1U;
    if (smp_validate_topology(&test, 1U, &bsp_index) !=
        SMP_TOPOLOGY_INVALID_COUNT)
        goto fail;

    serial_write("VANTAOS_SMP_TOPOLOGY_VALIDATION_OK\n");
    return 1;

fail:
    serial_write("VANTAOS_SMP_TOPOLOGY_VALIDATION_FAIL\n");
    return 0;
}
#endif

static int run_local_work_probe(uint32_t expected_logical_id)
{
    cpu_local_t* local = cpu_local_current();
    if (!local || local->self != local ||
        local->logical_id != expected_logical_id ||
        local->online == 0U)
        return 0;

    uint64_t start = local->work_iterations;
    for (uint32_t i = 0; i < SMP_WORK_PROBE_ITERATIONS; i++)
    {
        local->work_iterations++;
        __asm__ volatile ("pause" ::: "memory");
    }

    return local->work_iterations - start == SMP_WORK_PROBE_ITERATIONS;
}


static _Noreturn void smp_idle_process_entry(void)
{
    for (;;)
        __asm__ volatile ("sti; hlt" ::: "memory");
}

int smp_scheduler_initialize(void)
{
    if (!process_is_initialized() || !scheduler_is_initialized())
        return 0;

    /*
     * APIC/SMP unavailability is a supported legacy fallback. There are no
     * per-CPU idle contexts to publish in that mode, so this phase is a
     * successful no-op rather than a scheduler bootstrap failure.
     */
    if (discovered_cpu_count == 0U)
        return 1;

    if (discovered_cpu_count > cpu_local_capacity())
        return 0;

    /* Build every pinned idle context before allowing any AP to switch. */
    for (uint32_t cpu = 0; cpu < discovered_cpu_count; cpu++)
    {
        const cpu_local_t* local = cpu_local_get(cpu);
        int idle_pid = process_create_idle_for_cpu(
            cpu, "cpu-idle", smp_idle_process_entry);
        process_t idle;
        if (!local || local->logical_id != cpu || idle_pid <= 0 ||
            !process_snapshot((uint32_t)idle_pid, &idle) ||
            idle.state != PROCESS_READY ||
            idle.owner_cpu != PROCESS_CPU_UNOWNED ||
            idle.affinity_cpu != cpu ||
            __atomic_load_n(&local->idle_pid, __ATOMIC_ACQUIRE) !=
                (uint32_t)idle_pid)
        {
            serial_write("VANTAOS_SMP_IDLE_CONTEXTS_FAIL\n");
            return 0;
        }
    }

    for (uint32_t cpu = 0; cpu < discovered_cpu_count; cpu++)
    {
        cpu_local_t* local = (cpu_local_t*)cpu_local_get(cpu);
        if (!local)
        {
            serial_write("VANTAOS_SMP_IDLE_CONTEXTS_FAIL\n");
            return 0;
        }
        __atomic_store_n(&local->scheduler_online, 1U, __ATOMIC_RELEASE);
    }

    serial_write("VANTAOS_SMP_IDLE_CONTEXTS_OK\n");
    return 1;
}

_Noreturn void smp_ap_entry(uint32_t apic_id)
{
    /*
     * The BSP constructs the shared GDT before starting APs. Load its
     * immutable descriptors locally; rebuilding the shared table here could
     * race with interrupts on CPUs that are already online.
     */
    gdt_load();
    idt_load();

    const acpi_platform_info_t* info = apic_get_platform_info();
    int index = info ? find_cpu_index(info, apic_id) : -1;

    /*
     * A CPU is not reported online until its own APIC MSR/registers and GS
     * per-CPU base have both been initialized and validated.
     */
    if (index >= 0 &&
        apic_initialize_cpu(apic_id) &&
        cpu_local_initialize((uint32_t)index, apic_id) &&
        apic_timer_initialize(SMP_LOCAL_TIMER_VECTOR,
                              calibrated_timer_initial_count))
    {
        __atomic_store_n(&ap_online[index], 1U, __ATOMIC_RELEASE);
    }
    else
    {
        serial_write("VANTAOS_SMP_AP_LOCAL_INIT_FAIL\n");
        for (;;)
            __asm__ volatile ("cli; hlt" ::: "memory");
    }

    /* Sleep with interrupts enabled until the BSP sends the wakeup IPI. */
    while (__atomic_load_n(&work_probe_start, __ATOMIC_ACQUIRE) == 0U)
        __asm__ volatile ("sti; hlt" ::: "memory");

    if (!run_local_work_probe((uint32_t)index))
    {
        serial_write("VANTAOS_SMP_WORK_FAIL\n");
        for (;;)
            __asm__ volatile ("cli; hlt" ::: "memory");
    }

    __atomic_store_n(&work_probe_done[index], 1U, __ATOMIC_RELEASE);

    /* Work execution is proven; general scheduling is intentionally not enabled. */
    for (;;)
        __asm__ volatile ("sti; hlt" ::: "memory");
}

static int start_secondary_cpus(const acpi_platform_info_t* info)
{
    if (!apic_is_enabled())
        return 0;

    size_t trampoline_size =
        (size_t)(ap_trampoline_blob_end - ap_trampoline_blob_start);
    if (trampoline_size == 0 || trampoline_size > PAGE_SIZE)
    {
        serial_write("VANTAOS_SMP_TRAMPOLINE_SIZE_FAIL\n");
        return 0;
    }

    volatile uint8_t* destination =
        (volatile uint8_t*)(uintptr_t)AP_TRAMPOLINE_PHYSICAL;
    for (size_t i = 0; i < trampoline_size; i++)
        destination[i] = ap_trampoline_blob_start[i];

    ap_startup_mailbox_t* mailbox =
        (ap_startup_mailbox_t*)(uintptr_t)AP_MAILBOX_PHYSICAL;
    mailbox->cr3 = paging_get_kernel_address_space()->directory_physical;
    mailbox->entry = (uintptr_t)&smp_ap_entry;

    uint32_t bsp_id = apic_get_bsp_id();
    if (find_cpu_index(info, bsp_id) < 0)
    {
        serial_write("VANTAOS_SMP_BSP_NOT_IN_MADT_FAIL\n");
        return 0;
    }
    unsigned int online_count = 1U;

    if (info->cpu_count > 1U && !apic_send_init_all())
    {
        serial_write("VANTAOS_SMP_AP_START_FAIL\n");
        return 0;
    }

    for (uint32_t i = 0; i < info->cpu_count; i++)
    {
        uint32_t apic_id = info->cpu_apic_ids[i];
        if (apic_id == bsp_id)
            continue;

        __atomic_store_n(&ap_online[i], 0U, __ATOMIC_RELAXED);
        mailbox->stack_top =
            (uintptr_t)&ap_stacks[i][AP_STACK_SIZE];
        __asm__ volatile ("mfence" ::: "memory");

        serial_write("VANTAOS_SMP_AP_IPI_BEGIN\n");
        if (!apic_send_sipi(apic_id, AP_STARTUP_VECTOR))
        {
            serial_write("VANTAOS_SMP_AP_START_FAIL\n");
            return 0;
        }

        serial_write("VANTAOS_SMP_AP_IPI_SENT\n");
        uint32_t spin = 0;
        while (__atomic_load_n(&ap_online[i], __ATOMIC_ACQUIRE) == 0U &&
               spin++ < AP_STARTUP_SPIN_LIMIT)
            __asm__ volatile ("pause");

        if (__atomic_load_n(&ap_online[i], __ATOMIC_ACQUIRE) == 0U)
        {
            serial_write("VANTAOS_SMP_AP_START_FAIL\n");
            return 0;
        }

        serial_write("VANTAOS_SMP_AP_ONLINE\n");
        online_count++;
    }

    if (online_count != info->cpu_count)
    {
        serial_write("VANTAOS_SMP_AP_START_FAIL\n");
        return 0;
    }

    serial_write("VANTAOS_SMP_AP_START_OK\n");
    return 1;
}

static int smp_wait_for_reschedule_ipis(const acpi_platform_info_t* info)
{
    uint32_t expected[ACPI_MAX_CPUS] = {0};
    for (uint32_t i = 0; i < info->cpu_count; i++)
    {
        const cpu_local_t* local = cpu_local_get(i);
        if (!local)
            return 0;
        if (local->apic_id == apic_get_bsp_id())
            continue;
        expected[i] = __atomic_load_n(&local->reschedule_ipi_ticks,
                                      __ATOMIC_ACQUIRE) + 1U;
        if (!apic_send_reschedule_ipi(local->apic_id))
        {
            serial_write("VANTAOS_SMP_RESCHEDULE_IPI_FAIL\n");
            return 0;
        }
    }

    uint32_t spin = 0;
    int ready = 0;
    while (spin++ < AP_STARTUP_SPIN_LIMIT)
    {
        ready = 1;
        for (uint32_t i = 0; i < info->cpu_count; i++)
        {
            if (expected[i] == 0U)
                continue;
            const cpu_local_t* local = cpu_local_get(i);
            if (!local || __atomic_load_n(&local->reschedule_ipi_ticks,
                                          __ATOMIC_ACQUIRE) < expected[i])
            {
                ready = 0;
                break;
            }
        }
        if (ready)
            break;
        __asm__ volatile ("pause" ::: "memory");
    }

    if (!ready)
    {
        serial_write("VANTAOS_SMP_RESCHEDULE_IPI_FAIL\n");
        return 0;
    }
    int pending_ok = 1;
    for (uint32_t i = 0; i < info->cpu_count; i++)
    {
        if (expected[i] == 0U)
            continue;
        const cpu_local_t* local = cpu_local_get(i);
        if (!local ||
            __atomic_load_n(&local->reschedule_pending,
                            __ATOMIC_ACQUIRE) == 0U)
            pending_ok = 0;
    }

    if (!pending_ok)
    {
        serial_write("VANTAOS_SMP_RESCHEDULE_PENDING_FAIL\n");
        return 0;
    }
    serial_write("VANTAOS_SMP_RESCHEDULE_PENDING_OK\n");
    serial_write("VANTAOS_SMP_RESCHEDULE_IPI_OK\n");
    return 1;
}

static int smp_wait_for_local_timers(const acpi_platform_info_t* info)
{
    uint64_t saved_flags;
    __asm__ volatile ("pushfq; popq %0" : "=r"(saved_flags));
    __asm__ volatile ("sti" ::: "memory");

    uint32_t spin = 0;
    int ready = 0;
    while (spin++ < 250000000U)
    {
        ready = 1;
        for (uint32_t i = 0; i < info->cpu_count; i++)
        {
            const cpu_local_t* local = cpu_local_get(i);
            if (!local ||
                __atomic_load_n(&local->lapic_timer_ticks,
                                __ATOMIC_ACQUIRE) < 2U)
            {
                ready = 0;
                break;
            }
        }
        if (ready)
            break;
        __asm__ volatile ("pause" ::: "memory");
    }

    if ((saved_flags & (1ULL << 9)) == 0U)
        __asm__ volatile ("cli" ::: "memory");

    if (!ready)
    {
        serial_write("VANTAOS_SMP_LOCAL_TIMER_FAIL\n");
        return 0;
    }
    serial_write("VANTAOS_SMP_LOCAL_TIMER_OK\n");
    return smp_wait_for_reschedule_ipis(info);
}

static int smp_run_work_probe(const acpi_platform_info_t* info,
                              uint32_t bsp_index)
{
    if (!info || info->cpu_count == 0 ||
        info->cpu_count > CPU_LOCAL_MAX ||
        cpu_local_current() != cpu_local_get(bsp_index))
    {
        serial_write("VANTAOS_SMP_WORK_FAIL\n");
        return 0;
    }

    __atomic_store_n(&work_probe_start, 1U, __ATOMIC_RELEASE);

    /* Wake APs with a dedicated fixed IPI; do not make them burn TCG time
       spinning while the BSP starts the remaining processors. */
    for (uint32_t i = 0; i < info->cpu_count; i++)
    {
        if (i == bsp_index)
            continue;
        const cpu_local_t* local = cpu_local_get(i);
        if (!local || !apic_send_fixed_ipi(local->apic_id, 0xF1U))
        {
            serial_write("VANTAOS_SMP_WORK_FAIL\n");
            return 0;
        }
    }

    if (!run_local_work_probe(bsp_index))
    {
        serial_write("VANTAOS_SMP_WORK_FAIL\n");
        return 0;
    }

    uint32_t spin = 0;
    for (uint32_t i = 0; i < info->cpu_count; i++)
    {
        if (i == bsp_index)
            continue;

        while (__atomic_load_n(&work_probe_done[i], __ATOMIC_ACQUIRE) == 0U &&
               spin++ < AP_STARTUP_SPIN_LIMIT)
            __asm__ volatile ("pause");

        if (__atomic_load_n(&work_probe_done[i], __ATOMIC_ACQUIRE) == 0U)
        {
            serial_write("VANTAOS_SMP_WORK_FAIL\n");
            return 0;
        }

        const cpu_local_t* local = cpu_local_get(i);
        if (!local || local->logical_id != i ||
            local->work_iterations != SMP_WORK_PROBE_ITERATIONS)
        {
            serial_write("VANTAOS_SMP_WORK_FAIL\n");
            return 0;
        }
    }

    const cpu_local_t* bsp = cpu_local_get(bsp_index);
    if (!bsp || bsp->work_iterations != SMP_WORK_PROBE_ITERATIONS)
    {
        serial_write("VANTAOS_SMP_WORK_FAIL\n");
        return 0;
    }

    serial_write("VANTAOS_SMP_WORK_OK\n");
    return smp_wait_for_local_timers(info);
}

smp_init_result_t smp_initialize(void)
{
#ifdef VANTAOS_SELF_TEST
    if (!smp_topology_validation_probe())
        return SMP_INIT_FAILED;
#endif

    /*
     * ACPI/APIC may be unavailable on older machines. Preserve the existing
     * single-CPU fallback only before any local CPU state or AP startup is
     * committed.
     */
    if (!apic_is_enabled())
    {
        serial_write("VANTAOS_SMP_AP_START_SKIPPED\n");
        return SMP_INIT_UNAVAILABLE;
    }

    const acpi_platform_info_t* info = apic_get_platform_info();
    if (!info)
    {
        serial_write("VANTAOS_SMP_TOPOLOGY_UNAVAILABLE\n");
        return SMP_INIT_UNAVAILABLE;
    }

    uint32_t bsp_id = apic_get_bsp_id();
    int bsp_index = -1;
    smp_topology_status_t topology_status =
        smp_validate_topology(info, bsp_id, &bsp_index);
    if (topology_status != SMP_TOPOLOGY_VALID)
    {
        switch (topology_status)
        {
            case SMP_TOPOLOGY_INVALID_COUNT:
                serial_write("VANTAOS_SMP_TOPOLOGY_COUNT_FAIL\n");
                break;
            case SMP_TOPOLOGY_DUPLICATE_APIC_ID:
                serial_write("VANTAOS_SMP_TOPOLOGY_DUPLICATE_FAIL\n");
                break;
            case SMP_TOPOLOGY_BSP_MISSING:
                serial_write("VANTAOS_SMP_BSP_NOT_IN_MADT_FAIL\n");
                break;
            case SMP_TOPOLOGY_UNSUPPORTED_APIC_ID:
                serial_write("VANTAOS_SMP_APIC_ID_UNSUPPORTED\n");
                break;
            case SMP_TOPOLOGY_VALID:
                break;
        }
        return SMP_INIT_FAILED;
    }

    discovered_cpu_count = info->cpu_count;
    bsp_logical_id = (uint32_t)bsp_index;
    serial_write("VANTAOS_SMP_TOPOLOGY_OK\n");

    if (!cpu_local_initialize((uint32_t)bsp_index, bsp_id))
    {
        serial_write("VANTAOS_SMP_CPU_LOCAL_FAIL\n");
        return SMP_INIT_FAILED;
    }

    if (!apic_timer_calibrate(SMP_LOCAL_TIMER_VECTOR,
                              SMP_LOCAL_TIMER_TARGET_HZ,
                              &calibrated_timer_initial_count) ||
        calibrated_timer_initial_count == 0U)
    {
        serial_write("VANTAOS_SMP_TIMER_CALIBRATION_FAIL\n");
        return SMP_INIT_FAILED;
    }
    serial_write("VANTAOS_SMP_TIMER_CALIBRATION_OK\n");

    if (!apic_timer_initialize(SMP_LOCAL_TIMER_VECTOR,
                               calibrated_timer_initial_count))
    {
        serial_write("VANTAOS_SMP_CPU_LOCAL_FAIL\n");
        return SMP_INIT_FAILED;
    }

    if (!start_secondary_cpus(info))
    {
        /* APIC was active before startup; any failure here is fatal. */
        return SMP_INIT_FAILED;
    }

    for (uint32_t i = 0; i < info->cpu_count; i++)
    {
        const cpu_local_t* local = cpu_local_get(i);
        if (!local || local->logical_id != i ||
            local->apic_id != info->cpu_apic_ids[i])
        {
            serial_write("VANTAOS_SMP_CPU_LOCAL_FAIL\n");
            return SMP_INIT_FAILED;
        }
    }

    serial_write("VANTAOS_SMP_CPU_LOCAL_OK\n");
#ifdef VANTAOS_SELF_TEST
    if (info->cpu_count == 1U)
    {
        if (!apic_send_nmi_all())
        {
            serial_write("VANTAOS_SMP_NMI_BROADCAST_PROBE_FAIL\n");
            return SMP_INIT_FAILED;
        }
        serial_write("VANTAOS_SMP_NMI_BROADCAST_PROBE_OK\n");
    }
#endif
    if (!smp_run_work_probe(info, (uint32_t)bsp_index))
        return SMP_INIT_FAILED;

    return SMP_INIT_OK;
}

int smp_cpu_is_bsp(uint32_t logical_id)
{
    return logical_id == bsp_logical_id;
}

unsigned int smp_cpu_count(void)
{
    return discovered_cpu_count;
}
