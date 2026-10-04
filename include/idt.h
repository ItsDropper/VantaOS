#ifndef IDT_H
#define IDT_H
void idt_initialize(void);
void idt_set_gate(int number, unsigned long base, unsigned short selector, unsigned char flags);
void idt_load(void);
#endif
