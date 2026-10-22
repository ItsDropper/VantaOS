/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "gpu_mmio.h"

#include <assert.h>
#include <stdint.h>

int main(void)
{
    volatile uint32_t registers[3] = { 0x11223344U, 0U, 0xAABBCCDDU };
    gpu_mmio_region_t region = {
        .base = (volatile uint8_t *)(void *)registers,
        .length = sizeof(registers)
    };
    uint32_t value = 0U;

    assert(gpu_mmio_read32(&region, 0U, &value) == GPU_MMIO_OK);
    assert(value == 0x11223344U);
    assert(gpu_mmio_read32(&region, 8U, &value) == GPU_MMIO_OK);
    assert(value == 0xAABBCCDDU);
    assert(gpu_mmio_write32(&region, 4U, 0x55667788U) == GPU_MMIO_OK);
    assert(registers[1] == 0x55667788U);

    value = UINT32_MAX;
    assert(gpu_mmio_read32(&region, 2U, &value) == GPU_MMIO_UNALIGNED);
    assert(value == 0U);
    assert(gpu_mmio_read32(&region, 12U, &value) == GPU_MMIO_OUT_OF_RANGE);
    assert(gpu_mmio_write32(&region, 12U, 1U) == GPU_MMIO_OUT_OF_RANGE);
    assert(gpu_mmio_read32(&region, sizeof(registers), &value) ==
           GPU_MMIO_OUT_OF_RANGE);
    assert(gpu_mmio_read32(&region, 0U, NULL) == GPU_MMIO_INVALID_ARGUMENT);
    assert(gpu_mmio_write32(NULL, 0U, 1U) == GPU_MMIO_INVALID_ARGUMENT);

    region.base = (volatile uint8_t *)(void *)((uintptr_t)registers + 1U);
    region.length = sizeof(registers) - 1U;
    assert(gpu_mmio_read32(&region, 0U, &value) == GPU_MMIO_UNALIGNED);
    assert(gpu_mmio_write32(&region, 0U, 1U) == GPU_MMIO_UNALIGNED);
    region.base = NULL;
    assert(gpu_mmio_read32(&region, 0U, &value) == GPU_MMIO_INVALID_ARGUMENT);
    return 0;
}
