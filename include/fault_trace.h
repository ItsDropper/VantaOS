#ifndef FAULT_TRACE_H
#define FAULT_TRACE_H

#include <stdint.h>

#include "interrupts.h"

void fault_trace_draw(
    int x,
    int y,
    int width,
    unsigned int exception_number,
    const struct exception_frame* frame,
    unsigned int fault_address,
    int has_fault_address
);

#endif
