#include "panic.h"

#include "interrupts.h"
#include "desktop_process.h"

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

    if(frame != 0)
        frame->eip = (unsigned int)(uintptr_t)desktop_process_main;
}
