#include "gdt.h"

struct gdt_entry
{
    unsigned short limit_low;
    unsigned short base_low;
    unsigned char base_middle;
    unsigned char access;
    unsigned char granularity;
    unsigned char base_high;
} __attribute__((packed));

struct gdt_ptr
{
    unsigned short limit;
    unsigned int base;
} __attribute__((packed));

static struct gdt_entry gdt[3];
static struct gdt_ptr gdt_pointer;

static void gdt_set_gate(
    int number,
    unsigned int base,
    unsigned int limit,
    unsigned char access,
    unsigned char granularity
)
{
    gdt[number].base_low = base & 0xFFFF;
    gdt[number].base_middle = (base >> 16) & 0xFF;
    gdt[number].base_high = (base >> 24) & 0xFF;

    gdt[number].limit_low = limit & 0xFFFF;
    gdt[number].granularity = (limit >> 16) & 0x0F;
    gdt[number].granularity |= granularity & 0xF0;

    gdt[number].access = access;
}

void gdt_initialize(void)
{
    gdt_pointer.limit = sizeof(gdt) - 1;
    gdt_pointer.base = (unsigned int)&gdt;

    /*
     * Entry 0: Null descriptor.
     */
    gdt_set_gate(
        0,
        0,
        0,
        0,
        0
    );

    /*
     * Entry 1: Kernel code.
     *
     * Base:      0
     * Limit:     4 GB
     * Access:    present, ring 0, executable, readable
     * Granularity: 4 KB pages, 32-bit
     */
    gdt_set_gate(
        1,
        0,
        0xFFFFFFFF,
        0x9A,
        0xCF
    );

    /*
     * Entry 2: Kernel data.
     *
     * Base:      0
     * Limit:     4 GB
     * Access:    present, ring 0, writable
     * Granularity: 4 KB pages, 32-bit
     */
    gdt_set_gate(
        2,
        0,
        0xFFFFFFFF,
        0x92,
        0xCF
    );

    /*
     * Load our GDT.
     */
    __asm__ volatile (
        "lgdt %0\n"

        /*
         * Reload CS with our kernel code selector.
         */
        "ljmp $0x08, $1f\n"

        "1:\n"

        /*
         * Reload all data segments with our kernel data selector.
         */
        "movw $0x10, %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%fs\n"
        "movw %%ax, %%gs\n"
        "movw %%ax, %%ss\n"

        :
        : "m"(gdt_pointer)
        : "ax"
    );
}