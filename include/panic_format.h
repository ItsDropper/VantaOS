/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTAOS_PANIC_FORMAT_H
#define VANTAOS_PANIC_FORMAT_H

#include <stddef.h>
#include <stdint.h>

static inline void panic_format_hex64(char out[19], uint64_t value)
{
    static const char digits[] = "0123456789ABCDEF";
    out[0] = '0';
    out[1] = 'x';
    for (unsigned int i = 0; i < 16U; ++i)
        out[i + 2U] = digits[(value >> (60U - i * 4U)) & 0x0FU];
    out[18] = '\0';
}

static inline const char *panic_format_exception_name(unsigned int number)
{
    static const char *const names[32] = {
        "Divide Error", "Debug", "Non-Maskable Interrupt", "Breakpoint",
        "Overflow", "BOUND Range Exceeded", "Invalid Opcode",
        "Device Not Available", "Double Fault", "Coprocessor Segment Overrun",
        "Invalid TSS", "Segment Not Present", "Stack-Segment Fault",
        "General Protection Fault", "Page Fault", "Reserved",
        "x87 Floating-Point Exception", "Alignment Check", "Machine Check",
        "SIMD Floating-Point Exception", "Virtualization Exception",
        "Control Protection Exception", "Reserved", "Reserved", "Reserved",
        "Reserved", "Reserved", "Hypervisor Injection Exception",
        "VMM Communication Exception", "Security Exception", "Reserved",
        "Unknown"
    };
    return number < 31U ? names[number] : names[31];
}

#endif
