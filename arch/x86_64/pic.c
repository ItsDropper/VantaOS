/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "pic.h"
#include "apic.h"

#include <stdint.h>

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1
#define PIC_EOI      0x20

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

void pic_remap(void)
{
    outb(PIC1_COMMAND, 0x11);
    outb(PIC2_COMMAND, 0x11);
    outb(PIC1_DATA, 0x20);
    outb(PIC2_DATA, 0x28);
    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);
    outb(PIC1_DATA, 0xF8);
    outb(PIC2_DATA, 0xEF);
}

#define PIC_READ_ISR 0x0B

/* Restore the default mask state used by pic_remap(): timer, keyboard,
 * cascade and PS/2 mouse unmasked. */
void pic_restore_default_masks(void)
{
    outb(PIC1_DATA, 0xF8);
    outb(PIC2_DATA, 0xEF);
}

static uint8_t pic_read_isr(uint16_t command_port)
{
    uint8_t value;
    outb(command_port, PIC_READ_ISR);
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(command_port));
    return value;
}

/* IRQ7/IRQ15 are raised spuriously when a request vanishes before INTA. A
 * spurious IRQ7 gets no EOI; a spurious IRQ15 needs an EOI on the master only. */
void pic_handle_unexpected_irq(unsigned int vector)
{
    if (apic_is_enabled() && !apic_uses_legacy_pic())
    {
        apic_send_eoi();
        return;
    }

    unsigned int irq = vector - 32U;
    if (irq == 7U)
    {
        if ((pic_read_isr(PIC1_COMMAND) & 0x80U) != 0U)
            outb(PIC1_COMMAND, PIC_EOI);
        return;
    }
    if (irq == 15U)
    {
        if ((pic_read_isr(PIC2_COMMAND) & 0x80U) != 0U)
            outb(PIC2_COMMAND, PIC_EOI);
        outb(PIC1_COMMAND, PIC_EOI);
        return;
    }
    if (irq >= 8U)
        outb(PIC2_COMMAND, PIC_EOI);
    outb(PIC1_COMMAND, PIC_EOI);
}

void pic_send_eoi(unsigned int irq)
{
    if (apic_is_enabled() && !apic_uses_legacy_pic())
    {
        apic_send_eoi();
        return;
    }

    if (irq >= 8)
        outb(PIC2_COMMAND, PIC_EOI);
    outb(PIC1_COMMAND, PIC_EOI);
}

void pic_mask_all(void)
{
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}
