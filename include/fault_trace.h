/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

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
    uintptr_t fault_address,
    int has_fault_address
);

#endif
