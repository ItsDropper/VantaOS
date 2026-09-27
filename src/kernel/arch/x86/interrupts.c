#include "interrupts.h"
#include "keyboard.h"
#include "mouse.h"
#include "terminal.h"
#include "graphics.h"

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

static void panic_draw_hex(
    int x,
    int y,
    unsigned int value
)
{
    const char* hex = "0123456789ABCDEF";
    char text[11];

    text[0] = '0';
    text[1] = 'x';

    for (int i = 0; i < 8; i++)
        text[2 + i] = hex[(value >> (28 - i * 4)) & 0xF];

    text[10] = 0;

    graphics_draw_text(
        x,
        y,
        text,
        0x00F2F5F8,
        1
    );
}

static void panic_draw_line(
    int x,
    int y,
    const char* label,
    const char* value
)
{
    graphics_draw_text(
        x,
        y,
        label,
        0x00AEB8C2,
        1
    );

    graphics_draw_text(
        x + 132,
        y,
        value,
        0x00F2F5F8,
        1
    );
}

void kernel_panic(
    const char* reason,
    unsigned int exception_number,
    struct exception_frame* frame,
    unsigned int fault_address,
    int has_fault_address
)
{
    __asm__ volatile ("cli");

    if (graphics_is_initialized())
    {
        int width = (int)graphics_get_width();
        int height = (int)graphics_get_height();

        graphics_clear(0x00000000);

        int x = width >= 900 ? 70 : 32;
        int y = height >= 600 ? 48 : 28;

        graphics_draw_text(
            x,
            y,
            "VANTAOS KERNEL PANIC",
            0x00F2F5F8,
            2
        );

        graphics_draw_text(
            x,
            y + 28,
            "The kernel encountered a fatal error and has stopped.",
            0x00C7CFD7,
            1
        );

        graphics_fill_rect(
            x,
            y + 48,
            width - x * 2,
            1,
            0x004A525A
        );

        graphics_draw_text(
            x,
            y + 68,
            "REASON",
            0x00F2F5F8,
            1
        );

        graphics_draw_text(
            x,
            y + 84,
            reason ? reason : "Unknown kernel failure",
            0x00C7CFD7,
            1
        );

        int info_y = y + 116;

        if (exception_number == 0xFFFFFFFFU)
        {
            panic_draw_line(
                x,
                info_y,
                "STOP CODE",
                "VANTAOS TEST FAULT"
            );
        }
        else
        {
            panic_draw_line(
                x,
                info_y,
                "EXCEPTION",
                exception_names[exception_number < 32 ?
                    exception_number : 31]
            );

            panic_draw_line(
                x,
                info_y + 18,
                "NUMBER",
                ""
            );

            panic_draw_hex(
                x + 132,
                info_y + 18,
                exception_number
            );
        }

        if (frame != 0)
        {
            panic_draw_line(
                x,
                info_y + 60,
                "ERROR CODE",
                ""
            );

            panic_draw_hex(
                x + 132,
                info_y + 60,
                frame->error_code
            );

            panic_draw_line(
                x,
                info_y + 78,
                "EIP",
                ""
            );

            panic_draw_hex(
                x + 132,
                info_y + 78,
                frame->eip
            );

            panic_draw_line(
                x,
                info_y + 96,
                "CS",
                ""
            );

            panic_draw_hex(
                x + 132,
                info_y + 96,
                frame->cs
            );

            panic_draw_line(
                x,
                info_y + 114,
                "EFLAGS",
                ""
            );

            panic_draw_hex(
                x + 132,
                info_y + 114,
                frame->eflags
            );

            if (has_fault_address)
            {
                panic_draw_line(
                    x,
                    info_y + 132,
                    "FAULT ADDRESS",
                    ""
                );

                panic_draw_hex(
                    x + 132,
                    info_y + 132,
                    fault_address
                );
            }
        }

        int footer_y = height - 54;

        graphics_draw_text(
            x,
            footer_y,
            "SYSTEM HALTED",
            0x00F2F5F8,
            1
        );

        graphics_draw_text(
            x,
            footer_y + 16,
            "The kernel will not continue execution.",
            0x00AEB8C2,
            1
        );

        while (1)
            __asm__ volatile ("hlt");
    }

    terminal_reset();

    terminal_write("\nVANTAOS KERNEL PANIC\n\n");
    terminal_write("Reason: ");
    terminal_write(reason ? reason : "Unknown kernel failure");
    terminal_write("\n");

    if (exception_number != 0xFFFFFFFFU)
    {
        terminal_write("Exception: ");

        if (exception_number < 32)
            terminal_write(exception_names[exception_number]);
        else
            terminal_write("Unknown");

        terminal_write("\n");
    }

    if (frame != 0)
    {
        terminal_write("Error Code: ");
        terminal_write_hex(frame->error_code);

        terminal_write("\nEIP: ");
        terminal_write_hex(frame->eip);

        terminal_write("\nCS: ");
        terminal_write_hex(frame->cs);

        terminal_write("\nEFLAGS: ");
        terminal_write_hex(frame->eflags);

        if (has_fault_address)
        {
            terminal_write("\nFault Address: ");
            terminal_write_hex(fault_address);
        }

        terminal_write("\n");
    }

    terminal_write("\nSYSTEM HALTED\n");

    while (1)
        __asm__ volatile ("hlt");
}

void exception_handler(
    unsigned int exception_number,
    struct exception_frame* frame
)
{
    unsigned int fault_address = 0;
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
        exception_number < 32 ?
            exception_names[exception_number] :
            "Unknown CPU exception",
        exception_number,
        frame,
        fault_address,
        has_fault_address
    );

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