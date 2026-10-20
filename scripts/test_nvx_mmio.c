/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_mmio.h"

#include <assert.h>
#include <stdint.h>

int main(void)
{
    volatile uint32_t registers[3] = { 0x11223344U, 0U, 0xAABBCCDDU };
    nvx_mmio_region_t region = {
        .base = (volatile uint8_t *)(void *)registers,
        .length = sizeof(registers)
    };
    uint32_t value = 0U;

    assert(nvx_mmio_read32(&region, 0U, &value) == NVX_MMIO_OK);
    assert(value == 0x11223344U);
    assert(nvx_mmio_read32(&region, 8U, &value) == NVX_MMIO_OK);
    assert(value == 0xAABBCCDDU);
    assert(nvx_mmio_write32(&region, 4U, 0x55667788U) == NVX_MMIO_OK);
    assert(registers[1] == 0x55667788U);

    value = 0xFFFFFFFFU;
    assert(nvx_mmio_read32(&region, 10U, &value) == NVX_MMIO_UNALIGNED);
    assert(value == 0U);
    assert(nvx_mmio_read32(&region, 12U, &value) == NVX_MMIO_OUT_OF_RANGE);
    assert(nvx_mmio_write32(&region, 12U, 1U) == NVX_MMIO_OUT_OF_RANGE);
    assert(nvx_mmio_read32(&region, sizeof(registers), &value)
           == NVX_MMIO_OUT_OF_RANGE);
    assert(nvx_mmio_read32(&region, 0U, 0) == NVX_MMIO_INVALID_ARGUMENT);
    assert(nvx_mmio_write32(0, 0U, 1U) == NVX_MMIO_INVALID_ARGUMENT);

    region.base = (volatile uint8_t *)(void *)((uintptr_t)registers + 1U);
    region.length = sizeof(registers) - 1U;
    assert(nvx_mmio_read32(&region, 0U, &value) == NVX_MMIO_UNALIGNED);
    assert(nvx_mmio_write32(&region, 0U, 1U) == NVX_MMIO_UNALIGNED);

    region.base = 0;
    assert(nvx_mmio_read32(&region, 0U, &value) == NVX_MMIO_INVALID_ARGUMENT);
    return 0;
}
