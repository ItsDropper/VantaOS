/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_DEVICE_IRQ_H
#define VANTA_DEVICE_IRQ_H

#include <stdint.h>

#define DEVICE_IRQ_VECTOR_FIRST 0x40U
#define DEVICE_IRQ_VECTOR_LAST  0xDFU
#define DEVICE_IRQ_VECTOR_COUNT 160U
#define DEVICE_IRQ_VECTOR_RESERVED 0x80U

typedef void (*device_irq_handler_t)(uint8_t vector, void *context);

/* Install generic IDT stubs before loading the IDT. Called by interrupts_initialize. */
int device_irq_initialize(void);

/* MSI/MSI-X may be enabled only after a vector is claimed and a handler registered. */
int device_irq_vector_claim(uint8_t *out_vector);
int device_irq_vector_register(uint8_t vector, device_irq_handler_t handler,
                               void *context);

/*
 * Caller must mask/disable the device source before release. Release removes the
 * callback and waits for already-running callbacks to drain. Never call from
 * an interrupt callback or while holding a lock that callback needs.
 */
int device_irq_vector_release(uint8_t vector);

/* Assembly entry point; acknowledges APIC delivery after callback completion. */
void device_irq_dispatch(uint64_t vector);

#endif
