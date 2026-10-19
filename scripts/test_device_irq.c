/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "device_irq.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static unsigned gate_count;
static unsigned eoi_count;
static unsigned callback_count;
static uint8_t callback_vector;
static void *callback_context;
static int apic_enabled = 1;
static void dummy_stub(void) {}
void (*device_irq_stub_table[DEVICE_IRQ_VECTOR_COUNT])(void);

void idt_set_gate(int number, uint64_t base, uint16_t selector, uint8_t flags)
{
    assert(number >= (int)DEVICE_IRQ_VECTOR_FIRST &&
           number <= (int)DEVICE_IRQ_VECTOR_LAST);
    assert(base != 0U && selector == 0x08U && flags == 0x8EU);
    ++gate_count;
}

int apic_is_enabled(void) { return apic_enabled; }
void apic_send_eoi(void) { ++eoi_count; }

static void test_handler(uint8_t vector, void *context)
{
    ++callback_count;
    callback_vector = vector;
    callback_context = context;
}

int main(void)
{
    uint8_t vectors[DEVICE_IRQ_VECTOR_COUNT] = {0};
    size_t claimed = 0U;
    int context = 42;

    for (size_t i = 0; i < DEVICE_IRQ_VECTOR_COUNT; ++i)
        device_irq_stub_table[i] = dummy_stub;

    assert(!device_irq_vector_claim(&vectors[0]));
    assert(device_irq_initialize());
    assert(!device_irq_initialize());
    assert(gate_count == DEVICE_IRQ_VECTOR_COUNT - 1U);
    apic_enabled = 0;
    assert(!device_irq_vector_claim(&vectors[0]));
    apic_enabled = 1;

    assert(!device_irq_vector_register(0x40U, test_handler, &context));
    assert(!device_irq_vector_release(0x80U));

    while (claimed < DEVICE_IRQ_VECTOR_COUNT) {
        uint8_t vector = 0U;
        if (!device_irq_vector_claim(&vector))
            break;
        assert(vector >= DEVICE_IRQ_VECTOR_FIRST &&
               vector <= DEVICE_IRQ_VECTOR_LAST);
        assert(vector != DEVICE_IRQ_VECTOR_RESERVED);
        for (size_t i = 0; i < claimed; ++i)
            assert(vectors[i] != vector);
        vectors[claimed++] = vector;
    }
    assert(claimed == DEVICE_IRQ_VECTOR_COUNT - 1U);
    assert(!device_irq_vector_claim(&(uint8_t){0}));

    uint8_t test_vector = vectors[0];
    assert(device_irq_vector_register(test_vector, test_handler, &context));
    assert(!device_irq_vector_register(test_vector, test_handler, &context));
    device_irq_dispatch(test_vector);
    assert(callback_count == 1U && callback_vector == test_vector);
    assert(callback_context == &context && eoi_count == 1U);

    assert(device_irq_vector_release(test_vector));
    assert(!device_irq_vector_release(test_vector));
    device_irq_dispatch(test_vector);
    assert(callback_count == 1U && eoi_count == 2U);

    for (size_t i = 1; i < claimed; ++i)
        assert(device_irq_vector_release(vectors[i]));
    assert(device_irq_vector_claim(&(uint8_t){0}));
    return 0;
}
