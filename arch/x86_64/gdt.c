#include "gdt.h"
#include "cpu_local.h"

#include <stdint.h>

#define GDT_USER_DATA_INDEX 3U
#define GDT_USER_CODE_INDEX 4U
#define GDT_TSS_BASE_INDEX 5U
#define GDT_ENTRY_COUNT (GDT_TSS_BASE_INDEX + CPU_LOCAL_MAX * 2U)

typedef struct tss64
{
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed)) tss64_t;

_Static_assert(sizeof(tss64_t) == 104U, "x86-64 TSS must be 104 bytes");

struct gdt_ptr
{
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

/* Shared immutable descriptors; each CPU owns its TSS contents. */
static uint64_t gdt[GDT_ENTRY_COUNT];
static struct gdt_ptr gdt_pointer;
static tss64_t cpu_tss[CPU_LOCAL_MAX];

static void gdt_set_segment(unsigned int index, uint8_t access,
                            uint8_t flags)
{
    /* Flat descriptor. Long-mode code has L=1 and D=0. */
    gdt[index] = 0xFFFFULL |
                 ((uint64_t)access << 40) |
                 (0xFULL << 48) |
                 ((uint64_t)(flags & 0xF0U) << 48);
}

static void gdt_set_tss_descriptor(unsigned int cpu)
{
    const unsigned int index = GDT_TSS_BASE_INDEX + cpu * 2U;
    const uintptr_t base = (uintptr_t)&cpu_tss[cpu];
    const uint32_t limit = (uint32_t)sizeof(tss64_t) - 1U;

    gdt[index] = (uint64_t)(limit & 0xFFFFU) |
                 ((uint64_t)(base & 0xFFFFFFU) << 16) |
                 (0x89ULL << 40) |
                 ((uint64_t)((limit >> 16) & 0x0FU) << 48) |
                 ((uint64_t)((base >> 24) & 0xFFU) << 56);
    gdt[index + 1U] = (uint64_t)(base >> 32);
}

void gdt_initialize(void)
{
    for (unsigned int i = 0; i < GDT_ENTRY_COUNT; i++)
        gdt[i] = 0;

    /* Kernel code/data: selectors 0x08/0x10. */
    gdt_set_segment(1U, 0x9AU, 0xA0U);
    gdt_set_segment(2U, 0x92U, 0xC0U);

    /* User data/code: RPL3 selectors 0x1B/0x23. */
    gdt_set_segment(GDT_USER_DATA_INDEX, 0xF2U, 0xC0U);
    gdt_set_segment(GDT_USER_CODE_INDEX, 0xFAU, 0xA0U);

    for (unsigned int cpu = 0; cpu < CPU_LOCAL_MAX; cpu++)
    {
        cpu_tss[cpu].iomap_base = (uint16_t)sizeof(tss64_t);
        gdt_set_tss_descriptor(cpu);
    }

    gdt_pointer.limit = (uint16_t)(sizeof(gdt) - 1U);
    gdt_pointer.base = (uint64_t)(uintptr_t)&gdt;
    gdt_load();
}

/* Load the BSP-initialized GDT; APs must not rebuild shared descriptors. */
void gdt_load(void)
{
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

int gdt_set_kernel_stack(uint32_t logical_id, uintptr_t stack_top)
{
    if (logical_id >= CPU_LOCAL_MAX || stack_top == 0U ||
        (stack_top & 0xFULL) != 0U)
        return 0;

    cpu_tss[logical_id].rsp0 = (uint64_t)stack_top;
    return 1;
}

int gdt_load_task_register(uint32_t logical_id)
{
    if (logical_id >= CPU_LOCAL_MAX)
        return 0;

    const uint16_t selector =
        (uint16_t)((GDT_TSS_BASE_INDEX + logical_id * 2U) * 8U);
    __asm__ volatile ("ltr %0" : : "r"(selector) : "memory");
    return 1;
}
