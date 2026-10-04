#ifndef IDT_H
#define IDT_H

#include <stdint.h>

void idt_initialize(void);
void idt_set_gate(int number, uint64_t base, uint16_t selector, uint8_t flags);
void idt_load(void);

#endif
