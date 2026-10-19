/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "device_irq.h"

#include "apic.h"
#include "idt.h"
#include "spinlock.h"

#include <stddef.h>
#include <stdint.h>

typedef struct device_irq_slot {
    device_irq_handler_t handler;
    void *context;
    uint32_t in_flight;
    uint8_t claimed;
    uint8_t releasing;
} device_irq_slot_t;

static device_irq_slot_t slots[DEVICE_IRQ_VECTOR_COUNT];
static spinlock_t slots_lock = SPINLOCK_INITIALIZER;
static uint8_t initialized;

static uint64_t device_irq_lock(void)
{
#ifdef DEVICE_IRQ_TEST
    spinlock_lock(&slots_lock);
    return 0U;
#else
    return spinlock_lock_irqsave(&slots_lock);
#endif
}

static void device_irq_unlock(uint64_t flags)
{
#ifdef DEVICE_IRQ_TEST
    (void)flags;
    spinlock_unlock(&slots_lock);
#else
    spinlock_unlock_irqrestore(&slots_lock, flags);
#endif
}

extern void (*device_irq_stub_table[DEVICE_IRQ_VECTOR_COUNT])(void);

static int device_irq_slot_index(uint8_t vector, size_t *out_index)
{
    if (out_index == 0 || vector < DEVICE_IRQ_VECTOR_FIRST ||
        vector > DEVICE_IRQ_VECTOR_LAST || vector == DEVICE_IRQ_VECTOR_RESERVED)
        return 0;
    *out_index = (size_t)(vector - DEVICE_IRQ_VECTOR_FIRST);
    return 1;
}

int device_irq_initialize(void)
{
    uint64_t flags = device_irq_lock();
    if (initialized) {
        device_irq_unlock(flags);
        return 0;
    }

    for (size_t i = 0; i < DEVICE_IRQ_VECTOR_COUNT; ++i) {
        slots[i].handler = 0;
        slots[i].context = 0;
        slots[i].in_flight = 0U;
        slots[i].claimed = 0U;
        slots[i].releasing = 0U;
    }

    for (unsigned vector = DEVICE_IRQ_VECTOR_FIRST;
         vector <= DEVICE_IRQ_VECTOR_LAST; ++vector) {
        if (vector == DEVICE_IRQ_VECTOR_RESERVED)
            continue;
        idt_set_gate((int)vector,
                     (uint64_t)(uintptr_t)device_irq_stub_table[
                         vector - DEVICE_IRQ_VECTOR_FIRST],
                     0x08U, 0x8EU);
    }
    initialized = 1U;
    device_irq_unlock(flags);
    return 1;
}

int device_irq_vector_claim(uint8_t *out_vector)
{
    if (out_vector != 0)
        *out_vector = 0U;
    if (out_vector == 0 || !initialized || !apic_is_enabled())
        return 0;

    uint64_t flags = device_irq_lock();
    for (unsigned vector = DEVICE_IRQ_VECTOR_FIRST;
         vector <= DEVICE_IRQ_VECTOR_LAST; ++vector) {
        size_t index;
        if (!device_irq_slot_index((uint8_t)vector, &index))
            continue;
        if (!slots[index].claimed && !slots[index].releasing) {
            slots[index].claimed = 1U;
            *out_vector = (uint8_t)vector;
            device_irq_unlock(flags);
            return 1;
        }
    }
    device_irq_unlock(flags);
    return 0;
}

int device_irq_vector_register(uint8_t vector, device_irq_handler_t handler,
                               void *context)
{
    size_t index;
    if (!initialized || handler == 0 ||
        !device_irq_slot_index(vector, &index))
        return 0;

    uint64_t flags = device_irq_lock();
    device_irq_slot_t *slot = &slots[index];
    if (!slot->claimed || slot->releasing ||
        __atomic_load_n(&slot->handler, __ATOMIC_ACQUIRE) != 0) {
        device_irq_unlock(flags);
        return 0;
    }
    __atomic_store_n(&slot->context, context, __ATOMIC_RELAXED);
    __atomic_store_n(&slot->handler, handler, __ATOMIC_RELEASE);
    device_irq_unlock(flags);
    return 1;
}

int device_irq_vector_release(uint8_t vector)
{
    size_t index;
    if (!initialized || !device_irq_slot_index(vector, &index))
        return 0;

    uint64_t flags = device_irq_lock();
    device_irq_slot_t *slot = &slots[index];
    if (!slot->claimed || slot->releasing) {
        device_irq_unlock(flags);
        return 0;
    }
    slot->releasing = 1U;
    __atomic_store_n(&slot->handler, (device_irq_handler_t)0, __ATOMIC_RELEASE);
    device_irq_unlock(flags);

    while (__atomic_load_n(&slot->in_flight, __ATOMIC_ACQUIRE) != 0U)
        __asm__ volatile ("pause" ::: "memory");

    flags = device_irq_lock();
    __atomic_store_n(&slot->context, (void *)0, __ATOMIC_RELAXED);
    slot->claimed = 0U;
    slot->releasing = 0U;
    device_irq_unlock(flags);
    return 1;
}

void device_irq_dispatch(uint64_t raw_vector)
{
    if (raw_vector >= DEVICE_IRQ_VECTOR_FIRST &&
        raw_vector <= DEVICE_IRQ_VECTOR_LAST &&
        raw_vector != DEVICE_IRQ_VECTOR_RESERVED) {
        size_t index = (size_t)(raw_vector - DEVICE_IRQ_VECTOR_FIRST);
        device_irq_slot_t *slot = &slots[index];
        device_irq_handler_t handler;
        void *context;

        /*
         * Increment before loading handler: release clears handler then drains
         * in_flight, so context cannot be reclaimed under an active callback.
         */
        __atomic_add_fetch(&slot->in_flight, 1U, __ATOMIC_ACQUIRE);
        handler = __atomic_load_n(&slot->handler, __ATOMIC_ACQUIRE);
        context = __atomic_load_n(&slot->context, __ATOMIC_RELAXED);
        if (handler != 0)
            handler((uint8_t)raw_vector, context);
        __atomic_sub_fetch(&slot->in_flight, 1U, __ATOMIC_RELEASE);
    }

    if (apic_is_enabled())
        apic_send_eoi();
}
