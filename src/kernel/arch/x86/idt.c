#include "idt.h"
#include <stdint.h>

struct idt_entry
{
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct idt_ptr
{
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr idt_pointer;

void idt_set_gate(
    int number,
    unsigned long base,
    unsigned short selector,
    unsigned char flags
)
{
    if (number < 0 || number >= 256)
        return;

    uint64_t address = (uint64_t)base;

    idt[number].offset_low = (uint16_t)(address & 0xFFFF);
    idt[number].selector = selector;
    idt[number].ist = 0;
    idt[number].type_attr = flags;
    idt[number].offset_middle =
        (uint16_t)((address >> 16) & 0xFFFF);
    idt[number].offset_high =
        (uint32_t)(address >> 32);
    idt[number].zero = 0;
}

void idt_initialize(void)
{
    for (int i = 0; i < 256; i++)
        idt_set_gate(i, 0, 0x08, 0x00);

    idt_pointer.limit = sizeof(idt) - 1;
    idt_pointer.base = (uint64_t)(uintptr_t)&idt;
}

void idt_load(void)
{
    __asm__ volatile ("lidt %0" : : "m"(idt_pointer));
}
