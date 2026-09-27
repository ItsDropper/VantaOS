#include "interrupts.h"
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