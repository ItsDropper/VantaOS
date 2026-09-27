#include "panic.h"

#include "interrupts.h"

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
}

void exception_handler(
    unsigned int exception_number,
    struct exception_frame* frame
)
{
    kernel_panic(0, exception_number, frame, 0, 0);
}
