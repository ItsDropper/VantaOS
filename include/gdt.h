#ifndef GDT_H
#define GDT_H

#include <stdint.h>

#define GDT_USER_CODE_SELECTOR 0x23U
#define GDT_USER_DATA_SELECTOR 0x1BU

void gdt_initialize(void);
/* Load the BSP-initialized, shared GDT on a secondary CPU. */
void gdt_load(void);

/*
 * Set the ring-0 entry stack for one CPU's TSS. The stack top must be
 * 16-byte aligned and remain mapped in every address space used by that CPU.
 */
int gdt_set_kernel_stack(uint32_t logical_id, uintptr_t stack_top);

/* Load the TSS descriptor assigned to this logical CPU into TR. */
int gdt_load_task_register(uint32_t logical_id);

#endif
