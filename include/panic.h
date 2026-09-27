#ifndef PANIC_H
#define PANIC_H

struct exception_frame;

void kernel_panic(
    const char* reason,
    unsigned int exception_number,
    struct exception_frame* frame,
    unsigned int fault_address,
    int has_fault_address
);

void exception_handler(
    unsigned int exception_number,
    struct exception_frame* frame
);

#endif
