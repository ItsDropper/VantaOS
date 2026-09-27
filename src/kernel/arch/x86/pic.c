#include "pic.h"

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
    uint8_t mask1, mask2;

    __asm__ volatile ("inb %1, %0" : "=a"(mask1) : "Nd"((uint16_t)PIC1_DATA));
    __asm__ volatile ("inb %1, %0" : "=a"(mask2) : "Nd"((uint16_t)PIC2_DATA));

    outb(PIC1_COMMAND, 0x11);
    outb(PIC2_COMMAND, 0x11);

    outb(PIC1_DATA, 0x20);
    outb(PIC2_DATA, 0x28);

    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);

    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);

    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);

    /*
     * IRQ12 arrives through the slave PIC's cascade on master IRQ2.
     * The cascade must therefore be unmasked whenever any slave IRQ
     * is enabled. Leaving IRQ2 masked makes the PIC configuration
     * internally inconsistent and can leave pending slave interrupts
     * stuck while the kernel is already accepting interrupts.
     */
    outb(PIC1_DATA, (uint8_t)(mask1 & (uint8_t)~0x07));
    outb(PIC2_DATA, (uint8_t)(mask2 & (uint8_t)~0x10));
}

void pic_send_eoi(unsigned int irq)
{
    if (irq >= 8)
        outb(PIC2_COMMAND, PIC_EOI);

    outb(PIC1_COMMAND, PIC_EOI);
}
