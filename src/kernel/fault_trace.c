#include "fault_trace.h"

#include "graphics.h"
#include "process.h"

extern unsigned char _start;
extern unsigned char _end;

static void draw_hex(int x, int y, unsigned int value)
{
    const char* hex = "0123456789ABCDEF";
    char text[11];

    text[0] = '0';
    text[1] = 'x';

    for (int i = 0; i < 8; i++)
        text[2 + i] =
            hex[(value >> (28 - i * 4)) & 0x0F];

    text[10] = 0;

    graphics_draw_text(
        x, y, text,
        0x00F2F5F8, 1
    );
}

static void draw_label(
    int x,
    int y,
    const char* label
)
{
    graphics_draw_text(
        x, y, label,
        0x008E9AA5, 1
    );
}

static void draw_field(
    int x,
    int y,
    const char* label,
    unsigned int value
)
{
    draw_label(x, y, label);
    draw_hex(x + 132, y, value);
}

static const char* exception_name(
    unsigned int number
)
{
    static const char* names[] =
    {
        "Divide Error",
        "Debug",
        "Non-Maskable Interrupt",
        "Breakpoint",
        "Overflow",
        "BOUND Range Exceeded",
        "Invalid Opcode",
        "Device Not Available",
        "Double Fault",
        "Coprocessor Segment Overrun",
        "Invalid TSS",
        "Segment Not Present",
        "Stack-Segment Fault",
        "General Protection Fault",
        "Page Fault",
        "Reserved",
        "x87 Floating-Point Exception",
        "Alignment Check",
        "Machine Check",
        "SIMD Floating-Point Exception",
        "Virtualization Exception",
        "Control Protection Exception",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
        "Hypervisor Injection",
        "VMM Communication",
        "Security Exception",
        "Reserved",
        "Unknown"
    };

    return number < 31 ?
        names[number] :
        names[31];
}

static const char* page_fault_cause(
    unsigned int error_code
)
{
    if (error_code & 0x01U)
        return (error_code & 0x02U) ?
            "Write to a present page" :
            "Read from a present page";

    return (error_code & 0x02U) ?
        "Write to a non-present page" :
        "Read from a non-present page";
}

static const char* page_fault_privilege(
    unsigned int error_code
)
{
    return (error_code & 0x04U) ?
        "User-mode access" :
        "Kernel-mode access";
}

static void draw_page_fault_details(
    int x,
    int y,
    unsigned int error_code
)
{
    graphics_draw_text(
        x, y,
        "PAGE FAULT DECODE",
        0x00F2F5F8, 1
    );

    graphics_draw_text(
        x, y + 18,
        page_fault_cause(error_code),
        0x00C7CFD7, 1
    );

    graphics_draw_text(
        x, y + 36,
        page_fault_privilege(error_code),
        0x00C7CFD7, 1
    );

    graphics_draw_text(
        x, y + 54,
        (error_code & 0x08U) ?
            "Reserved-bit violation" :
            "No reserved-bit violation",
        0x00C7CFD7, 1
    );

    graphics_draw_text(
        x, y + 72,
        (error_code & 0x10U) ?
            "Instruction fetch" :
            "Data access",
        0x00C7CFD7, 1
    );
}

static void draw_execution_context(
    int x,
    int y,
    const struct exception_frame* frame
)
{
    graphics_draw_text(
        x, y,
        "CPU CONTEXT",
        0x00F2F5F8, 1
    );

    draw_field(x, y + 18, "EIP", frame->eip);
    draw_field(x, y + 36, "CS", frame->cs);
    draw_field(x, y + 54, "EFLAGS", frame->eflags);
    draw_field(x, y + 72, "SAVED ESP", frame->esp);

    draw_field(x, y + 108, "EAX", frame->eax);
    draw_field(x, y + 126, "EBX", frame->ebx);
    draw_field(x, y + 144, "ECX", frame->ecx);
    draw_field(x, y + 162, "EDX", frame->edx);

    draw_field(x, y + 198, "ESI", frame->esi);
    draw_field(x, y + 216, "EDI", frame->edi);
    draw_field(x, y + 234, "EBP", frame->ebp);
}

static void draw_process_context(
    int x,
    int y,
    const struct exception_frame* frame
)
{
    uint32_t pid = process_current_pid();
    const process_t* process = process_get(pid);

    graphics_draw_text(
        x, y,
        "PROCESS CONTEXT",
        0x00F2F5F8, 1
    );

    draw_field(x, y + 18, "PID", pid);

    if (!process)
    {
        graphics_draw_text(
            x, y + 36,
            "No current process record.",
            0x00C7CFD7, 1
        );
        return;
    }

    graphics_draw_text(
        x, y + 36,
        "NAME",
        0x008E9AA5, 1
    );

    graphics_draw_text(
        x + 132, y + 36,
        process->name,
        0x00F2F5F8, 1
    );

    draw_field(
        x, y + 54,
        "STATE",
        (unsigned int)process->state
    );

    draw_field(
        x, y + 72,
        "SAVED STACK",
        process->stack_pointer
    );

    if (frame->eip < (uint32_t)(uintptr_t)&_start ||
        frame->eip >= (uint32_t)(uintptr_t)&_end)
    {
        graphics_draw_text(
            x, y + 90,
            "EIP IS OUTSIDE KERNEL IMAGE",
            0x00FFB4A2, 1
        );
    }
    else
    {
        graphics_draw_text(
            x, y + 90,
            "EIP IS INSIDE KERNEL IMAGE",
            0x00C7CFD7, 1
        );
    }
}

void fault_trace_draw(
    int x,
    int y,
    int width,
    unsigned int exception_number,
    const struct exception_frame* frame,
    unsigned int fault_address,
    int has_fault_address
)
{
    if (!frame)
        return;

    graphics_draw_text(
        x, y,
        "FAULT TRACE",
        0x00F2F5F8, 1
    );

    graphics_draw_text(
        x, y + 18,
        exception_name(exception_number),
        0x00FFB4A2, 1
    );

    draw_field(
        x, y + 36,
        "EXCEPTION",
        exception_number
    );

    draw_field(
        x, y + 54,
        "ERROR CODE",
        frame->error_code
    );

    /*
     * Keep a raw frame sanity check in the panic screen. This deliberately
     * does not use the process/scheduler state, because those may be the
     * subsystem that corrupted the frame in the first place.
     */
    draw_field(
        x, y + 72,
        "FRAME PTR",
        (unsigned int)(uintptr_t)frame
    );

    if (has_fault_address)
        draw_field(
            x, y + 72,
            "FAULT ADDRESS",
            fault_address
        );

    draw_execution_context(
        x,
        y + 102,
        frame
    );

    int right_x = x + width / 2;

    draw_process_context(
        right_x,
        y,
        frame
    );

    if (exception_number == 14)
    {
        draw_page_fault_details(
            right_x,
            y + 116,
            frame->error_code
        );
    }
    else
    {
        graphics_draw_text(
            right_x,
            y + 116,
            "EXCEPTION DETAILS",
            0x00F2F5F8, 1
        );

        graphics_draw_text(
            right_x,
            y + 134,
            exception_name(exception_number),
            0x00C7CFD7, 1
        );
    }
}
