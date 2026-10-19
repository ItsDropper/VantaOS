/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef IDT_H
#define IDT_H

#include <stdint.h>

void idt_initialize(void);
void idt_set_gate(int number, uint64_t base, uint16_t selector, uint8_t flags);
void idt_load(void);

#endif
