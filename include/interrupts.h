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

    unsigned int error_code;

    unsigned int eip;
    unsigned int cs;
    unsigned int eflags;
};

void interrupts_initialize(void);

void interrupt_handler(unsigned int interrupt_number);

void exception_handler(
    unsigned int exception_number,
    struct exception_frame* frame
);

unsigned int interrupts_get_ticks(void);

void kernel_panic(
    const char* reason,
    unsigned int exception_number,
    struct exception_frame* frame,
    unsigned int fault_address,
    int has_fault_address
);

#endif