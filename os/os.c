#include "os.h"

#include "desktop.h"
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
#include "pmm.h"
#include "serial.h"
#include "scheduler.h"
#include "shell.h"
#include "terminal.h"
#include "terminal_process.h"
#include "vfs.h"

extern void shell_set_multiboot_info(multiboot_info_t* mbd);

static unsigned long long boot_terminal;
static unsigned long long boot_keyboard;
static unsigned long long boot_mouse;
static unsigned long long boot_shell;

#ifdef VANTAOS_SELF_TEST
static uint32_t scheduler_probe_pid;

static void scheduler_probe_entry(void)
{
    serial_write("VANTAOS_SCHEDULER_PROBE_OK\n");
}

static void scheduler_reap_observer_entry(void)
{
    for (uint32_t attempt = 0; attempt < 1000U; attempt++)
    {
        if (process_get(scheduler_probe_pid) == NULL)
        {
            serial_write("VANTAOS_SCHEDULER_REAP_OK\n");
            return;
        }

        __asm__ volatile ("hlt");
    }

    serial_write("VANTAOS_SCHEDULER_REAP_FAIL\n");
}

static void filesystem_probe_initialize(void)
{
    int root_id = vfs_lookup("/");
    const fs_node_t* root = root_id >= 0 ?
        vfs_get_node((uint32_t)root_id) : NULL;

    /*
     * When the CI disk is mounted, prove that case variants resolve to
     * the same on-disk nodes and that the FAT32 entries remain reachable.
     * This catches duplicate virtual directories shadowing real directories.
     */
    int fat32_namespace_ok = 1;
    if (fat32_is_mounted())
    {
        int system_lower = filesystem_lookup("/system");
        int system_upper = filesystem_lookup("/SYSTEM");
        int drivers_lower = filesystem_lookup("/system/drivers");
        int drivers_upper = filesystem_lookup("/SYSTEM/DRIVERS");
        int ata_lower = filesystem_lookup("/system/drivers/ata.sys");
        int ata_upper = filesystem_lookup("/SYSTEM/DRIVERS/ATA.SYS");
        int keyboard_lfn = filesystem_lookup("/system/devices/keyboard0.dev");
        int config_lfn = filesystem_lookup("/etc/system.conf");
        const fs_node_t* keyboard_node = keyboard_lfn >= 0 ?
            filesystem_get_node((uint32_t)keyboard_lfn) : NULL;
        const fs_node_t* config_node = config_lfn >= 0 ?
            filesystem_get_node((uint32_t)config_lfn) : NULL;

        fat32_namespace_ok =
            system_lower >= 0 &&
            system_lower == system_upper &&
            drivers_lower >= 0 &&
            drivers_lower == drivers_upper &&
            ata_lower >= 0 &&
            ata_lower == ata_upper &&
            keyboard_node &&
            keyboard_node->size > 0 &&
            config_node &&
            config_node->size > 0;
    }

    if (!filesystem_is_initialized() ||
        !vfs_is_initialized() ||
        !root ||
        root->type != FS_NODE_DIRECTORY ||
        vfs_lookup("/system") < 0 ||
        vfs_lookup("/system/drivers") < 0 ||
        vfs_lookup("/home/user") < 0 ||
        vfs_lookup("/etc") < 0 ||
        vfs_lookup("/__vantaos_ci_missing_entry__") != -1 ||
        vfs_list((uint32_t)root_id, NULL, 0) < 0 ||
        !fat32_namespace_ok)
    {
        serial_write("VANTAOS_FILESYSTEM_NAMESPACE_FAIL\n");
        return;
    }

    char byte = 'x';
    if (vfs_read(-1, &byte, 1) != -1 ||
        vfs_write(-1, "x", 1) != -1 ||
        vfs_close(-1) != -1)
    {
        serial_write("VANTAOS_VFS_HANDLE_GUARDS_FAIL\n");
        return;
    }

    serial_write("VANTAOS_FILESYSTEM_NAMESPACE_OK\n");
    serial_write("VANTAOS_VFS_HANDLE_GUARDS_OK\n");
}

static void process_state_transition_probe(void)
{
    int pid = process_create_kernel(
        "state-transition-probe",
        process_current_pid(),
        scheduler_probe_entry
    );

    if (pid < 0)
    {
        serial_write("VANTAOS_PROCESS_STATE_TRANSITIONS_FAIL\n");
        return;
    }

    int ok = 1;
    process_t process;
    ok = process_snapshot((uint32_t)pid, &process) &&
         process.state == PROCESS_READY;

    if (ok)
        ok = process_mark_running((uint32_t)pid);
    if (ok)
        ok = process_snapshot((uint32_t)pid, &process) &&
             process.state == PROCESS_RUNNING;

    if (ok)
        ok = process_mark_ready((uint32_t)pid);
    if (ok)
        ok = process_snapshot((uint32_t)pid, &process) &&
             process.state == PROCESS_READY;

    if (ok)
        ok = process_set_running((uint32_t)pid);
    if (ok)
        ok = process_mark_ready((uint32_t)pid);
    if (ok)
    {
        ok = process_snapshot((uint32_t)pid, &process) &&
             process.state == PROCESS_READY &&
             process.owner_cpu != PROCESS_CPU_UNOWNED;
        if (ok)
            ok = !process_claim_for_cpu(
                (uint32_t)pid, 0U, process.owner_cpu + 1U);
    }
    if (ok)
        ok = process_set_running((uint32_t)pid);
    if (ok)
    {
        process_block_current();
        ok = process_snapshot((uint32_t)pid, &process) &&
             process.state == PROCESS_SLEEPING &&
             process.owner_cpu != PROCESS_CPU_UNOWNED;
    }

    if (ok)
        ok = process_wake((uint32_t)pid);
    if (ok)
        ok = process_snapshot((uint32_t)pid, &process) &&
             process.state == PROCESS_READY;

    /* Restore the boot/desktop process before reaping the probe. */
    if (process_current_pid() != 1U)
        ok = process_switch_to(1U) && ok;

    if (process_snapshot((uint32_t)pid, &process))
        ok = process_terminate((uint32_t)pid) && ok;

    if (!ok || process_snapshot((uint32_t)pid, &process))
    {
        serial_write("VANTAOS_PROCESS_STATE_TRANSITIONS_FAIL\n");
        return;
    }

    serial_write("VANTAOS_PROCESS_STATE_TRANSITIONS_OK\n");
}


static void pmm_allocator_probe(void)
{
    if (!pmm_is_initialized())
    {
        serial_write("VANTAOS_PMM_ALLOCATOR_FAIL\n");
        return;
    }

    uint32_t total_before = pmm_get_total_blocks();
    uint32_t free_before = pmm_get_free_blocks();
    uint32_t used_before = pmm_get_used_blocks();

    if (total_before == 0 ||
        free_before > total_before ||
        used_before > total_before ||
        free_before + used_before != total_before)
    {
        serial_write("VANTAOS_PMM_ALLOCATOR_FAIL\n");
        return;
    }

    void* first = pmm_alloc_block();
    void* second = pmm_alloc_block();

    if (!first || !second)
    {
        if (first)
            pmm_free_block(first);
        if (second)
            pmm_free_block(second);
        serial_write("VANTAOS_PMM_ALLOCATOR_FAIL\n");
        return;
    }

    uintptr_t first_address = (uintptr_t)first;
    uintptr_t second_address = (uintptr_t)second;
    int ok =
        first_address != second_address &&
        (first_address % PAGE_SIZE) == 0 &&
        (second_address % PAGE_SIZE) == 0 &&
        pmm_get_free_blocks() + 2U == free_before &&
        pmm_get_used_blocks() == used_before + 2U;

    pmm_free_block((void*)(first_address + 1U));
    ok = (pmm_get_free_blocks() + 2U == free_before) && ok;

    pmm_free_block(first);
    pmm_free_block(second);
    ok = (pmm_get_free_blocks() == free_before) && ok;
    ok = (pmm_get_used_blocks() == used_before) && ok;

    /* A second free must not release the same frame twice. */
    pmm_free_block(first);
    ok = (pmm_get_free_blocks() == free_before) && ok;
    ok = (pmm_get_total_blocks() == total_before) && ok;

    serial_write(ok ?
        "VANTAOS_PMM_ALLOCATOR_OK\n" :
        "VANTAOS_PMM_ALLOCATOR_FAIL\n");
}

static void heap_allocator_probe(void)
{
    if (!heap_is_initialized())
    {
        serial_write("VANTAOS_HEAP_ALLOCATOR_FAIL\n");
        return;
    }

    size_t total_before = heap_get_total_size();
    size_t used_before = heap_get_used_size();
    size_t free_before = heap_get_free_size();

    if (total_before < 16U * 4096U ||
        used_before > total_before ||
        free_before > total_before)
    {
        serial_write("VANTAOS_HEAP_ALLOCATOR_FAIL\n");
        return;
    }

    unsigned char* first = (unsigned char*)kmalloc(64U);
    unsigned char* second = (unsigned char*)kmalloc(257U);

    if (!first || !second)
    {
        if (first)
            kfree(first);
        if (second)
            kfree(second);
        serial_write("VANTAOS_HEAP_ALLOCATOR_FAIL\n");
        return;
    }

    int ok =
        ((uintptr_t)first % HEAP_ALIGNMENT) == 0 &&
        ((uintptr_t)second % HEAP_ALIGNMENT) == 0;

    for (size_t i = 0; i < 64U; i++)
        first[i] = (unsigned char)(i ^ 0x5AU);

    for (size_t i = 0; i < 257U; i++)
        second[i] = (unsigned char)(i ^ 0xA5U);

    for (size_t i = 0; i < 64U; i++)
        ok = (first[i] == (unsigned char)(i ^ 0x5AU)) && ok;

    for (size_t i = 0; i < 257U; i++)
        ok = (second[i] == (unsigned char)(i ^ 0xA5U)) && ok;

    size_t used_after_alloc = heap_get_used_size();
    kfree((void*)((uintptr_t)first + 1U));
    ok = (heap_get_used_size() == used_after_alloc) && ok;

    kfree(first);
    kfree(second);
    size_t used_after_free = heap_get_used_size();
    kfree(first); /* A double free must not change accounting. */

    ok = (used_after_free == used_before) && ok;
    ok = (heap_get_used_size() == used_before) && ok;
    ok = (heap_get_total_size() == total_before) && ok;
    ok = (heap_get_free_size() >= free_before) && ok;

    serial_write(ok ?
        "VANTAOS_HEAP_ALLOCATOR_OK\n" :
        "VANTAOS_HEAP_ALLOCATOR_FAIL\n");
}


static uint32_t smp_scheduler_probe_task_count;
static uint32_t smp_scheduler_probe_ap_cpu;
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

    for (uint32_t i = 0; i < 5000000U; i++)
        __asm__ volatile ("pause" ::: "memory");

    __atomic_sub_fetch(&smp_scheduler_probe_active, 1U, __ATOMIC_ACQ_REL);
    __asm__ volatile ("sti" ::: "memory");
    __atomic_add_fetch(&smp_scheduler_probe_completed, 1U, __ATOMIC_RELEASE);
}

static int smp_scheduler_probe_initialize(void)
{
    smp_scheduler_probe_completed = 0;
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
    return 1;
}

static int smp_scheduler_probe_wait(void)
{
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
    if (completed != smp_scheduler_probe_task_count ||
        cpus_seen < required_cpus || duplicates != 0U ||
        max_active < required_cpus)
    {
        serial_write("VANTAOS_SMP_SCHEDULER_STRESS_FAIL\n");
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
        if (current_cpu == smp_migration_bsp_cpu &&
            current_cpu != start_cpu &&
            process_snapshot(pid, &snapshot) &&
            snapshot.affinity_cpu == smp_migration_bsp_cpu)
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

    if (__atomic_load_n(&smp_migration_helper_done, __ATOMIC_ACQUIRE) == 0U ||
        __atomic_load_n(&smp_migration_ok, __ATOMIC_ACQUIRE) == 0U)
    {
        serial_write("VANTAOS_SMP_MIGRATION_FAIL\n");
        return 0;
    }

    serial_write("VANTAOS_SMP_MIGRATION_OK\n");
    return 1;
}

static void scheduler_probe_initialize(void)
{
    pmm_allocator_probe();
    heap_allocator_probe();
    filesystem_probe_initialize();
    process_state_transition_probe();

    if (process_create_kernel(NULL, process_current_pid(),
                              scheduler_probe_entry) != -1 ||
        process_create_kernel("invalid-entry", process_current_pid(),
                              NULL) != -1 ||
        process_switch_to(0) != 0 ||
        process_switch_to(PROCESS_MAX) != 0 ||
        process_set_running(PROCESS_MAX) != 0 ||
        process_mark_ready(0) != 0 ||
        process_terminate(0) != 0 ||
        process_wake(PROCESS_MAX) != 0)
    {
        serial_write("VANTAOS_PROCESS_API_GUARDS_FAIL\n");
        return;
    }

    serial_write("VANTAOS_PROCESS_API_GUARDS_OK\n");

    int probe_pid = process_create_kernel(
        "scheduler-probe-name-that-exceeds-the-process-name-limit",
        process_current_pid(),
        scheduler_probe_entry
    );

    if (probe_pid < 0)
    {
        serial_write("VANTAOS_SCHEDULER_PROBE_CREATE_FAIL\n");
        return;
    }

    scheduler_probe_pid = (uint32_t)probe_pid;
    const process_t* probe = process_get(scheduler_probe_pid);
    if (!probe || probe->name[PROCESS_NAME_MAX] != '\0')
    {
        serial_write("VANTAOS_PROCESS_NAME_FAIL\n");
        return;
    }

    serial_write("VANTAOS_PROCESS_NAME_OK\n");

    if (process_create_kernel(
            "sched-reap-check",
            process_current_pid(),
            scheduler_reap_observer_entry
        ) < 0)
    {
        serial_write("VANTAOS_SCHEDULER_OBSERVER_CREATE_FAIL\n");
    }
}
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

void os_initialize(multiboot_info_t* mbd)
{
    terminal_initialize();
    boot_terminal = read_tsc();

    keyboard_initialize();
    boot_keyboard = read_tsc();

    int graphics_ready =
        graphics_initialize(mbd);

    terminal_write("Graphics diagnostics:\n");
    terminal_write("  Multiboot flags: ");
    terminal_write_hex(mbd ? mbd->flags : 0);
    terminal_write("\n");

    terminal_write("  Framebuffer: ");
    terminal_write_hex(
        mbd ? (uint32_t)mbd->framebuffer_addr : 0
    );
    terminal_write("\n");

    terminal_write("  Type: ");
    terminal_write_hex(
        mbd ? mbd->framebuffer_type : 0
    );
    terminal_write("\n");

    terminal_write("  BPP: ");
    terminal_write_hex(
        mbd ? mbd->framebuffer_bpp : 0
    );
    terminal_write("\n");

    terminal_write("  Graphics init: ");
    terminal_write(
        graphics_ready ? "YES\n" : "NO\n"
    );

    filesystem_initialize(mbd);
    vfs_initialize();

    process_initialize();

    /*
     * Register the boot/desktop execution context as PID 1 before any
     * application processes are created. Its stack is the real kernel
     * boot stack, and the first timer IRQ will replace its saved ESP
     * with a genuine long-mode interrupt frame.
     */
    process_attach_current("desktop", 0);

    scheduler_initialize();
    (void)smp_scheduler_initialize();
    terminal_process_initialize();
    desktop_process_initialize();

    mouse_initialize();
    boot_mouse = read_tsc();

    shell_initialize();
    shell_set_multiboot_info(mbd);
    boot_shell = read_tsc();

    desktop_initialize(mbd);

#ifdef VANTAOS_SELF_TEST
    scheduler_probe_initialize();
#endif
}

void os_run(void)
{
    terminal_write("\nKernel initialized successfully.\n");

    terminal_write(
        filesystem_is_initialized() ?
        "Filesystem: online.\n" :
        "Filesystem: offline.\n"
    );

    terminal_write(
        process_is_initialized() ?
        "Process manager: online.\n" :
        "Process manager: offline.\n"
    );

    terminal_write(
        "Paging: enabled (identity-mapped 16 MiB).\n"
    );

    terminal_write(
        "Type 'help' in the Terminal app.\n\n"
    );

    desktop_update();
    desktop_present();

    serial_write("VANTAOS_BOOT_OK\n");
    /* Publish boot completion before enabling timer-driven preemption. */
    __asm__ volatile ("sti");

#ifdef VANTAOS_SELF_TEST
    /* The cross-CPU workload is meaningful only when an AP exists. */
    if (smp_cpu_count() > 1U && smp_scheduler_probe_initialize())
        (void)smp_scheduler_probe_wait();
    if (smp_cpu_count() > 1U && smp_remote_wake_probe_initialize())
        (void)smp_remote_wake_probe_wait();
    if (smp_cpu_count() > 1U && smp_process_migration_probe_initialize())
        (void)smp_process_migration_probe_wait();
#endif
    desktop_process_main();
}

unsigned long long kernel_boot_terminal(void)
{
    return boot_terminal;
}

unsigned long long kernel_boot_keyboard(void)
{
    return boot_keyboard;
}

unsigned long long kernel_boot_mouse(void)
{
    return boot_mouse;
}

unsigned long long kernel_boot_shell(void)
{
    return boot_shell;
}
