#include "panic.h"

#include "interrupts.h"

static void panic_halt(void)
{
    __asm__ volatile("cli");
    while(1)
        __asm__ volatile("hlt");
}

void kernel_panic(
    const char* reason,
    unsigned int exception_number,
    struct exception_frame* frame,
    unsigned int fault_address,
    int has_fault_address
)
{
    (void)reason;
    (void)exception_number;
    (void)frame;
    (void)fault_address;
    (void)has_fault_address;

    panic_halt();
}

void exception_handler(
    unsigned int exception_number,
    struct exception_frame* frame
)
{
    (void)exception_number;
    (void)frame;

    panic_halt();
}
