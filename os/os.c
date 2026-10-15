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

        fat32_namespace_ok =
            system_lower >= 0 &&
            system_lower == system_upper &&
            drivers_lower >= 0 &&
            drivers_lower == drivers_upper &&
            ata_lower >= 0 &&
            ata_lower == ata_upper;
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
    const process_t* process = process_get((uint32_t)pid);
    ok = process && process->state == PROCESS_READY;

    if (ok)
        ok = process_mark_running((uint32_t)pid);
    process = process_get((uint32_t)pid);
    if (ok)
        ok = process && process->state == PROCESS_RUNNING;

    if (ok)
        ok = process_mark_ready((uint32_t)pid);
    process = process_get((uint32_t)pid);
    if (ok)
        ok = process && process->state == PROCESS_READY;

    if (ok)
        ok = process_set_running((uint32_t)pid);
    if (ok)
    {
        process_block_current();
        process = process_get((uint32_t)pid);
        ok = process && process->state == PROCESS_SLEEPING;
    }

    if (ok)
        ok = process_wake((uint32_t)pid);
    process = process_get((uint32_t)pid);
    if (ok)
        ok = process && process->state == PROCESS_READY;

    /* Restore the boot/desktop process before reaping the probe. */
    if (process_current_pid() != 1U)
        ok = process_switch_to(1U) && ok;

    if (process_get((uint32_t)pid))
        ok = process_terminate((uint32_t)pid) && ok;

    if (!ok || process_get((uint32_t)pid) != NULL)
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

    __asm__ volatile ("sti");

    serial_write("VANTAOS_BOOT_OK\n");
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
