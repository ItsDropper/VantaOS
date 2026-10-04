#include "gdt.h"

#include <stdint.h>

struct gdt_entry
{
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));

struct gdt_ptr
{
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct gdt_entry gdt[3];
static struct gdt_ptr gdt_pointer;

static void gdt_set_gate(int number, uint32_t base, uint32_t limit,
                         uint8_t access, uint8_t granularity)
{
    gdt[number].base_low = base & 0xFFFFU;
    gdt[number].base_middle = (base >> 16) & 0xFFU;
    gdt[number].base_high = (base >> 24) & 0xFFU;
    gdt[number].limit_low = limit & 0xFFFFU;
    gdt[number].granularity = (limit >> 16) & 0x0FU;
    gdt[number].granularity |= granularity & 0xF0U;
    gdt[number].access = access;
}

void gdt_initialize(void)
{
    gdt_pointer.limit = sizeof(gdt) - 1;
    gdt_pointer.base = (uint64_t)(uintptr_t)&gdt;

    gdt_set_gate(0, 0, 0, 0, 0);

    /* Long-mode kernel code: L=1, D=0, present, ring 0, executable. */
    gdt_set_gate(1, 0, 0xFFFFFFFFU, 0x9AU, 0xA0U);

    /* Kernel data segment. Segmentation is effectively flat in long mode. */
    gdt_set_gate(2, 0, 0xFFFFFFFFU, 0x92U, 0x00U);

    __asm__ volatile (
        "lgdt %0\n"
        "pushq $0x08\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        "movw $0x10, %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%ss\n"
        :
        : "m"(gdt_pointer)
        : "rax", "memory"
    );
}
