/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "panic.h"
#include "panic_format.h"
#include "cpu_local.h"

#include "graphics.h"
#include "interrupts.h"
#include "terminal.h"
#include "process.h"
#include "paging.h"
#include "fault_trace.h"
#include "apic.h"
#include "serial.h"
#include "selftest/user_mode_selftest.h"

static volatile uint32_t panic_broadcast_active;

static int panic_broadcast_begin(void)
{
    return __atomic_exchange_n(&panic_broadcast_active, 1U,
                               __ATOMIC_ACQ_REL) == 0U;
}

/*
 * Stop peers only after the local panic report is complete. Freezing a peer
 * while it owns a console/serial/graphics lock could deadlock panic reporting.
 */
static void panic_broadcast_stop_peers(void)
{
    /*
     * Do not log after broadcasting: a peer may have been stopped while
     * holding a serial lock, and logging here could deadlock the panic CPU.
     */
    if (apic_is_enabled())
        (void)apic_send_nmi_all();
}

static const char* panic_exception_name(unsigned int number)
{
    return panic_format_exception_name(number);
}

static __attribute__((noreturn)) void panic_halt(void)
{
    __asm__ volatile ("cli");
    for (;;)
        __asm__ volatile ("hlt");
}

_Noreturn void kernel_panic_halt_all(void)
{
    __asm__ volatile ("cli" ::: "memory");
    if (panic_broadcast_begin())
    {
        serial_write("VANTAOS_SMP_FATAL_HALT\n");
        panic_broadcast_stop_peers();
    }
    panic_halt();
}

void kernel_panic(
    const char* reason,
    unsigned int exception_number,
    struct exception_frame* frame,
    uintptr_t fault_address,
    int has_fault_address
)
{
    __asm__ volatile ("cli");
    if (!panic_broadcast_begin())
        panic_halt();

    if (!graphics_is_initialized())
    {
        terminal_reset();
        terminal_write("\nVANTAOS ENCOUNTERED A FATAL ERROR\n\n");
        terminal_write("The kernel stopped because a fatal error was detected.\n");
        terminal_write("Reason: ");
        terminal_write(reason ? reason : "Unknown kernel failure");
        terminal_write("\n");

        if (exception_number != 0xFFFFFFFFU)
        {
            terminal_write("Exception: ");
            terminal_write(panic_exception_name(exception_number));
            terminal_write("\n");
        }

        terminal_write("Current PID: ");
        terminal_write_hex(process_current_pid());
        terminal_write("\n");

        if (frame)
        {
            terminal_write("Error Code: ");
            terminal_write_hex(frame->error_code);
            terminal_write("\nEIP: ");
            terminal_write_hex(frame->eip);
            terminal_write("\nCS: ");
            terminal_write_hex(frame->cs);
            terminal_write("\nEFLAGS: ");
            terminal_write_hex(frame->eflags);
            terminal_write("\n");
        }

        terminal_write("\nSYSTEM HALTED\n");
        panic_broadcast_stop_peers();
        panic_halt();
    }

    int width = (int)graphics_get_width();
    int height = (int)graphics_get_height();

    if (!graphics_panic_prepare())\n    {\n        panic_broadcast_stop_peers();\n        panic_halt();\n    }\n\n    graphics_clear(0x008A1018);

    int x = width >= 900 ? 64 : 28;
    int y = height >= 600 ? 42 : 24;

    graphics_draw_text(
        x, y,
        "VANTAOS ENCOUNTERED A FATAL ERROR",
        0x00F2F5F8, 2
    );

    graphics_draw_text(
        x, y + 28,
        "The kernel stopped because a fatal error was detected.",
        0x00C7CFD7, 1
    );

    graphics_fill_rect(
        x, y + 48,
        width - x * 2,
        1,
        0x00384048
    );

    graphics_draw_text(
        x, y + 68,
        "WHAT HAPPENED",
        0x00F2F5F8, 1
    );

    graphics_draw_text(
        x, y + 84,
        reason ? reason : "Unknown kernel failure",
        0x00C7CFD7, 1
    );

    graphics_draw_text(x, y + 100, "CPU / APIC ID", 0x00FFB4A2, 1);
    const cpu_local_t* panic_cpu = cpu_local_current();
    if (panic_cpu && panic_cpu->self == panic_cpu)
    {
        char cpu_text[19];
        panic_format_hex64(cpu_text, panic_cpu->apic_id);
        graphics_draw_text(x + 104, y + 100, cpu_text, 0x00FFFFFF, 1);
    }
    else
        graphics_draw_text(x + 104, y + 100, "unavailable", 0x00FFFFFF, 1);

    fault_trace_draw(
        x,
        y + 120,
        width - x * 2,
        exception_number,
        frame,
        fault_address,
        has_fault_address
    );

    graphics_draw_text(
        x,
        height - 54,
        "THE SYSTEM CANNOT SAFELY CONTINUE",
        0x00F2F5F8, 1
    );

    graphics_draw_text(
        x,
        height - 38,
        "Restart VantaOS. Diagnostic output is also available on serial.",
        0x008E9AA5, 1
    );

    panic_broadcast_stop_peers();
    panic_halt();
}

void exception_handler(
    unsigned int exception_number,
    struct exception_frame* frame
)
{
    if (user_mode_selftest_handle_exception(exception_number, frame))
        return;

    /*
     * A fault in CPL3 belongs to the process, not the kernel. The assembly
     * return path detects the user CS, schedules away, and reclaims this
     * address space only after switching CR3 and RSP.
     */
    if (frame && (frame->cs & 3U) == 3U)
    {
        process_t process;
        if (process_snapshot(process_current_pid(), &process) &&
            process.is_user &&
            process.address_space == paging_get_current_address_space())
        {
            serial_write("VANTAOS_USER_PROCESS_FAULT\n");
            process_terminate_current();
            return;
        }
    }

    if (exception_number == 2U &&
        __atomic_load_n(&panic_broadcast_active, __ATOMIC_ACQUIRE) != 0U)
        panic_halt();

    uintptr_t fault_address = 0;
    int has_fault_address = 0;

    if (exception_number == 14)
    {
        __asm__ volatile (
            "mov %%cr2, %0"
            : "=r"(fault_address)
        );
        has_fault_address = 1;
    }

    kernel_panic(
        exception_number < 31 ?
            panic_format_exception_name(exception_number) :
            "Unknown CPU exception",
        exception_number,
        frame,
        fault_address,
        has_fault_address
    );

    __builtin_unreachable();
}
