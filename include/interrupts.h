#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include <stddef.h>
#include <stdint.h>

struct exception_frame
{
    uint64_t rax, rbx, rcx, rdx, rbp, rdi, rsi;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t error_code;
    union { uint64_t rip; uint64_t eip; };
    uint64_t cs;
    union { uint64_t rflags; uint64_t eflags; };
    union { uint64_t rsp; uint64_t esp; };
};

_Static_assert(offsetof(struct exception_frame, rax) == 0, "ISR frame: RAX offset");
_Static_assert(offsetof(struct exception_frame, r15) == 112, "ISR frame: R15 offset");
_Static_assert(offsetof(struct exception_frame, error_code) == 120, "ISR frame: error offset");
_Static_assert(offsetof(struct exception_frame, rip) == 128, "ISR frame: RIP offset");
_Static_assert(offsetof(struct exception_frame, cs) == 136, "ISR frame: CS offset");
_Static_assert(offsetof(struct exception_frame, rflags) == 144, "ISR frame: RFLAGS offset");

void interrupts_initialize(void);
uint64_t interrupt_handler(uint64_t interrupt_number, uint64_t saved_stack);
unsigned int interrupts_get_ticks(void);

#endif
