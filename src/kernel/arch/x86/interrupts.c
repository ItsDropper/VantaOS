#include "interrupts.h"
#include "keyboard.h"
#include "mouse.h"
#include "terminal.h"

extern void irq0_stub(void);
extern void irq1_stub(void);
extern void irq12_stub(void);

extern void exception0_stub(void);
extern void exception1_stub(void);
extern void exception2_stub(void);
extern void exception3_stub(void);
extern void exception4_stub(void);
extern void exception5_stub(void);
extern void exception6_stub(void);
extern void exception7_stub(void);
extern void exception8_stub(void);
extern void exception9_stub(void);
extern void exception10_stub(void);
extern void exception11_stub(void);
extern void exception12_stub(void);
extern void exception13_stub(void);
extern void exception14_stub(void);
extern void exception15_stub(void);
extern void exception16_stub(void);
extern void exception17_stub(void);
extern void exception18_stub(void);
extern void exception19_stub(void);
extern void exception20_stub(void);
extern void exception21_stub(void);
extern void exception22_stub(void);
extern void exception23_stub(void);
extern void exception24_stub(void);
extern void exception25_stub(void);
extern void exception26_stub(void);
extern void exception27_stub(void);
extern void exception28_stub(void);
extern void exception29_stub(void);
extern void exception30_stub(void);
extern void exception31_stub(void);

struct idt_entry
{
    unsigned short offset_low;
    unsigned short selector;
    unsigned char zero;
    unsigned char type_attr;
    unsigned short offset_high;
} __attribute__((packed));

struct idt_ptr
{
    unsigned short limit;
    unsigned int base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr idt_pointer;

static volatile unsigned int timer_ticks = 0;

static const char* exception_names[] =
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
    "Hypervisor Injection Exception",
    "VMM Communication Exception",
    "Security Exception",
    "Reserved"
};

static void idt_set_gate(
    int number,
    unsigned int handler,
    unsigned short selector,
    unsigned char type_attr
)
{
    idt[number].offset_low = handler & 0xFFFF;
    idt[number].selector = selector;
    idt[number].zero = 0;
    idt[number].type_attr = type_attr;
    idt[number].offset_high = (handler >> 16) & 0xFFFF;
}

static void pic_remap(void)
{
    __asm__ volatile (
        "movb $0x11, %%al\n"
        "outb %%al, $0x20\n"
        "outb %%al, $0xA0\n"

        "movb $0x20, %%al\n"
        "outb %%al, $0x21\n"

        "movb $0x28, %%al\n"
        "outb %%al, $0xA1\n"

        "movb $0x04, %%al\n"
        "outb %%al, $0x21\n"

        "movb $0x02, %%al\n"
        "outb %%al, $0xA1\n"

        "movb $0x01, %%al\n"
        "outb %%al, $0x21\n"
        "outb %%al, $0xA1\n"

        /*
         * Master PIC:
         * IRQ0 = timer       enabled
         * IRQ1 = keyboard    enabled
         * IRQ2 = slave PIC   enabled
         * IRQ3-7             masked
         *
         * 11111000 = 0xF8
         *
         * Slave PIC:
         * IRQ12 = mouse      enabled
         * Everything else    masked
         *
         * IRQ12 is bit 4 on the slave PIC.
         * 11101111 = 0xEF
         */
        "movb $0xF8, %%al\n"
        "outb %%al, $0x21\n"

        "movb $0xEF, %%al\n"
        "outb %%al, $0xA1\n"
        :
        :
        : "al"
    );
}

static void pit_initialize(void)
{
    unsigned int divisor = 1193180 / 100;

    unsigned char low = divisor & 0xFF;
    unsigned char high = (divisor >> 8) & 0xFF;

    __asm__ volatile (
        "movb $0x36, %%al\n"
        "outb %%al, $0x43\n"

        "movb %0, %%al\n"
        "outb %%al, $0x40\n"

        "movb %1, %%al\n"
        "outb %%al, $0x40\n"
        :
        : "r"(low), "r"(high)
        : "al"
    );
}

static void pic_send_eoi(unsigned int interrupt_number)
{
    /*
     * IRQs 8-15 originate from the slave PIC.
     *
     * The slave must receive an EOI first, followed by
     * the master PIC's cascade IRQ2 EOI.
     */
    if (interrupt_number >= 40)
    {
        __asm__ volatile (
            "movb $0x20, %%al\n"
            "outb %%al, $0xA0\n"
            "outb %%al, $0x20\n"
            :
            :
            : "al"
        );
    }
    else
    {
        __asm__ volatile (
            "movb $0x20, %%al\n"
            "outb %%al, $0x20\n"
            :
            :
            : "al"
        );
    }
}

void interrupt_handler(unsigned int interrupt_number)
{
    if (interrupt_number == 32)
    {
        timer_ticks++;
    }
    else if (interrupt_number == 33)
    {
        keyboard_handle_interrupt();
    }
    else if (interrupt_number == 44)
    {
        mouse_handle_interrupt();
    }

    pic_send_eoi(interrupt_number);
}

unsigned int interrupts_get_ticks(void)
{
    return timer_ticks;
}

void exception_handler(
    unsigned int exception_number,
    struct exception_frame* frame
)
{
    __asm__ volatile ("cli");

    terminal_write("\n\n");
    terminal_write("==============================\n");
    terminal_write("       VANTAOS KERNEL PANIC\n");
    terminal_write("==============================\n\n");

    terminal_write("Exception: ");

    if (exception_number < 32)
    {
        terminal_write(exception_names[exception_number]);
    }
    else
    {
        terminal_write("Unknown");
    }

    terminal_write("\nException Number: ");
    terminal_write_hex(exception_number);

    terminal_write("\n\nError Code: ");
    terminal_write_hex(frame->error_code);

    if (exception_number == 14)
    {
        unsigned int fault_address;

        __asm__ volatile (
            "mov %%cr2, %0"
            : "=r"(fault_address)
        );

        terminal_write("\n\nPage Fault Address: ");
        terminal_write_hex(fault_address);

        terminal_write("\nAccess: ");
        terminal_write(
            (frame->error_code & 0x2) ? "write" : "read"
        );

        terminal_write("\nPrivilege: ");
        terminal_write(
            (frame->error_code & 0x4) ? "user" : "kernel"
        );

        terminal_write("\nCause: ");
        terminal_write(
            (frame->error_code & 0x1) ?
            "protection violation" :
            "non-present page"
        );

        if (frame->error_code & 0x8)
            terminal_write("\nReserved-bit violation.");

        if (frame->error_code & 0x10)
            terminal_write("\nInstruction fetch.");
    }

    terminal_write("\n\nEIP:    ");
    terminal_write_hex(frame->eip);

    terminal_write("\nCS:     ");
    terminal_write_hex(frame->cs);

    terminal_write("\nEFLAGS: ");
    terminal_write_hex(frame->eflags);

    terminal_write("\n\nEAX:    ");
    terminal_write_hex(frame->eax);

    terminal_write("\nEBX:    ");
    terminal_write_hex(frame->ebx);

    terminal_write("\nECX:    ");
    terminal_write_hex(frame->ecx);

    terminal_write("\nEDX:    ");
    terminal_write_hex(frame->edx);

    terminal_write("\n\nESI:    ");
    terminal_write_hex(frame->esi);

    terminal_write("\nEDI:    ");
    terminal_write_hex(frame->edi);

    terminal_write("\nEBP:    ");
    terminal_write_hex(frame->ebp);

    terminal_write("\n\nSystem halted.");

    while (1)
    {
        __asm__ volatile ("hlt");
    }
}

void interrupts_initialize(void)
{
    for (int i = 0; i < 256; i++)
    {
        idt_set_gate(i, 0, 0x08, 0x8E);
    }

    idt_set_gate(0,  (unsigned int)exception0_stub,  0x08, 0x8E);
    idt_set_gate(1,  (unsigned int)exception1_stub,  0x08, 0x8E);
    idt_set_gate(2,  (unsigned int)exception2_stub,  0x08, 0x8E);
    idt_set_gate(3,  (unsigned int)exception3_stub,  0x08, 0x8E);
    idt_set_gate(4,  (unsigned int)exception4_stub,  0x08, 0x8E);
    idt_set_gate(5,  (unsigned int)exception5_stub,  0x08, 0x8E);
    idt_set_gate(6,  (unsigned int)exception6_stub,  0x08, 0x8E);
    idt_set_gate(7,  (unsigned int)exception7_stub,  0x08, 0x8E);
    idt_set_gate(8,  (unsigned int)exception8_stub,  0x08, 0x8E);
    idt_set_gate(9,  (unsigned int)exception9_stub,  0x08, 0x8E);
    idt_set_gate(10, (unsigned int)exception10_stub, 0x08, 0x8E);
    idt_set_gate(11, (unsigned int)exception11_stub, 0x08, 0x8E);
    idt_set_gate(12, (unsigned int)exception12_stub, 0x08, 0x8E);
    idt_set_gate(13, (unsigned int)exception13_stub, 0x08, 0x8E);
    idt_set_gate(14, (unsigned int)exception14_stub, 0x08, 0x8E);
    idt_set_gate(15, (unsigned int)exception15_stub, 0x08, 0x8E);
    idt_set_gate(16, (unsigned int)exception16_stub, 0x08, 0x8E);
    idt_set_gate(17, (unsigned int)exception17_stub, 0x08, 0x8E);
    idt_set_gate(18, (unsigned int)exception18_stub, 0x08, 0x8E);
    idt_set_gate(19, (unsigned int)exception19_stub, 0x08, 0x8E);
    idt_set_gate(20, (unsigned int)exception20_stub, 0x08, 0x8E);
    idt_set_gate(21, (unsigned int)exception21_stub, 0x08, 0x8E);
    idt_set_gate(22, (unsigned int)exception22_stub, 0x08, 0x8E);
    idt_set_gate(23, (unsigned int)exception23_stub, 0x08, 0x8E);
    idt_set_gate(24, (unsigned int)exception24_stub, 0x08, 0x8E);
    idt_set_gate(25, (unsigned int)exception25_stub, 0x08, 0x8E);
    idt_set_gate(26, (unsigned int)exception26_stub, 0x08, 0x8E);
    idt_set_gate(27, (unsigned int)exception27_stub, 0x08, 0x8E);
    idt_set_gate(28, (unsigned int)exception28_stub, 0x08, 0x8E);
    idt_set_gate(29, (unsigned int)exception29_stub, 0x08, 0x8E);
    idt_set_gate(30, (unsigned int)exception30_stub, 0x08, 0x8E);
    idt_set_gate(31, (unsigned int)exception31_stub, 0x08, 0x8E);

    idt_set_gate(32, (unsigned int)irq0_stub,  0x08, 0x8E);
    idt_set_gate(33, (unsigned int)irq1_stub,  0x08, 0x8E);
    idt_set_gate(44, (unsigned int)irq12_stub, 0x08, 0x8E);

    idt_pointer.limit = sizeof(idt) - 1;
    idt_pointer.base = (unsigned int)&idt;

    pic_remap();
    pit_initialize();

    __asm__ volatile (
        "lidt %0"
        :
        : "m"(idt_pointer)
    );
}