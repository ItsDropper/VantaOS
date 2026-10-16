/*
 * Test-only kernel regression probes. This translation unit is empty in
 * ordinary builds; VANTAOS_SELF_TEST enables the test implementation.
 */
#include "desktop_process.h"
#include "filesystem.h"
#include "fat32.h"
#include "heap.h"
#include "graphics.h"
#include "keyboard.h"
#include "mouse.h"
#include "process.h"
#include "cpu_local.h"
#include "smp.h"
#include "panic.h"
#include "spinlock.h"
#include "pmm.h"
#include "serial.h"
#include "scheduler.h"
#include "shell.h"
#include "terminal.h"
#include "terminal_process.h"
#include "vfs.h"

#ifdef VANTAOS_SELF_TEST
#define SMP_SCHEDULER_LOCK_PROBE_ITERATIONS 64U

static uint32_t smp_scheduler_probe_task_count;
static uint32_t smp_scheduler_probe_ap_cpu;
static spinlock_t smp_scheduler_probe_lock = SPINLOCK_INITIALIZER;
static uint32_t smp_scheduler_probe_locked_count;
static volatile uint32_t smp_scheduler_probe_completed;
static volatile uint32_t smp_scheduler_probe_cpu_mask;
static volatile uint32_t smp_scheduler_probe_active;
static volatile uint32_t smp_scheduler_probe_max_active;
static volatile uint32_t smp_scheduler_probe_duplicates;
static volatile uint32_t smp_scheduler_probe_seen[PROCESS_MAX];

static void smp_scheduler_probe_entry(void)
{
    uint32_t pid = process_current_pid();
    if (pid == 0U || pid >= PROCESS_MAX ||
        __atomic_exchange_n(&smp_scheduler_probe_seen[pid], 1U,
                            __ATOMIC_ACQ_REL) != 0U)
        __atomic_add_fetch(&smp_scheduler_probe_duplicates, 1U,
                           __ATOMIC_RELAXED);

    cpu_local_t* local = cpu_local_current();
    if (local && local->self == local && local->online != 0U)
        __atomic_fetch_or(&smp_scheduler_probe_cpu_mask,
                          1U << local->logical_id, __ATOMIC_RELEASE);

    /*
     * Do not barrier here: a barrier can occupy every available process slot
     * with waiters before the scheduler gets a useful overlap window. Let each
     * CPU enter the measured section as soon as its probe is scheduled.
     *
     * Keep this section non-preemptible: active > 1 can then only happen when
     * two distinct CPUs execute probe entries concurrently.
     */
    __asm__ volatile ("cli" ::: "memory");
    uint32_t active = __atomic_add_fetch(&smp_scheduler_probe_active, 1U,
                                         __ATOMIC_ACQ_REL);
    uint32_t observed = __atomic_load_n(&smp_scheduler_probe_max_active,
                                        __ATOMIC_RELAXED);
    while (active > observed &&
           !__atomic_compare_exchange_n(&smp_scheduler_probe_max_active,
                                        &observed, active, 0,
                                        __ATOMIC_RELAXED,
                                        __ATOMIC_RELAXED))
    {
    }

    /*
     * In multicore runs, hold the first entrant in this non-preemptible
     * section until another CPU enters. This makes the overlap assertion
     * deterministic instead of depending on QEMU timing. Bound the wait so a
     * broken scheduler reports a failure rather than hanging forever.
     */
    if (smp_cpu_count() > 1U)
    {
        uint32_t overlap_wait = 0;
        while (__atomic_load_n(&smp_scheduler_probe_active,
                               __ATOMIC_ACQUIRE) < 2U &&
               overlap_wait++ < 10000000U)
            __asm__ volatile ("pause" ::: "memory");
    }

    /*
     * Exercise the shared ticket lock while the task set is known to overlap
     * on distinct CPUs. A lost increment or lock corruption is caught by the
     * exact final counter check in smp_scheduler_probe_wait().
     */
    for (uint32_t i = 0; i < SMP_SCHEDULER_LOCK_PROBE_ITERATIONS; i++)
    {
        uint64_t flags = spinlock_lock_irqsave(&smp_scheduler_probe_lock);
        smp_scheduler_probe_locked_count++;
        spinlock_unlock_irqrestore(&smp_scheduler_probe_lock, flags);
    }

    for (uint32_t i = 0; i < 50000U; i++)
        __asm__ volatile ("pause" ::: "memory");

    __atomic_sub_fetch(&smp_scheduler_probe_active, 1U, __ATOMIC_ACQ_REL);
    __asm__ volatile ("sti" ::: "memory");
    __atomic_add_fetch(&smp_scheduler_probe_completed, 1U, __ATOMIC_RELEASE);
}

static int smp_scheduler_probe_initialize(void)
{
    serial_write("VANTAOS_SMP_STRESS_INIT_BEGIN\n");
    smp_scheduler_probe_completed = 0;
    smp_scheduler_probe_locked_count = 0;
    smp_scheduler_probe_ap_cpu = UINT32_MAX;
    smp_scheduler_probe_cpu_mask = 0;
    smp_scheduler_probe_active = 0;
    smp_scheduler_probe_max_active = 0;
    smp_scheduler_probe_duplicates = 0;
    for (uint32_t pid = 0; pid < PROCESS_MAX; pid++)
        __atomic_store_n(&smp_scheduler_probe_seen[pid], 0U,
                         __ATOMIC_RELAXED);

    for (uint32_t cpu = 0; cpu < smp_cpu_count(); cpu++)
    {
        if (!smp_cpu_is_bsp(cpu) && cpu_local_get(cpu))
        {
            smp_scheduler_probe_ap_cpu = cpu;
            break;
        }
    }
    if (smp_scheduler_probe_ap_cpu == UINT32_MAX)
    {
        serial_write("VANTAOS_SMP_SCHEDULER_STRESS_FAIL\n");
        return 0;
    }

    serial_write("VANTAOS_SMP_STRESS_AP_SELECTED\n");
    smp_scheduler_probe_task_count = smp_cpu_count() * 8U;
    if (smp_scheduler_probe_task_count < 4U)
        smp_scheduler_probe_task_count = 4U;
    if (smp_scheduler_probe_task_count > 64U)
        smp_scheduler_probe_task_count = 64U;

    for (uint32_t i = 0; i < smp_scheduler_probe_task_count; i++)
    {
        /* Pin one probe to each side so the overlap test cannot depend on
         * the scheduler opportunistically choosing an AP for ANY work. */
        int pid = i == 0U ?
            process_create_kernel_on_cpu("smp-scheduler-probe-bsp",
                                         process_current_pid(),
                                         smp_scheduler_probe_entry,
                                         process_current_cpu_id()) :
            i == 1U ?
            process_create_kernel_on_cpu("smp-scheduler-probe-ap",
                                         process_current_pid(),
                                         smp_scheduler_probe_entry,
                                         smp_scheduler_probe_ap_cpu) :
            process_create_kernel_any("smp-scheduler-probe",
                                      process_current_pid(),
                                      smp_scheduler_probe_entry);
        if (pid < 0)
        {
            serial_write("VANTAOS_SMP_SCHEDULER_STRESS_FAIL\n");
            return 0;
        }
    }
    serial_write("VANTAOS_SMP_STRESS_TASKS_CREATED\n");
    return 1;
}

static int smp_scheduler_probe_wait(void)
{
    serial_write("VANTAOS_SMP_STRESS_WAIT_BEGIN\n");
    uint32_t attempts = 0;
    while (attempts++ < 1500U &&
           __atomic_load_n(&smp_scheduler_probe_completed,
                           __ATOMIC_ACQUIRE) <
               smp_scheduler_probe_task_count)
        __asm__ volatile ("sti; hlt" ::: "memory");

    uint32_t completed = __atomic_load_n(&smp_scheduler_probe_completed,
                                         __ATOMIC_ACQUIRE);
    uint32_t cpu_mask = __atomic_load_n(&smp_scheduler_probe_cpu_mask,
                                        __ATOMIC_ACQUIRE);
    uint32_t cpus_seen = 0;
    while (cpu_mask != 0U)
    {
        cpus_seen += cpu_mask & 1U;
        cpu_mask >>= 1;
    }

    uint32_t required_cpus = smp_cpu_count() > 1U ? 2U : 1U;
    uint32_t max_active = __atomic_load_n(&smp_scheduler_probe_max_active,
                                          __ATOMIC_ACQUIRE);
    uint32_t duplicates = __atomic_load_n(&smp_scheduler_probe_duplicates,
                                          __ATOMIC_ACQUIRE);
    uint32_t expected_locked_count =
        smp_scheduler_probe_task_count * SMP_SCHEDULER_LOCK_PROBE_ITERATIONS;
    uint32_t locked_count = smp_scheduler_probe_locked_count;
    serial_write("VANTAOS_SMP_STRESS_WAIT_DONE\n");
    if (completed != smp_scheduler_probe_task_count)
    {
        serial_write("VANTAOS_SMP_STRESS_COMPLETION_FAIL\n");
        return 0;
    }
    if (cpus_seen < required_cpus)
    {
        serial_write("VANTAOS_SMP_STRESS_CPU_COVERAGE_FAIL\n");
        return 0;
    }
    if (duplicates != 0U)
    {
        serial_write("VANTAOS_SMP_STRESS_DUPLICATE_FAIL\n");
        return 0;
    }
    if (max_active < required_cpus)
    {
        serial_write("VANTAOS_SMP_STRESS_OVERLAP_FAIL\n");
        return 0;
    }
    if (locked_count != expected_locked_count)
    {
        serial_write("VANTAOS_SMP_STRESS_LOCK_COUNT_FAIL\n");
        return 0;
    }

    serial_write("VANTAOS_SMP_SCHEDULER_STRESS_OK\n");
    return 1;
}

static uint32_t smp_remote_wake_target_cpu;
static volatile uint32_t smp_remote_wake_target_pid;
static volatile uint32_t smp_remote_wake_target_done;
static volatile uint32_t smp_remote_wake_helper_done;
static volatile uint32_t smp_remote_wake_ok;

static void smp_remote_wake_target_entry(void)
{
    uint32_t pid = process_current_pid();
    __atomic_store_n(&smp_remote_wake_target_pid, pid, __ATOMIC_RELEASE);
    process_block_current();

    for (uint32_t spin = 0; spin < 100000000U; spin++)
    {
        process_t snapshot;
        if (process_snapshot(pid, &snapshot) &&
            (snapshot.state == PROCESS_READY ||
             snapshot.state == PROCESS_RUNNING))
        {
            __atomic_store_n(&smp_remote_wake_target_done, 1U,
                             __ATOMIC_RELEASE);
            return;
        }
        __asm__ volatile ("pause" ::: "memory");
    }
}

static void smp_remote_wake_helper_entry(void)
{
    uint32_t target_pid = 0;
    for (uint32_t spin = 0; spin < 100000000U; spin++)
    {
        target_pid = __atomic_load_n(&smp_remote_wake_target_pid,
                                     __ATOMIC_ACQUIRE);
        if (target_pid != 0U)
            break;
        __asm__ volatile ("pause" ::: "memory");
    }

    const cpu_local_t* target_cpu = cpu_local_get(smp_remote_wake_target_cpu);
    int ok = target_pid != 0U && target_cpu != NULL;
    process_t target;
    int sleeping = 0;
    for (uint32_t spin = 0; ok && spin < 100000000U; spin++)
    {
        if (process_snapshot(target_pid, &target) &&
            target.state == PROCESS_SLEEPING)
        {
            sleeping = 1;
            break;
        }
        __asm__ volatile ("pause" ::: "memory");
    }

    uint32_t ipi_before = target_cpu ?
        __atomic_load_n(&target_cpu->reschedule_ipi_ticks, __ATOMIC_ACQUIRE) :
        0U;
    if (!sleeping || !process_wake(target_pid))
        ok = 0;

    int ipi_seen = 0;
    for (uint32_t spin = 0; ok && spin < 100000000U; spin++)
    {
        uint32_t delivered = __atomic_load_n(
            &target_cpu->reschedule_ipi_ticks, __ATOMIC_ACQUIRE);
        if (delivered > ipi_before &&
            __atomic_load_n(&smp_remote_wake_target_done,
                            __ATOMIC_ACQUIRE) != 0U)
        {
            ipi_seen = 1;
            break;
        }
        __asm__ volatile ("pause" ::: "memory");
    }

    __atomic_store_n(&smp_remote_wake_ok, ok && ipi_seen ? 1U : 0U,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&smp_remote_wake_helper_done, 1U, __ATOMIC_RELEASE);
}

static int smp_remote_wake_probe_initialize(void)
{
    smp_remote_wake_target_pid = 0;
    smp_remote_wake_target_done = 0;
    smp_remote_wake_helper_done = 0;
    smp_remote_wake_ok = 0;
    smp_remote_wake_target_cpu = UINT32_MAX;

    for (uint32_t cpu = 0; cpu < smp_cpu_count(); cpu++)
    {
        if (!smp_cpu_is_bsp(cpu) && cpu_local_get(cpu))
        {
            smp_remote_wake_target_cpu = cpu;
            break;
        }
    }

    if (smp_remote_wake_target_cpu == UINT32_MAX ||
        process_create_kernel_on_cpu("remote-wake-target",
                                     process_current_pid(),
                                     smp_remote_wake_target_entry,
                                     smp_remote_wake_target_cpu) < 0 ||
        process_create_kernel("remote-wake-helper", process_current_pid(),
                              smp_remote_wake_helper_entry) < 0)
    {
        serial_write("VANTAOS_SMP_REMOTE_WAKE_FAIL\n");
        return 0;
    }
    return 1;
}

static int smp_remote_wake_probe_wait(void)
{
    uint32_t attempts = 0;
    while (attempts++ < 1500U &&
           __atomic_load_n(&smp_remote_wake_helper_done,
                           __ATOMIC_ACQUIRE) == 0U)
        __asm__ volatile ("sti; hlt" ::: "memory");

    if (__atomic_load_n(&smp_remote_wake_helper_done, __ATOMIC_ACQUIRE) == 0U ||
        __atomic_load_n(&smp_remote_wake_ok, __ATOMIC_ACQUIRE) == 0U)
    {
        serial_write("VANTAOS_SMP_REMOTE_WAKE_FAIL\n");
        return 0;
    }

    serial_write("VANTAOS_SMP_REMOTE_WAKE_OK\n");
    return 1;
}


static uint32_t smp_migration_bsp_cpu;
static uint32_t smp_migration_target_cpu;
static volatile uint32_t smp_migration_target_pid;
static volatile uint32_t smp_migration_start_cpu;
static volatile uint32_t smp_migration_target_done;
static volatile uint32_t smp_migration_helper_done;
static volatile uint32_t smp_migration_ok;
static volatile uint32_t smp_migration_cpu_mask;
static volatile uint32_t smp_migration_owner_mismatch;

static void smp_migration_target_entry(void)
{
    uint32_t pid = process_current_pid();
    uint32_t start_cpu = process_current_cpu_id();
    __atomic_store_n(&smp_migration_start_cpu, start_cpu, __ATOMIC_RELEASE);
    __atomic_store_n(&smp_migration_target_pid, pid, __ATOMIC_RELEASE);

    for (uint32_t spin = 0; spin < 100000000U; spin++)
    {
        process_t snapshot;
        uint32_t current_cpu = process_current_cpu_id();
        if (current_cpu < 32U)
            __atomic_fetch_or(&smp_migration_cpu_mask,
                              1U << current_cpu, __ATOMIC_RELEASE);

        if (process_snapshot(pid, &snapshot) &&
            snapshot.state != PROCESS_UNUSED &&
            snapshot.owner_cpu != current_cpu)
            __atomic_store_n(&smp_migration_owner_mismatch, 1U,
                             __ATOMIC_RELEASE);

        if (current_cpu == smp_migration_bsp_cpu &&
            current_cpu != start_cpu &&
            process_snapshot(pid, &snapshot) &&
            snapshot.affinity_cpu == smp_migration_bsp_cpu &&
            snapshot.owner_cpu == current_cpu)
        {
            __atomic_store_n(&smp_migration_target_done, 1U,
                             __ATOMIC_RELEASE);
            return;
        }
        __asm__ volatile ("pause" ::: "memory");
    }
}

static void smp_migration_helper_entry(void)
{
    uint32_t target_pid = 0;
    uint32_t start_cpu = UINT32_MAX;
    for (uint32_t spin = 0; spin < 100000000U; spin++)
    {
        target_pid = __atomic_load_n(&smp_migration_target_pid,
                                     __ATOMIC_ACQUIRE);
        start_cpu = __atomic_load_n(&smp_migration_start_cpu,
                                    __ATOMIC_ACQUIRE);
        if (target_pid != 0U && start_cpu != UINT32_MAX)
            break;
        __asm__ volatile ("pause" ::: "memory");
    }

    int ok = target_pid != 0U &&
             start_cpu == smp_migration_target_cpu &&
             start_cpu != smp_migration_bsp_cpu &&
             process_set_affinity(target_pid, smp_migration_bsp_cpu);

    for (uint32_t spin = 0; ok && spin < 100000000U; spin++)
    {
        if (__atomic_load_n(&smp_migration_target_done,
                            __ATOMIC_ACQUIRE) != 0U)
            break;
        __asm__ volatile ("pause" ::: "memory");
    }

    ok = ok && __atomic_load_n(&smp_migration_target_done,
                               __ATOMIC_ACQUIRE) != 0U;
    __atomic_store_n(&smp_migration_ok, ok ? 1U : 0U, __ATOMIC_RELEASE);
    __atomic_store_n(&smp_migration_helper_done, 1U, __ATOMIC_RELEASE);
}

static int smp_process_migration_probe_initialize(void)
{
    smp_migration_bsp_cpu = process_current_cpu_id();
    smp_migration_target_cpu = UINT32_MAX;
    smp_migration_target_pid = 0;
    smp_migration_start_cpu = UINT32_MAX;
    smp_migration_target_done = 0;
    smp_migration_helper_done = 0;
    smp_migration_ok = 0;
    smp_migration_cpu_mask = 0;
    smp_migration_owner_mismatch = 0;

    for (uint32_t cpu = 0; cpu < smp_cpu_count(); cpu++)
    {
        if (!smp_cpu_is_bsp(cpu) && cpu_local_get(cpu))
        {
            smp_migration_target_cpu = cpu;
            break;
        }
    }

    if (smp_migration_target_cpu == UINT32_MAX ||
        process_create_kernel_on_cpu("migration-target",
                                     process_current_pid(),
                                     smp_migration_target_entry,
                                     smp_migration_target_cpu) < 0 ||
        process_create_kernel("migration-helper", process_current_pid(),
                              smp_migration_helper_entry) < 0)
    {
        serial_write("VANTAOS_SMP_MIGRATION_FAIL\n");
        return 0;
    }
    return 1;
}

static int smp_process_migration_probe_wait(void)
{
    uint32_t attempts = 0;
    while (attempts++ < 1500U &&
           __atomic_load_n(&smp_migration_helper_done,
                           __ATOMIC_ACQUIRE) == 0U)
        __asm__ volatile ("sti; hlt" ::: "memory");

    if (__atomic_load_n(&smp_migration_helper_done, __ATOMIC_ACQUIRE) == 0U)
    {
        serial_write("VANTAOS_SMP_MIGRATION_FAIL\n");
        return 0;
    }

    uint32_t cpu_mask = __atomic_load_n(&smp_migration_cpu_mask,
                                        __ATOMIC_ACQUIRE);
    uint32_t required_mask = (1U << smp_migration_bsp_cpu) |
                             (1U << smp_migration_target_cpu);
    if (__atomic_load_n(&smp_migration_owner_mismatch,
                        __ATOMIC_ACQUIRE) != 0U ||
        (cpu_mask & required_mask) != required_mask)
    {
        serial_write("VANTAOS_SMP_MIGRATION_OWNERSHIP_FAIL\n");
        return 0;
    }

    if (__atomic_load_n(&smp_migration_ok, __ATOMIC_ACQUIRE) == 0U)
    {
        serial_write("VANTAOS_SMP_MIGRATION_FAIL\n");
        return 0;
    }

    serial_write("VANTAOS_SMP_MIGRATION_OK\n");
    return 1;
}

/* Keep boot orchestration out of the test implementation. */
void os_selftest_run_smp_probes(void)
{
    if (smp_cpu_count() > 1U && smp_scheduler_probe_initialize())
        (void)smp_scheduler_probe_wait();
    if (smp_cpu_count() > 1U && smp_remote_wake_probe_initialize())
        (void)smp_remote_wake_probe_wait();
    if (smp_cpu_count() > 1U && smp_process_migration_probe_initialize())
        (void)smp_process_migration_probe_wait();
}


