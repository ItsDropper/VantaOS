#include "panic.h"
#include <stdint.h>

#include "graphics.h"
#include "interrupts.h"
#include "terminal.h"
#include "process.h"
#include "fault_trace.h"

static const char* exception_names[] =
{
    "Divide Error","Debug","Non-Maskable Interrupt","Breakpoint",
    "Overflow","BOUND Range Exceeded","Invalid Opcode","Device Not Available",
    "Double Fault","Coprocessor Segment Overrun","Invalid TSS",
    "Segment Not Present","Stack-Segment Fault","General Protection Fault",
    "Page Fault","Reserved","x87 Floating-Point Exception","Alignment Check",
    "Machine Check","SIMD Floating-Point Exception","Virtualization Exception",
    "Control Protection Exception","Reserved","Reserved","Reserved","Reserved",
    "Reserved","Hypervisor Injection Exception","VMM Communication Exception",
    "Security Exception","Reserved","Unknown"
};

static const char* panic_exception_name(unsigned int number)
{
    return number < 31 ? exception_names[number] : exception_names[31];
}

static __attribute__((noreturn)) void panic_halt(void)
{
    __asm__ volatile ("cli");
    for (;;)
        __asm__ volatile ("hlt");
}

void kernel_panic(
    const char* reason,
    unsigned int exception_number,
    struct exception_frame* frame,
    uint64_t fault_address,
    int has_fault_address
)
{
    __asm__ volatile ("cli");

    if (!graphics_is_initialized())
    {
        terminal_reset();
        terminal_write("\nVANTAOS KERNEL PANIC\n\n");
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
            terminal_write("\nRIP: ");
            terminal_write_hex(frame->rip);
            terminal_write("\nCS: ");
            terminal_write_hex(frame->cs);
            terminal_write("\nEFLAGS: ");
            terminal_write_hex(frame->eflags);
            terminal_write("\n");
        }

        terminal_write("\nSYSTEM HALTED\n");
        panic_halt();
    }

    int width = (int)graphics_get_width();
    int height = (int)graphics_get_height();

    graphics_clear(0x00000000);

    int x = width >= 900 ? 64 : 28;
    int y = height >= 600 ? 42 : 24;

    graphics_draw_text(
        x, y,
        "VANTAOS KERNEL PANIC",
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
        "SYSTEM HALTED",
        0x00F2F5F8, 1
    );

    graphics_draw_text(
        x,
        height - 38,
        "The system cannot safely continue. Restart VantaOS.",
        0x008E9AA5, 1
    );

    panic_halt();
}

void exception_handler(
    unsigned int exception_number,
    struct exception_frame* frame
)
{
    uint64_t fault_address = 0;
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
            exception_names[exception_number] :
            "Unknown CPU exception",
        exception_number,
        frame,
        fault_address,
        has_fault_address
    );

    __builtin_unreachable();
}
