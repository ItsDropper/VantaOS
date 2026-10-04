#include "fault_trace.h"

#include "graphics.h"
#include "process.h"

extern unsigned char _start;
extern unsigned char _end;
extern unsigned char _kernel_text_start;
extern unsigned char _kernel_text_end;

struct descriptor_pointer
{
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static void draw_hex(int x, int y, uint64_t value)
{
    const char* hex = "0123456789ABCDEF";
    char text[19];

    text[0] = '0';
    text[1] = 'x';

    for (int i = 0; i < 16; i++)
        text[2 + i] =
            hex[(value >> (60 - i * 4)) & 0x0F];

    text[18] = 0;
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
    uint64_t value
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

    draw_field(x, y + 16, "RIP", frame->rip);
    draw_field(x, y + 32, "CS", frame->cs);
    draw_field(x, y + 48, "RFLAGS", frame->rflags);
    draw_field(x, y + 64, "RAX", frame->rax);
    draw_field(x, y + 80, "RBX", frame->rbx);
    draw_field(x, y + 96, "RCX", frame->rcx);
    draw_field(x, y + 112, "RDX", frame->rdx);
    draw_field(x, y + 128, "RSI", frame->rsi);
    draw_field(x, y + 144, "RDI", frame->rdi);
    draw_field(x, y + 160, "RBP", frame->rbp);
    draw_field(x, y + 176, "R8", frame->r8);
    draw_field(x, y + 192, "R9", frame->r9);
    draw_field(x, y + 208, "R10", frame->r10);
    draw_field(x, y + 224, "R11", frame->r11);
    draw_field(x, y + 240, "R12", frame->r12);
    draw_field(x, y + 256, "R13", frame->r13);
    draw_field(x, y + 272, "R14", frame->r14);
    draw_field(x, y + 288, "R15", frame->r15);
}

static void draw_process_context(int x, int y)
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
    draw_field(x, y + 96, "ENTRY", (uint64_t)(uintptr_t)process->entry);
    draw_field(x, y + 112, "KSTACK", (uint64_t)(uintptr_t)process->kernel_stack);
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
    uintptr_t rip = (uintptr_t)frame->rip;

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
        "RIP IMAGE OFF",
        rip >= image_start && rip < image_end ?
            (uint64_t)(rip - image_start) :
            UINT64_MAX
    );

    draw_field(
        x, y + 96,
        "RIP TEXT OFF",
        rip >= text_start && rip < text_end ?
            (uint64_t)(rip - text_start) :
            UINT64_MAX
    );

    if (rip >= image_start &&
        rip < image_end &&
        rip <= image_end - 8U)
    {
        const unsigned char* bytes =
            (const unsigned char*)rip;

        uint64_t packed =
            ((uint64_t)bytes[0]) |
            ((uint64_t)bytes[1] << 8) |
            ((uint64_t)bytes[2] << 16) |
            ((uint64_t)bytes[3] << 24) |
            ((uint64_t)bytes[4] << 32) |
            ((uint64_t)bytes[5] << 40) |
            ((uint64_t)bytes[6] << 48) |
            ((uint64_t)bytes[7] << 56);

        draw_field(x, y + 112, "BYTES +00", packed);
    }
    else
    {
        graphics_draw_text(
            x, y + 112,
            "BYTES UNAVAILABLE",
            0x00FFB4A2, 1
        );
    }

    draw_field(x, y + 144, "RIP", frame->rip);
}

static void draw_machine_state(int x, int y)
{
    uint64_t cr0, cr2, cr3, cr4;
    uint16_t ds, es, fs, gs, ss, tr, ldtr;
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

    draw_field(x, y + 16, "R15", frame->r15);
    draw_field(x, y + 32, "R14", frame->r14);
    draw_field(x, y + 48, "R13", frame->r13);
    draw_field(x, y + 64, "R12", frame->r12);
    draw_field(x, y + 80, "R11", frame->r11);
    draw_field(x, y + 96, "R10", frame->r10);
    draw_field(x, y + 112, "R9", frame->r9);
    draw_field(x, y + 128, "R8", frame->r8);
    draw_field(x, y + 144, "RDI", frame->rdi);
    draw_field(x, y + 160, "RSI", frame->rsi);
    draw_field(x, y + 176, "RBP", frame->rbp);
    draw_field(x, y + 192, "RDX", frame->rdx);
    draw_field(x, y + 208, "RCX", frame->rcx);
    draw_field(x, y + 224, "RBX", frame->rbx);
    draw_field(x, y + 240, "RAX", frame->rax);
    draw_field(x, y + 256, "ERROR", frame->error_code);
    draw_field(x, y + 272, "RIP", frame->rip);
    draw_field(x, y + 288, "CS", frame->cs);
    draw_field(x, y + 304, "FLAGS", frame->rflags);
    draw_field(x, y + 320, "FRAME PTR", (uintptr_t)frame);
}

void fault_trace_draw(
    int x,
    int y,
    int width,
    unsigned int exception_number,
    const struct exception_frame* frame,
    uint64_t fault_address,
    int has_fault_address
)
{
    if (!frame)
        return;

    int right_x = x + width / 2;
    int lower_y = y + 330;

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
    draw_process_context(right_x, y);
    draw_instruction_diagnostics(right_x, y + 150, frame);

    draw_machine_state(x, lower_y);
    draw_raw_frame(right_x, lower_y, frame);
}
