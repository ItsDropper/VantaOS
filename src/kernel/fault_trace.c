#include "fault_trace.h"

#include "graphics.h"
#include "process.h"

extern unsigned char _start;
extern unsigned char _end;
extern unsigned char _kernel_text_start;
extern unsigned char _kernel_text_end;
extern unsigned char stack_bottom;
extern unsigned char stack_top;

struct descriptor_pointer
{
    unsigned short limit;
    unsigned int base;
} __attribute__((packed));

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

static void draw_instruction_diagnostics(
    int x,
    int y,
    const struct exception_frame* frame
)
{
    uintptr_t image_start = (uintptr_t)&_start;
    uintptr_t image_end = (uintptr_t)&_end;
    uintptr_t text_start = (uintptr_t)&_kernel_text_start;
    uintptr_t text_end = (uintptr_t)&_kernel_text_end;
    uintptr_t eip = (uintptr_t)frame->eip;

    graphics_draw_text(
        x, y,
        "INSTRUCTION DIAGNOSTICS",
        0x00F2F5F8, 1
    );

    draw_field(
        x, y + 18,
        "IMAGE START",
        (unsigned int)image_start
    );

    draw_field(
        x, y + 36,
        "IMAGE END",
        (unsigned int)image_end
    );

    draw_field(
        x, y + 54,
        "TEXT START",
        (unsigned int)text_start
    );

    draw_field(
        x, y + 72,
        "TEXT END",
        (unsigned int)text_end
    );

    draw_field(
        x, y + 90,
        "EIP IMAGE OFF",
        eip >= image_start && eip < image_end ?
            (unsigned int)(eip - image_start) :
            0xFFFFFFFFU
    );

    draw_field(
        x, y + 108,
        "EIP TEXT OFF",
        eip >= text_start && eip < text_end ?
            (unsigned int)(eip - text_start) :
            0xFFFFFFFFU
    );

    /*
     * The previous diagnostic only read bytes when EIP was inside .text.
     * That hid the most useful evidence for an EIP such as 0x1000D,
     * because that address is inside the kernel image but before .text.
     * Reading inside the complete linked image lets us inspect the exact
     * bytes the CPU was executing, including boot/header corruption.
     */
    if (eip >= image_start &&
        eip < image_end &&
        eip <= image_end - 8U)
    {
        const unsigned char* bytes =
            (const unsigned char*)eip;

        unsigned int packed0 =
            ((unsigned int)bytes[0]) |
            ((unsigned int)bytes[1] << 8) |
            ((unsigned int)bytes[2] << 16) |
            ((unsigned int)bytes[3] << 24);

        unsigned int packed1 =
            ((unsigned int)bytes[4]) |
            ((unsigned int)bytes[5] << 8) |
            ((unsigned int)bytes[6] << 16) |
            ((unsigned int)bytes[7] << 24);

        draw_field(x, y + 126, "BYTES +00", packed0);
        draw_field(x, y + 144, "BYTES +04", packed1);
    }
    else
    {
        graphics_draw_text(
            x, y + 126,
            "CODE BYTES: unavailable",
            0x00FFB4A2, 1
        );
    }
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

    draw_field(
        x, y + 90,
        "PARENT PID",
        process->parent_pid
    );

    draw_field(
        x, y + 108,
        "ENTRY",
        (unsigned int)(uintptr_t)process->entry
    );

    draw_field(
        x, y + 126,
        "KSTACK",
        (unsigned int)(uintptr_t)process->kernel_stack
    );

    if (frame->eip < (uint32_t)(uintptr_t)&_start ||
        frame->eip >= (uint32_t)(uintptr_t)&_end)
    {
        graphics_draw_text(
            x, y + 144,
            "EIP IS OUTSIDE KERNEL IMAGE",
            0x00FFB4A2, 1
        );
    }
    else if (
        frame->eip >= (uint32_t)(uintptr_t)&_kernel_text_start &&
        frame->eip < (uint32_t)(uintptr_t)&_kernel_text_end)
    {
        graphics_draw_text(
            x, y + 144,
            "EIP IS INSIDE .TEXT",
            0x00C7CFD7, 1
        );
    }
    else
    {
        graphics_draw_text(
            x, y + 144,
            "EIP IS IMAGE DATA/BOOT AREA",
            0x00FFB4A2, 1
        );
    }
}

static void draw_machine_state(
    int x,
    int y
)
{
    unsigned int cr0;
    unsigned int cr2;
    unsigned int cr3;
    unsigned int cr4;
    unsigned int ds;
    unsigned int es;
    unsigned int fs;
    unsigned int gs;
    unsigned int ss;
    unsigned int tr;
    unsigned int ldtr;
    struct descriptor_pointer gdtr;
    struct descriptor_pointer idtr;

    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));

    __asm__ volatile("mov %%ds, %0" : "=r"(ds));
    __asm__ volatile("mov %%es, %0" : "=r"(es));
    __asm__ volatile("mov %%fs, %0" : "=r"(fs));
    __asm__ volatile("mov %%gs, %0" : "=r"(gs));
    __asm__ volatile("mov %%ss, %0" : "=r"(ss));
    __asm__ volatile("str %0" : "=r"(tr));
    __asm__ volatile("sldt %0" : "=r"(ldtr));

    __asm__ volatile("sgdt %0" : "=m"(gdtr));
    __asm__ volatile("sidt %0" : "=m"(idtr));

    graphics_draw_text(
        x, y,
        "MACHINE STATE",
        0x00F2F5F8, 1
    );

    draw_field(x, y + 18, "CR0", cr0);
    draw_field(x, y + 36, "CR2", cr2);
    draw_field(x, y + 54, "CR3", cr3);
    draw_field(x, y + 72, "CR4", cr4);

    draw_field(x, y + 90, "DS", ds);
    draw_field(x, y + 108, "ES", es);
    draw_field(x, y + 126, "FS", fs);
    draw_field(x, y + 144, "GS", gs);

    draw_field(x, y + 162, "SS", ss);
    draw_field(x, y + 180, "TR", tr);
    draw_field(x, y + 198, "LDTR", ldtr);
    draw_field(x, y + 216, "GDTR BASE", gdtr.base);
    draw_field(x, y + 234, "GDTR LIMIT", gdtr.limit);
    draw_field(x, y + 252, "IDTR BASE", idtr.base);
    draw_field(x, y + 270, "IDTR LIMIT", idtr.limit);
}

static void draw_raw_frame(
    int x,
    int y,
    const struct exception_frame* frame
)
{
    graphics_draw_text(
        x, y,
        "RAW EXCEPTION FRAME",
        0x00F2F5F8, 1
    );

    draw_field(x, y + 18, "RAW +00 EDI", frame->edi);
    draw_field(x, y + 36, "RAW +04 ESI", frame->esi);
    draw_field(x, y + 54, "RAW +08 EBP", frame->ebp);
    draw_field(x, y + 72, "RAW +0C ESP", frame->esp);
    draw_field(x, y + 90, "RAW +10 EBX", frame->ebx);
    draw_field(x, y + 108, "RAW +14 EDX", frame->edx);
    draw_field(x, y + 126, "RAW +18 ECX", frame->ecx);
    draw_field(x, y + 144, "RAW +1C EAX", frame->eax);
    draw_field(x, y + 162, "RAW +20 ERR", frame->error_code);
    draw_field(x, y + 180, "RAW +24 EIP", frame->eip);
    draw_field(x, y + 198, "RAW +28 CS", frame->cs);
    draw_field(x, y + 216, "RAW +2C FLAGS", frame->eflags);
    draw_field(x, y + 234, "FRAME PTR", (unsigned int)(uintptr_t)frame);
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

    draw_instruction_diagnostics(
        right_x,
        y + 164,
        frame
    );

    draw_machine_state(
        x,
        y + 370
    );

    draw_raw_frame(
        right_x,
        y + 370,
        frame
    );
}
