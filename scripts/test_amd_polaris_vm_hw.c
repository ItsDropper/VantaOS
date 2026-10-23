/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "amd_polaris_vm_hw.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#define MOCK_MMIO_BYTES 0x2000U
#define REG_INVALIDATE_REQUEST 0x1478U
#define REG_INVALIDATE_RESPONSE 0x147CU
#define REG_CONTEXT1_CNTL 0x1414U
#define REG_CONTEXT1_BASE 0x1540U
#define REG_CONTEXT1_START 0x1560U
#define REG_CONTEXT1_END 0x1580U
#define REG_CONTEXT1_FAULT 0x151CU

static uint32_t registers[MOCK_MMIO_BYTES / sizeof(uint32_t)];

static gpu_mmio_region_t region(size_t length)
{
    gpu_mmio_region_t mmio;
    mmio.base = (volatile uint8_t *)registers;
    mmio.length = length;
    return mmio;
}

static uint32_t read_reg(uint32_t offset)
{
    return registers[offset / sizeof(uint32_t)];
}

static void test_install_root(void)
{
    gpu_mmio_region_t mmio = region(MOCK_MMIO_BYTES);
    memset(registers, 0, sizeof(registers));
    registers[REG_INVALIDATE_RESPONSE / 4U] = 1U << AMD_POLARIS_VM_CONTEXT1_ID;

    assert(amd_polaris_vm_install_root(&mmio, 0x200000ULL, 0U,
               AMD_POLARIS_VM_DEFAULT_RANGE_SIZE, 0x300000ULL, 4U, 0) ==
           AMD_POLARIS_VM_HW_DEVICE_BUSY);
    assert(read_reg(REG_CONTEXT1_BASE) == 0U);

    assert(amd_polaris_vm_install_root(&mmio, 0x200001ULL, 0U,
               AMD_POLARIS_VM_DEFAULT_RANGE_SIZE, 0x300000ULL, 4U, 1) ==
           AMD_POLARIS_VM_HW_INVALID_ADDRESS);
    assert(amd_polaris_vm_install_root(&mmio, 0x200000ULL, 0U,
               AMD_POLARIS_VM_DEFAULT_RANGE_SIZE, 0x300000ULL, 4U, 1) ==
           AMD_POLARIS_VM_HW_OK);

    assert(read_reg(REG_CONTEXT1_BASE) == 0x200U);
    assert(read_reg(REG_CONTEXT1_START) == 0U);
    assert(read_reg(REG_CONTEXT1_END) == 0xFFFFFFU);
    assert(read_reg(REG_CONTEXT1_FAULT) == 0x300U);
    assert((read_reg(REG_CONTEXT1_CNTL) & 0x13U) == 0x13U);
    assert(read_reg(REG_INVALIDATE_REQUEST) ==
           (1U << AMD_POLARIS_VM_CONTEXT1_ID));
}

static void test_invalidate_bounds_and_timeout(void)
{
    gpu_mmio_region_t mmio = region(MOCK_MMIO_BYTES);
    memset(registers, 0, sizeof(registers));
    assert(amd_polaris_vm_invalidate(&mmio, 3U) ==
           AMD_POLARIS_VM_HW_INVALIDATE_TIMEOUT);
    registers[REG_INVALIDATE_RESPONSE / 4U] =
        1U << AMD_POLARIS_VM_CONTEXT1_ID;
    assert(amd_polaris_vm_invalidate(&mmio, 3U) == AMD_POLARIS_VM_HW_OK);

    mmio.length = REG_INVALIDATE_RESPONSE;
    assert(amd_polaris_vm_invalidate(&mmio, 3U) ==
           AMD_POLARIS_VM_HW_INVALID_ARGUMENT);
    assert(amd_polaris_vm_invalidate(&mmio, 0U) ==
           AMD_POLARIS_VM_HW_INVALID_ARGUMENT);
}

int main(void)
{
    test_install_root();
    test_invalidate_bounds_and_timeout();
    return 0;
}
