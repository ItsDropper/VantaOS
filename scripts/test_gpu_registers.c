/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "gpu_registers.h"

#include <assert.h>
#include <stdint.h>

int main(void)
{
    volatile uint32_t regs[8] = {
        0xA5A50000U, 0x00001234U, 0U, 0U, 0U, 0x00000004U, 0U, 0U
    };
    gpu_mmio_region_t mmio = {
        .base = (volatile uint8_t *)(void *)regs,
        .length = sizeof(regs)
    };
    uint32_t value = 0U, encoded = 0U;
    size_t failed = 0U;
    const gpu_register_write_t seq[] = {
        { .offset = 8U, .value = 0x00001234U, .verify_mask = 0x0000FFFFU },
        { .offset = 12U, .value = 0x00005678U, .verify_mask = 0x0000FFFFU }
    };

    assert(gpu_register_read(&mmio, 0U, &value) == GPU_REGISTER_OK);
    assert(value == 0xA5A50000U);
    assert(gpu_register_update(&mmio, 0U, 0x00FF0000U, 0x00001200U,
                               &value) == GPU_REGISTER_OK);
    assert(value == 0xA5001200U);
    assert(gpu_register_update(&mmio, 0U, 0x00000001U, 0x00000001U,
                               NULL) == GPU_REGISTER_INVALID);
    assert(gpu_register_poll(&mmio, 4U, 0xFFFFU, 0x1234U, 1U, &value)
           == GPU_REGISTER_OK);
    assert(gpu_register_poll(&mmio, 4U, 0xFFFFU, 0xFFFFU, 2U, &value)
           == GPU_REGISTER_TIMEOUT);
    assert(value == 0x1234U);
    assert(gpu_register_poll(&mmio, 4U, 0U, 0U, 1U, NULL)
           == GPU_REGISTER_INVALID);
    assert(gpu_register_field_encode((gpu_register_field_t){0x0000FF00U, 8U},
                                     0x5AU, &encoded) == GPU_REGISTER_OK);
    assert(encoded == 0x00005A00U);
    assert(gpu_register_field_decode((gpu_register_field_t){0x0000FF00U, 8U},
                                     0xABCD1200U) == 0x12U);
    assert(gpu_register_field_encode((gpu_register_field_t){0x0000000FU, 0U},
                                     16U, &encoded) == GPU_REGISTER_INVALID);
    assert(gpu_register_field_encode((gpu_register_field_t){0x00000005U, 0U},
                                     1U, &encoded) == GPU_REGISTER_INVALID);
    assert(gpu_register_field_encode((gpu_register_field_t){0x00000002U, 0U},
                                     1U, &encoded) == GPU_REGISTER_INVALID);
    assert(gpu_register_apply_sequence(&mmio, seq, 2U, 2U, &failed)
           == GPU_REGISTER_OK);
    assert(failed == SIZE_MAX);
    assert(gpu_register_apply_sequence(&mmio, seq, 2U, 2U, &failed)
           == GPU_REGISTER_OK);
    assert(gpu_register_apply_sequence(&mmio, NULL, 1U, 1U, &failed)
           == GPU_REGISTER_INVALID);
    assert(failed == SIZE_MAX);
    return 0;
}
