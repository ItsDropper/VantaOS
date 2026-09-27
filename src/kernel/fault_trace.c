#include "fault_trace.h"

#include "graphics.h"
#include "process.h"

extern unsigned char _start;
extern unsigned char _end;
extern unsigned char _kernel_text_start;
extern unsigned char _kernel_text_end;

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

    graphics_draw_text(x, y, text, 0x00F2F5F8, 1);
}

static void draw_label(int x, int y, const char* label)
{
    graphics_draw_text(x, y, label, 0x008E9AA5, 1);
}

static void draw_field(
    int x,
    int y,
    const char* label,
    unsigned int value
)
{
    draw_label(x, y, label);
    draw_hex(x + 118, y, value);
}

static const char* exception_name(unsigned int number)
{
    static const char* names[] =
    {
        "Divide Error", "Debug", "Non-Maskable Interrupt",
        "Breakpoint", "Overflow", "BOUND Range Exceeded",
        "Invalid Opcode", "Device Not Available", "Double Fault",
        "Coprocessor Segment Overrun", "Invalid TSS",
        "Segment Not Present", "Stack-Segment Fault",
        "General Protection Fault", "Page Fault", "Reserved",
        "x87 Floating-Point Exception", "Alignment Check",
        "Machine Check", "SIMD Floating-Point Exception",
        "Virtualization Exception", "Control Protection Exception",
        "Reserved", "Reserved", "Reserved", "Reserved",
        "Reserved", "Hypervisor Injection", "VMM Communication",
        "Security Exception", "Reserved", "Unknown"
    };

    return number < 31 ? names[number] : names[31];
}

static void draw_execution_context(
    int x,
    int y,
    const struct exception_frame* frame
)
{
    graphics_draw_text(x, y, "CPU CONTEXT", 0x00F2F5F8, 1);

    draw_field(x, y + 16, "EIP", frame->eip);
    draw_field(x, y + 32, "CS", frame->cs);
    draw_field(x, y + 48, "EFLAGS", frame->eflags);
    draw_field(x, y + 64, "SAVED ESP", frame->esp);
    draw_field(x, y + 80, "EAX", frame->eax);
    draw_field(x, y + 96, "EBX", frame->ebx);
    draw_field(x, y + 112, "ECX", frame->ecx);
    draw_field(x, y + 128, "EDX", frame->edx);
    draw_field(x, y + 144, "ESI", frame->esi);
    draw_field(x, y + 160, "EDI", frame->edi);
    draw_field(x, y + 176, "EBP", frame->ebp);
}

static void draw_process_context(
    int x,
    int y,
    const struct exception_frame* frame
)
{
    uint32_t pid = process_current_pid();
    const process_t* process = process_get(pid);

    graphics_draw_text(x, y, "PROCESS CONTEXT", 0x00F2F5F8, 1);
    draw_field(x, y + 16, "PID", pid);

    if (!process)
    {
        graphics_draw_text(
            x, y + 32,
            "NO PROCESS RECORD",
            0x00FFB4A2, 1
        );
        return;
    }

    graphics_draw_text(x, y + 32, "NAME", 0x008E9AA5, 1);
    graphics_draw_text(
        x + 118, y + 32,
        process->name,
        0x00F2F5F8, 1
    );

    draw_field(x, y + 48, "STATE", process->state);
    draw_field(x, y + 64, "SAVED STACK", process->stack_pointer);
    draw_field(x, y + 80, "PARENT PID", process->parent_pid);
    draw_field(x, y + 96, "ENTRY", (uint32_t)(uintptr_t)process->entry);
    draw_field(x, y + 112, "KSTACK", (uint32_t)(uintptr_t)process->kernel_stack);

    if (frame->eip < (uint32_t)(uintptr_t)&_start ||
        frame->eip >= (uint32_t)(uintptr_t)&_end)
    {
        graphics_draw_text(
            x, y + 128,
            "EIP OUTSIDE IMAGE",
            0x00FFB4A2, 1
        );
    }
    else if (
        frame->eip >= (uint32_t)(uintptr_t)&_kernel_text_start &&
        frame->eip < (uint32_t)(uintptr_t)&_kernel_text_end)
    {
        graphics_draw_text(
            x, y + 128,
            "EIP INSIDE .TEXT",
            0x00C7CFD7, 1
        );
    }
    else
    {
        graphics_draw_text(
            x, y + 128,
            "EIP IN IMAGE DATA/BOOT",
            0x00FFB4A2, 1
        );
    }
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

    draw_field(x, y + 16, "IMAGE START", image_start);
    draw_field(x, y + 32, "IMAGE END", image_end);
    draw_field(x, y + 48, "TEXT START", text_start);
    draw_field(x, y + 64, "TEXT END", text_end);

    draw_field(
        x, y + 80,
        "EIP IMAGE OFF",
        eip >= image_start && eip < image_end ?
            (unsigned int)(eip - image_start) :
            0xFFFFFFFFU
    );

    draw_field(
        x, y + 96,
        "EIP TEXT OFF",
        eip >= text_start && eip < text_end ?
            (unsigned int)(eip - text_start) :
            0xFFFFFFFFU
    );

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

        draw_field(x, y + 112, "BYTES +00", packed0);
        draw_field(x, y + 128, "BYTES +04", packed1);
    }
    else
    {
        graphics_draw_text(
            x, y + 112,
            "BYTES UNAVAILABLE",
            0x00FFB4A2, 1
        );
    }

    draw_field(x, y + 144, "EIP", frame->eip);
}

static void draw_machine_state(int x, int y)
{
    unsigned int cr0, cr2, cr3, cr4;
    unsigned int ds, es, fs, gs, ss, tr, ldtr;
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

    graphics_draw_text(x, y, "MACHINE STATE", 0x00F2F5F8, 1);

    draw_field(x, y + 16, "CR0", cr0);
    draw_field(x, y + 32, "CR2", cr2);
    draw_field(x, y + 48, "CR3", cr3);
    draw_field(x, y + 64, "CR4", cr4);
    draw_field(x, y + 80, "DS", ds);
    draw_field(x, y + 96, "ES", es);
    draw_field(x, y + 112, "FS", fs);
    draw_field(x, y + 128, "GS", gs);
    draw_field(x, y + 144, "SS", ss);
    draw_field(x, y + 160, "TR", tr);
    draw_field(x, y + 176, "LDTR", ldtr);
    draw_field(x, y + 192, "GDTR BASE", gdtr.base);
    draw_field(x, y + 208, "GDTR LIMIT", gdtr.limit);
    draw_field(x, y + 224, "IDTR BASE", idtr.base);
    draw_field(x, y + 240, "IDTR LIMIT", idtr.limit);
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

    draw_field(x, y + 16, "EDI", frame->edi);
    draw_field(x, y + 32, "ESI", frame->esi);
    draw_field(x, y + 48, "EBP", frame->ebp);
    draw_field(x, y + 64, "ESP", frame->esp);
    draw_field(x, y + 80, "EBX", frame->ebx);
    draw_field(x, y + 96, "EDX", frame->edx);
    draw_field(x, y + 112, "ECX", frame->ecx);
    draw_field(x, y + 128, "EAX", frame->eax);
    draw_field(x, y + 144, "ERROR", frame->error_code);
    draw_field(x, y + 160, "EIP", frame->eip);
    draw_field(x, y + 176, "CS", frame->cs);
    draw_field(x, y + 192, "FLAGS", frame->eflags);
    draw_field(x, y + 208, "FRAME PTR", (uint32_t)(uintptr_t)frame);
}

static void draw_interrupted_stack(
    int x,
    int y,
    unsigned int exception_number,
    const struct exception_frame* frame
)
{
    graphics_draw_text(
        x, y,
        "INTERRUPTED STACK",
        0x00F2F5F8, 1
    );

    /*
     * For a no-error-code exception, frame->esp is the value saved
     * by PUSHA after the stub pushed the synthetic error code.
     * Therefore the ESP at the moment the CPU took the exception
     * is frame->esp + 16.
     *
     * For an error-code exception the CPU supplied the error code,
     * so the interrupted ESP is frame->esp + 12.
     */
    unsigned int interrupted_esp =
        frame->esp +
        (exception_number == 8 ||
         (exception_number >= 10 && exception_number <= 14) ? 12U : 16U);

    draw_field(x, y + 16, "INTERRUPTED ESP", interrupted_esp);
    draw_field(
        x, y + 32,
        "STACK EIP",
        *(volatile unsigned int*)(interrupted_esp - 12U)
    );
    draw_field(
        x, y + 48,
        "STACK CS",
        *(volatile unsigned int*)(interrupted_esp - 8U)
    );
    draw_field(
        x, y + 64,
        "STACK FLAGS",
        *(volatile unsigned int*)(interrupted_esp - 4U)
    );

    draw_field(
        x, y + 80,
        "ESP +00",
        *(volatile unsigned int*)interrupted_esp
    );
    draw_field(
        x, y + 96,
        "ESP +04",
        *(volatile unsigned int*)(interrupted_esp + 4U)
    );
    draw_field(
        x, y + 112,
        "ESP +08",
        *(volatile unsigned int*)(interrupted_esp + 8U)
    );
    draw_field(
        x, y + 128,
        "ESP +0C",
        *(volatile unsigned int*)(interrupted_esp + 12U)
    );

    unsigned int found = 0;
    for (unsigned int i = 0; i < 32; i++)
    {
        unsigned int value =
            *(volatile unsigned int*)(interrupted_esp + i * 4U);

        if (value == frame->eip)
        {
            found = interrupted_esp + i * 4U;
            break;
        }
    }

    draw_field(x, y + 144, "EIP ON STACK", found);
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

    int right_x = x + width / 2;
    int lower_y = y + 250;

    graphics_draw_text(
        x, y,
        "FAULT TRACE",
        0x00F2F5F8, 1
    );

    graphics_draw_text(
        x, y + 16,
        exception_name(exception_number),
        0x00FFB4A2, 1
    );

    draw_field(x, y + 32, "EXCEPTION", exception_number);
    draw_field(x, y + 48, "ERROR CODE", frame->error_code);

    if (has_fault_address)
        draw_field(x, y + 64, "FAULT ADDRESS", fault_address);

    draw_execution_context(x, y + 86, frame);
    draw_process_context(right_x, y, frame);
    draw_instruction_diagnostics(right_x, y + 150, frame);

    draw_machine_state(x, lower_y);
    draw_raw_frame(right_x, lower_y, frame);
    draw_interrupted_stack(right_x, lower_y + 218, exception_number, frame);
}
