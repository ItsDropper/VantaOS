/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "power.h"
#include "serial.h"

#include <stdint.h>

_Noreturn void power_restart(void)
{
    serial_write("VANTAOS_POWER_RESTART\n");
    __asm__ volatile ("cli" ::: "memory");

    /* Legacy 8042 keyboard-controller reset, supported by QEMU and many PCs. */
    for (unsigned int attempt = 0; attempt < 100000U; ++attempt) {
        uint8_t status;
        __asm__ volatile ("inb $0x64, %0" : "=a"(status));
        if ((status & 0x02U) == 0U) {
            uint8_t reset = 0xFEU;
            __asm__ volatile ("outb %0, $0x64" : : "a"(reset));
            break;
        }
    }

    /* If the controller is absent or ignores the request, remain safely halted. */
    for (;;) __asm__ volatile ("hlt");
}

_Noreturn void power_shutdown(void)
{
    serial_write("VANTAOS_POWER_SHUTDOWN\n");
    __asm__ volatile ("cli" ::: "memory");

    /* QEMU/Bochs ACPI shutdown ports; other platforms may require ACPI AML. */
    __asm__ volatile ("outw %0, %1" : : "a"((uint16_t)0x2000U),
                      "Nd"((uint16_t)0x0604U));
    __asm__ volatile ("outw %0, %1" : : "a"((uint16_t)0x2000U),
                      "Nd"((uint16_t)0xB004U));

    for (;;) __asm__ volatile ("hlt");
}
