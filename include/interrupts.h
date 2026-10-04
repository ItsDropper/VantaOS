#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include <stdint.h>

struct exception_frame
{
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rsi, rdi, rbp, rdx, rcx, rbx, rax;
    uint64_t error_code;
    union { uint64_t rip; uint64_t eip; };
    uint64_t cs;
    union { uint64_t rflags; uint64_t eflags; };
    union { uint64_t rsp; uint64_t esp; };
};

void interrupts_initialize(void);
uint64_t interrupt_handler(uint64_t interrupt_number, uint64_t saved_stack);
unsigned int interrupts_get_ticks(void);

#endif
