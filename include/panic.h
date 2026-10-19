/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef PANIC_H
#define PANIC_H

#include <stdint.h>

struct exception_frame;

_Noreturn void kernel_panic_halt_all(void);

void kernel_panic(
    const char* reason,
    unsigned int exception_number,
    struct exception_frame* frame,
    uintptr_t fault_address,
    int has_fault_address
);

void exception_handler(
    unsigned int exception_number,
    struct exception_frame* frame
);

#endif
