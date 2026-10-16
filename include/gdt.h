#ifndef GDT_H
#define GDT_H

void gdt_initialize(void);
/* Load the BSP-initialized, shared GDT on a secondary CPU. */
void gdt_load(void);

#endif