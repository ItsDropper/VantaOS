/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "amd_polaris_vm_hw.h"

#define REG_VM_CONTEXT1_CNTL                 0x1414U
#define REG_VM_CONTEXT1_CNTL2                0x1434U
#define REG_VM_INVALIDATE_REQUEST            0x1478U
#define REG_VM_INVALIDATE_RESPONSE           0x147CU
#define REG_VM_CONTEXT1_FAULT_DEFAULT_ADDR   0x151CU
#define REG_VM_CONTEXT1_PAGE_TABLE_BASE_ADDR 0x1540U
#define REG_VM_CONTEXT1_PAGE_TABLE_START     0x1560U
#define REG_VM_CONTEXT1_PAGE_TABLE_END       0x1580U

#define VM_CONTEXT_ENABLE                    (1U << 0)
#define VM_CONTEXT_PAGE_TABLE_DEPTH_MASK     (3U << 1)
#define VM_CONTEXT_PAGE_TABLE_DEPTH_2_LEVEL  (1U << 1)
#define VM_CONTEXT_RANGE_FAULT_DEFAULT       (1U << 4)
#define VM_CONTEXT_WAIT_IDLE_ON_INVALIDATE   (1U << 4)
#define VM_CONTEXT1_INVALIDATE_BIT           (1U << AMD_POLARIS_VM_CONTEXT1_ID)
#define VM_PAGE_NUMBER_MASK                  0x0FFFFFFFU
#define VM_PHYSICAL_ADDRESS_MAX              0xFFFFFFFFFFULL

static int read32(const gpu_mmio_region_t *mmio, uint32_t offset,
                  uint32_t *value)
{
    return gpu_mmio_read32(mmio, offset, value) == GPU_MMIO_OK;
}

static int write32(const gpu_mmio_region_t *mmio, uint32_t offset,
                   uint32_t value)
{
    return gpu_mmio_write32(mmio, offset, value) == GPU_MMIO_OK;
}

amd_polaris_vm_hw_status_t amd_polaris_vm_invalidate(
    const gpu_mmio_region_t *mmio, unsigned timeout_iterations)
{
    uint32_t response = 0U;
    if (mmio == 0 || timeout_iterations == 0U ||
        mmio->length < REG_VM_INVALIDATE_RESPONSE + sizeof(uint32_t))
        return AMD_POLARIS_VM_HW_INVALID_ARGUMENT;

    if (!write32(mmio, REG_VM_INVALIDATE_REQUEST, VM_CONTEXT1_INVALIDATE_BIT))
        return AMD_POLARIS_VM_HW_MMIO_ERROR;

    for (unsigned i = 0U; i < timeout_iterations; ++i) {
        if (!read32(mmio, REG_VM_INVALIDATE_RESPONSE, &response))
            return AMD_POLARIS_VM_HW_MMIO_ERROR;
        if ((response & VM_CONTEXT1_INVALIDATE_BIT) != 0U)
            return AMD_POLARIS_VM_HW_OK;
    }
    return AMD_POLARIS_VM_HW_INVALIDATE_TIMEOUT;
}

amd_polaris_vm_hw_status_t amd_polaris_vm_install_root(
    const gpu_mmio_region_t *mmio, uint64_t root_physical,
    uint64_t va_start, uint64_t va_size, uint64_t fault_page_physical,
    unsigned timeout_iterations, int device_quiesced)
{
    uint64_t va_end;
    uint32_t root_pfn, start_pfn, end_pfn, fault_pfn;
    uint32_t control, observed;

    if (mmio == 0 || timeout_iterations == 0U ||
        mmio->length < REG_VM_CONTEXT1_PAGE_TABLE_END + sizeof(uint32_t))
        return AMD_POLARIS_VM_HW_INVALID_ARGUMENT;
    if (!device_quiesced)
        return AMD_POLARIS_VM_HW_DEVICE_BUSY;
    if ((root_physical & 0xFFFU) != 0U ||
        (fault_page_physical & 0xFFFU) != 0U || va_size == 0U ||
        (va_start & 0xFFFU) != 0U ||
        va_start > UINT64_MAX - va_size)
        return AMD_POLARIS_VM_HW_INVALID_ADDRESS;
    va_end = va_start + va_size;
    if (va_start >= (1ULL << AMD_POLARIS_VM_VA_BITS) ||
        va_end > (1ULL << AMD_POLARIS_VM_VA_BITS) ||
        root_physical > VM_PHYSICAL_ADDRESS_MAX ||
        fault_page_physical > VM_PHYSICAL_ADDRESS_MAX)
        return AMD_POLARIS_VM_HW_INVALID_ADDRESS;

    root_pfn = (uint32_t)(root_physical >> 12U);
    start_pfn = (uint32_t)(va_start >> 12U);
    end_pfn = (uint32_t)((va_end - 1U) >> 12U);
    fault_pfn = (uint32_t)(fault_page_physical >> 12U);
    if (root_pfn > VM_PAGE_NUMBER_MASK || start_pfn > VM_PAGE_NUMBER_MASK ||
        end_pfn > VM_PAGE_NUMBER_MASK || fault_pfn > VM_PAGE_NUMBER_MASK)
        return AMD_POLARIS_VM_HW_INVALID_ADDRESS;

    if (!read32(mmio, REG_VM_CONTEXT1_CNTL, &control))
        return AMD_POLARIS_VM_HW_MMIO_ERROR;
    if ((control & VM_CONTEXT_ENABLE) != 0U) {
        control &= ~VM_CONTEXT_ENABLE;
        if (!write32(mmio, REG_VM_CONTEXT1_CNTL, control) ||
            !read32(mmio, REG_VM_CONTEXT1_CNTL, &observed) ||
            (observed & VM_CONTEXT_ENABLE) != 0U)
            return AMD_POLARIS_VM_HW_VERIFY_FAILED;
    }

    if (!write32(mmio, REG_VM_CONTEXT1_PAGE_TABLE_START, start_pfn) ||
        !write32(mmio, REG_VM_CONTEXT1_PAGE_TABLE_END, end_pfn) ||
        !write32(mmio, REG_VM_CONTEXT1_PAGE_TABLE_BASE_ADDR, root_pfn) ||
        !write32(mmio, REG_VM_CONTEXT1_FAULT_DEFAULT_ADDR, fault_pfn) ||
        !write32(mmio, REG_VM_CONTEXT1_CNTL2,
                 VM_CONTEXT_WAIT_IDLE_ON_INVALIDATE))
        return AMD_POLARIS_VM_HW_MMIO_ERROR;

    control = (control & ~VM_CONTEXT_PAGE_TABLE_DEPTH_MASK) |
              VM_CONTEXT_PAGE_TABLE_DEPTH_2_LEVEL |
              VM_CONTEXT_RANGE_FAULT_DEFAULT | VM_CONTEXT_ENABLE;
    if (!write32(mmio, REG_VM_CONTEXT1_CNTL, control) ||
        !read32(mmio, REG_VM_CONTEXT1_CNTL, &observed) ||
        (observed & (VM_CONTEXT_ENABLE | VM_CONTEXT_PAGE_TABLE_DEPTH_MASK |
                     VM_CONTEXT_RANGE_FAULT_DEFAULT)) !=
            (VM_CONTEXT_ENABLE | VM_CONTEXT_PAGE_TABLE_DEPTH_2_LEVEL |
             VM_CONTEXT_RANGE_FAULT_DEFAULT))
        return AMD_POLARIS_VM_HW_VERIFY_FAILED;

    if (!read32(mmio, REG_VM_CONTEXT1_PAGE_TABLE_BASE_ADDR, &observed) ||
        observed != root_pfn ||
        !read32(mmio, REG_VM_CONTEXT1_PAGE_TABLE_START, &observed) ||
        observed != start_pfn ||
        !read32(mmio, REG_VM_CONTEXT1_PAGE_TABLE_END, &observed) ||
        observed != end_pfn ||
        !read32(mmio, REG_VM_CONTEXT1_FAULT_DEFAULT_ADDR, &observed) ||
        observed != fault_pfn)
        return AMD_POLARIS_VM_HW_VERIFY_FAILED;

    return amd_polaris_vm_invalidate(mmio, timeout_iterations);
}
