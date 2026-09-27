#include "idt.h"

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

void idt_set_gate(
    int number,
    unsigned int base,
    unsigned short selector,
    unsigned char flags
)
{
    if (number < 0 || number >= 256)
        return;

    idt[number].offset_low = (unsigned short)(base & 0xFFFF);
    idt[number].selector = selector;
    idt[number].zero = 0;
    idt[number].type_attr = flags;
    idt[number].offset_high = (unsigned short)((base >> 16) & 0xFFFF);
}

void idt_initialize(void)
{
    /* Leave unused vectors non-present. A present gate with offset 0
     * turns any unexpected vector into a jump through address 0. */
    for (int i = 0; i < 256; i++)
        idt_set_gate(i, 0, 0x08, 0x00);

    idt_pointer.limit = sizeof(idt) - 1;
    idt_pointer.base = (unsigned int)&idt;
}

void idt_load(void)
{
    __asm__ volatile ("lidt %0" : : "m"(idt_pointer));
}
