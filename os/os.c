/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "os.h"

#include "desktop.h"
#include "desktop_process.h"
#include "filesystem.h"
#include "graphics.h"
#include "input.h"
#include "keyboard.h"
#include "mouse.h"
#include "process.h"
#include "smp.h"
#include "panic.h"
#include "serial.h"
#include "scheduler.h"
#include "shell.h"
#include "terminal.h"
#include "terminal_process.h"
#include "usb.h"
#include "vfs.h"

extern void shell_set_multiboot_info(multiboot_info_t* mbd);
extern void input_register_ps2_devices(void);

static unsigned long long boot_terminal;
static unsigned long long boot_keyboard;
static unsigned long long boot_mouse;
static unsigned long long boot_shell;

#ifdef VANTAOS_SELF_TEST
#include "selftest/selftest.h"
#include "user_mode_test.h"
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

static _Noreturn void os_boot_fatal(const char* marker)
{
    serial_write(marker);
    kernel_panic_halt_all();
}

void os_initialize(multiboot_info_t* mbd)
{
    terminal_initialize();
    boot_terminal = read_tsc();

    input_initialize();
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
    (void)serial_boot_log_flush();
    vfs_initialize();

    process_initialize();
    if (!process_is_initialized())
        os_boot_fatal("VANTAOS_PROCESS_INIT_FATAL_FAIL\n");

    /*
     * Register the boot/desktop execution context as PID 1 before any
     * application processes are created. Its stack is the real kernel
     * boot stack, and the first timer IRQ will replace its saved ESP
     * with a genuine long-mode interrupt frame.
     */
    if (process_attach_current("desktop", 0) <= 0)
        os_boot_fatal("VANTAOS_PROCESS_BOOT_ATTACH_FATAL_FAIL\n");

    scheduler_initialize();
    if (!smp_scheduler_initialize())
        os_boot_fatal("VANTAOS_SMP_SCHEDULER_INIT_FATAL_FAIL\n");
    terminal_process_initialize();
    desktop_process_initialize();

    mouse_initialize();
    input_register_ps2_devices();
    usb_initialize();
    boot_mouse = read_tsc();

    shell_initialize();
    shell_set_multiboot_info(mbd);
    boot_shell = read_tsc();

    desktop_initialize(mbd);
    (void)serial_boot_log_flush();

#ifdef VANTAOS_SELF_TEST
    os_selftest_initialize();
    if (!user_mode_process_smoke_test_start())
        os_boot_fatal("VANTAOS_USER_PROCESS_TEST_START_FATAL_FAIL\n");
    shell_exec("/HELLO.ELF alpha \"two words\"");
    shell_exec("/EXPLORER-E2E.ELF");
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
    /* Test implementation lives outside the boot/run orchestration. */
    os_selftest_run_smp_probes();
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
