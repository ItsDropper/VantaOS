#ifndef INTERRUPTS_H
#define INTERRUPTS_H

struct exception_frame
{
    unsigned int edi;
    unsigned int esi;
    unsigned int ebp;
    unsigned int esp;
    unsigned int ebx;
    unsigned int edx;
    unsigned int ecx;
    unsigned int eax;
    unsigned int eip;
    unsigned int cs;
    unsigned int eflags;
};

void interrupts_initialize(void);
unsigned int interrupt_handler(
    unsigned int interrupt_number,
    unsigned int saved_stack
);
unsigned int interrupts_get_ticks(void);

#endif
