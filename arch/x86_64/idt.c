#include "idt.h"

#include <stdint.h>

struct idt_entry
{
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
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

void idt_set_gate(int number, uint64_t base, uint16_t selector, uint8_t flags)
{
    if (number < 0 || number >= 256)
        return;

    idt[number].offset_low = (uint16_t)(base & 0xFFFFU);
    idt[number].selector = selector;
    idt[number].ist = 0;
    idt[number].type_attr = flags;
    idt[number].offset_mid = (uint16_t)((base >> 16) & 0xFFFFU);
    idt[number].offset_high = (uint32_t)((base >> 32) & 0xFFFFFFFFU);
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
