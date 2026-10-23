/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "intel_gen9_ggtt.h"
#include "intel_gen9_pte.h"

#define REG_FORCEWAKE_RENDER_GEN9 0x0000A278U
#define REG_FORCEWAKE_ACK_RENDER  0x00000D84U
#define FORCEWAKE_KERNEL          (1U << 0)
#define GFX_FLSH_CNTL_ENABLE       (1U << 0)

static int valid_aperture(const gpu_mmio_region_t *mmio)
{
    return mmio != 0 && mmio->base != 0 &&
           mmio->length >= INTEL_GEN9_GTTMMADR_SIZE;
}

static intel_gen9_ggtt_status_t ggtt_pte_offset(
    const gpu_mmio_region_t *mmio, uint64_t ggtt_address, size_t *out_offset)
{
    uint64_t index;
    uint64_t offset;

    if (!valid_aperture(mmio) || out_offset == 0)
        return INTEL_GEN9_GGTT_INVALID_ARGUMENT;
    if ((ggtt_address & 0xFFFU) != 0U)
        return INTEL_GEN9_GGTT_UNALIGNED;
    if (ggtt_address >= ((uint64_t)INTEL_GEN9_GGTT_PTE_COUNT << 12U))
        return INTEL_GEN9_GGTT_OUT_OF_RANGE;

    index = ggtt_address >> 12U;
    offset = (uint64_t)INTEL_GEN9_GGTT_PTE_BASE + index * sizeof(uint64_t);
    if (offset > SIZE_MAX || offset > mmio->length ||
        sizeof(uint64_t) > mmio->length - (size_t)offset)
        return INTEL_GEN9_GGTT_OUT_OF_RANGE;
    *out_offset = (size_t)offset;
    return INTEL_GEN9_GGTT_OK;
}

intel_gen9_ggtt_status_t intel_gen9_ggtt_map_page(
    const gpu_mmio_region_t *gttmmadr, uint64_t ggtt_address,
    uint64_t physical_address)
{
    size_t offset;
    uint64_t pte, observed;
    intel_gen9_ggtt_status_t status =
        ggtt_pte_offset(gttmmadr, ggtt_address, &offset);

    if (status != INTEL_GEN9_GGTT_OK)
        return status;
    if ((physical_address & 0xFFFU) != 0U)
        return INTEL_GEN9_GGTT_UNALIGNED;
    if ((physical_address & ~INTEL_GEN9_PTE_ADDRESS_MASK) != 0U)
        return INTEL_GEN9_GGTT_OUT_OF_RANGE;

    pte = physical_address | INTEL_GEN9_PTE_PRESENT;
    if (gpu_mmio_write64(gttmmadr, offset, pte) != GPU_MMIO_OK ||
        gpu_mmio_read64(gttmmadr, offset, &observed) != GPU_MMIO_OK)
        return INTEL_GEN9_GGTT_MMIO_ERROR;
    return observed == pte ? INTEL_GEN9_GGTT_OK :
                             INTEL_GEN9_GGTT_VERIFY_FAILED;
}

intel_gen9_ggtt_status_t intel_gen9_ggtt_unmap_page(
    const gpu_mmio_region_t *gttmmadr, uint64_t ggtt_address)
{
    size_t offset;
    uint64_t observed;
    intel_gen9_ggtt_status_t status =
        ggtt_pte_offset(gttmmadr, ggtt_address, &offset);

    if (status != INTEL_GEN9_GGTT_OK)
        return status;
    if (gpu_mmio_write64(gttmmadr, offset, 0U) != GPU_MMIO_OK ||
        gpu_mmio_read64(gttmmadr, offset, &observed) != GPU_MMIO_OK)
        return INTEL_GEN9_GGTT_MMIO_ERROR;
    return observed == 0U ? INTEL_GEN9_GGTT_OK :
                            INTEL_GEN9_GGTT_VERIFY_FAILED;
}


intel_gen9_ggtt_status_t intel_gen9_ggtt_map_contiguous(
    const gpu_mmio_region_t *gttmmadr, uint64_t ggtt_address,
    uint64_t physical_address, size_t size,
    const intel_gen9_ggtt_forcewake_ops_t *forcewake)
{
    size_t pages, completed = 0U;
    intel_gen9_ggtt_status_t status;

    if (!valid_aperture(gttmmadr) || forcewake == 0 ||
        forcewake->acquire_render == 0 || forcewake->release_render == 0 ||
        size == 0U || (size & 0xFFFU) != 0U ||
        (ggtt_address & 0xFFFU) != 0U ||
        (physical_address & 0xFFFU) != 0U)
        return INTEL_GEN9_GGTT_INVALID_ARGUMENT;
    if (ggtt_address >= ((uint64_t)INTEL_GEN9_GGTT_PTE_COUNT << 12U) ||
        (uint64_t)size >
            ((uint64_t)INTEL_GEN9_GGTT_PTE_COUNT << 12U) - ggtt_address ||
        physical_address > UINT64_MAX - ((uint64_t)size - 1U) ||
        physical_address + (uint64_t)size - 1U >
            INTEL_GEN9_PTE_ADDRESS_MASK + 0xFFFULL)
        return INTEL_GEN9_GGTT_OUT_OF_RANGE;

    pages = size / 4096U;
    for (size_t i = 0U; i < pages; ++i) {
        status = intel_gen9_ggtt_map_page(
            gttmmadr, ggtt_address + (uint64_t)i * 4096U,
            physical_address + (uint64_t)i * 4096U);
        if (status != INTEL_GEN9_GGTT_OK) {
            int rollback_ok = 1;
            /* Include the failing slot: its write may have reached the device. */
            for (size_t j = i + 1U; j > 0U; --j) {
                if (intel_gen9_ggtt_unmap_page(
                        gttmmadr,
                        ggtt_address + (uint64_t)(j - 1U) * 4096U) !=
                    INTEL_GEN9_GGTT_OK)
                    rollback_ok = 0;
            }
            if (!rollback_ok ||
                intel_gen9_ggtt_invalidate(gttmmadr, forcewake) !=
                    INTEL_GEN9_GGTT_OK)
                return INTEL_GEN9_GGTT_STATE_UNCERTAIN;
            return status;
        }
        ++completed;
    }

    status = intel_gen9_ggtt_invalidate(gttmmadr, forcewake);
    if (status != INTEL_GEN9_GGTT_OK)
        return INTEL_GEN9_GGTT_STATE_UNCERTAIN;
    return completed == pages ? INTEL_GEN9_GGTT_OK :
                                INTEL_GEN9_GGTT_VERIFY_FAILED;
}

intel_gen9_ggtt_status_t intel_gen9_ggtt_unmap_contiguous(
    const gpu_mmio_region_t *gttmmadr, uint64_t ggtt_address,
    size_t size, const intel_gen9_ggtt_forcewake_ops_t *forcewake)
{
    size_t pages;
    if (!valid_aperture(gttmmadr) || forcewake == 0 ||
        forcewake->acquire_render == 0 || forcewake->release_render == 0 ||
        size == 0U || (size & 0xFFFU) != 0U ||
        (ggtt_address & 0xFFFU) != 0U)
        return INTEL_GEN9_GGTT_INVALID_ARGUMENT;
    if (ggtt_address >= ((uint64_t)INTEL_GEN9_GGTT_PTE_COUNT << 12U) ||
        (uint64_t)size >
            ((uint64_t)INTEL_GEN9_GGTT_PTE_COUNT << 12U) - ggtt_address)
        return INTEL_GEN9_GGTT_OUT_OF_RANGE;

    pages = size / 4096U;
    for (size_t i = 0U; i < pages; ++i) {
        intel_gen9_ggtt_status_t status = intel_gen9_ggtt_unmap_page(
            gttmmadr, ggtt_address + (uint64_t)i * 4096U);
        if (status != INTEL_GEN9_GGTT_OK)
            return INTEL_GEN9_GGTT_STATE_UNCERTAIN;
    }
    if (intel_gen9_ggtt_invalidate(gttmmadr, forcewake) != INTEL_GEN9_GGTT_OK)
        return INTEL_GEN9_GGTT_STATE_UNCERTAIN;
    return INTEL_GEN9_GGTT_OK;
}

intel_gen9_ggtt_status_t intel_gen9_ggtt_invalidate(
    const gpu_mmio_region_t *gttmmadr,
    const intel_gen9_ggtt_forcewake_ops_t *forcewake)
{
    uint32_t observed;
    int acquired, write_ok = 0, release_ok;

    if (!valid_aperture(gttmmadr) || forcewake == 0 ||
        forcewake->acquire_render == 0 || forcewake->release_render == 0)
        return INTEL_GEN9_GGTT_INVALID_ARGUMENT;
    acquired = forcewake->acquire_render(forcewake->context);
    if (!acquired)
        return INTEL_GEN9_GGTT_FORCEWAKE_FAILED;

    __atomic_thread_fence(__ATOMIC_RELEASE);
    if (gpu_mmio_write32(gttmmadr, INTEL_GEN9_GFX_FLSH_CNTL,
                         GFX_FLSH_CNTL_ENABLE) == GPU_MMIO_OK &&
        gpu_mmio_read32(gttmmadr, INTEL_GEN9_GFX_FLSH_CNTL,
                        &observed) == GPU_MMIO_OK &&
        (observed & GFX_FLSH_CNTL_ENABLE) != 0U)
        write_ok = 1;

    release_ok = forcewake->release_render(forcewake->context);
    if (!release_ok)
        return INTEL_GEN9_GGTT_FORCEWAKE_FAILED;
    return write_ok ? INTEL_GEN9_GGTT_OK : INTEL_GEN9_GGTT_VERIFY_FAILED;
}

static int forcewake_change(const gpu_mmio_region_t *mmio,
                            unsigned timeout_iterations, int acquire)
{
    uint32_t value = 0U;
    if (!valid_aperture(mmio) || timeout_iterations == 0U)
        return 0;
    if (gpu_mmio_write32(mmio, REG_FORCEWAKE_RENDER_GEN9,
                         acquire ? FORCEWAKE_KERNEL : 0U) != GPU_MMIO_OK)
        return 0;
    for (unsigned i = 0U; i < timeout_iterations; ++i) {
        if (gpu_mmio_read32(mmio, REG_FORCEWAKE_ACK_RENDER, &value) !=
            GPU_MMIO_OK)
            return 0;
        if (((value & FORCEWAKE_KERNEL) != 0U) == (acquire != 0))
            return 1;
    }
    if (acquire) {
        /* A timed-out request must not be left asserted indefinitely. */
        (void)gpu_mmio_write32(mmio, REG_FORCEWAKE_RENDER_GEN9, 0U);
        for (unsigned i = 0U; i < timeout_iterations; ++i) {
            if (gpu_mmio_read32(mmio, REG_FORCEWAKE_ACK_RENDER, &value) !=
                GPU_MMIO_OK)
                break;
            if ((value & FORCEWAKE_KERNEL) == 0U)
                break;
        }
    }
    return 0;
}

int intel_gen9_forcewake_render_acquire(const gpu_mmio_region_t *gttmmadr,
                                        unsigned timeout_iterations)
{
    return forcewake_change(gttmmadr, timeout_iterations, 1);
}

int intel_gen9_forcewake_render_release(const gpu_mmio_region_t *gttmmadr,
                                        unsigned timeout_iterations)
{
    return forcewake_change(gttmmadr, timeout_iterations, 0);
}
