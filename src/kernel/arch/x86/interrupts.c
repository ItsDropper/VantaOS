#include "interrupts.h"
#include "idt.h"
#include "pic.h"
#include "timer.h"
#include "keyboard.h"
#include "mouse.h"
#include "panic.h"

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

#define IRQ0_VECTOR 32
#define IRQ1_VECTOR 33
#define IRQ12_VECTOR 44

static void install_exceptions(void)
{
    void (*stubs[32])(void) = {
        exception0_stub, exception1_stub, exception2_stub, exception3_stub,
        exception4_stub, exception5_stub, exception6_stub, exception7_stub,
        exception8_stub, exception9_stub, exception10_stub, exception11_stub,
        exception12_stub, exception13_stub, exception14_stub, exception15_stub,
        exception16_stub, exception17_stub, exception18_stub, exception19_stub,
        exception20_stub, exception21_stub, exception22_stub, exception23_stub,
        exception24_stub, exception25_stub, exception26_stub, exception27_stub,
        exception28_stub, exception29_stub, exception30_stub, exception31_stub
    };

    for (int i = 0; i < 32; i++)
        idt_set_gate(i, (unsigned int)stubs[i], 0x08, 0x8E);
}

void interrupts_initialize(void)
{
    idt_initialize();
    install_exceptions();

    idt_set_gate(IRQ0_VECTOR, (unsigned int)irq0_stub, 0x08, 0x8E);
    idt_set_gate(IRQ1_VECTOR, (unsigned int)irq1_stub, 0x08, 0x8E);
    idt_set_gate(IRQ12_VECTOR, (unsigned int)irq12_stub, 0x08, 0x8E);

    pic_remap();
    timer_initialize(100);
    idt_load();
}

unsigned int interrupt_handler(
    unsigned int interrupt_number,
    unsigned int saved_stack
)
{
    switch (interrupt_number)
    {
        case IRQ1_VECTOR:
            keyboard_handle_interrupt();
            pic_send_eoi(1);
            return saved_stack;

        case IRQ12_VECTOR:
            mouse_handle_interrupt();
            pic_send_eoi(12);
            return saved_stack;

        default:
            if (interrupt_number >= 32 &&
                interrupt_number <= 47)
            {
                pic_send_eoi(
                    interrupt_number - 32
                );
            }
            break;
    }

    return saved_stack;
}

unsigned int interrupts_get_ticks(void)
{
    return timer_get_ticks();
}
