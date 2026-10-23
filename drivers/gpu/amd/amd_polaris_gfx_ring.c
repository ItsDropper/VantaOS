/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "amd_polaris_gfx_ring.h"

#include <string.h>

#define REG_CP_ME_CNTL             (0x21B6U * 4U)
#define REG_CP_RB0_RPTR            (0x21C0U * 4U)
#define REG_CP_RB_WPTR_DELAY       (0x21C1U * 4U)
#define REG_CP_RB_WPTR_POLL_CNTL   (0x21C2U * 4U)
#define REG_CP_RB0_BASE            (0x3040U * 4U)
#define REG_CP_RB0_BASE_HI         (0x30B1U * 4U)
#define REG_CP_RB0_CNTL            (0x3041U * 4U)
#define REG_CP_RB0_RPTR_ADDR       (0x3043U * 4U)
#define REG_CP_RB0_RPTR_ADDR_HI    (0x3044U * 4U)
#define REG_CP_RB0_WPTR            (0x3045U * 4U)
#define REG_CP_RB_WPTR_POLL_ADDR_LO (0x3046U * 4U)
#define REG_CP_RB_WPTR_POLL_ADDR_HI (0x3047U * 4U)
#define REG_CP_RB_VMID             (0x3051U * 4U)

#define CP_ME_HALT_MASK (0x10000000U | 0x04000000U | 0x01000000U)
#define CP_RB_BUFSZ_MASK 0x0000003FU
#define CP_RB_BLKSZ_MASK 0x00003F00U
#define CP_RB_MTYPE_MASK 0x00018000U
#define CP_RB_MIN_IB_AVAILSZ_MASK 0x00C00000U
#define CP_RB_RPTR_WRITE_ENABLE 0x80000000U
#define CP_RB_MTYPE_COHERENT 3U
#define CP_RB_MIN_IB_AVAILSZ_1 1U
#define AMD_POLARIS_GPU_ADDRESS_MAX ((1ULL << 40U) - 1U)

static int read32(const gpu_mmio_region_t *mmio, size_t offset,
                  uint32_t *value)
{
    return gpu_mmio_read32(mmio, offset, value) == GPU_MMIO_OK;
}

static int write32(const gpu_mmio_region_t *mmio, size_t offset,
                   uint32_t value)
{
    return gpu_mmio_write32(mmio, offset, value) == GPU_MMIO_OK;
}

static int readback(const gpu_mmio_region_t *mmio, size_t offset,
                    uint32_t expected)
{
    uint32_t observed;
    return read32(mmio, offset, &observed) && observed == expected;
}

static int is_power_of_two(size_t value)
{
    return value != 0U && (value & (value - 1U)) == 0U;
}

static unsigned log2_size(size_t value)
{
    unsigned result = 0U;
    while (value > 1U) {
        value >>= 1U;
        ++result;
    }
    return result;
}

static int halt_cp(const gpu_mmio_region_t *mmio)
{
    uint32_t control, observed;
    if (!read32(mmio, REG_CP_ME_CNTL, &control))
        return 0;
    control |= CP_ME_HALT_MASK;
    return write32(mmio, REG_CP_ME_CNTL, control) &&
           read32(mmio, REG_CP_ME_CNTL, &observed) &&
           (observed & CP_ME_HALT_MASK) == CP_ME_HALT_MASK;
}

amd_polaris_gfx_ring_status_t amd_polaris_gfx_ring_init(
    amd_polaris_gfx_ring_t *ring, const gpu_mmio_region_t *mmio,
    volatile uint32_t *ring_storage, uint64_t ring_gpu_address,
    size_t ring_bytes, uint64_t rptr_gpu_address,
    int firmware_ready, int device_quiesced)
{
    uint32_t control, observed;
    unsigned buffer_size_log2;
    uint64_t ring_gpu_shifted;

    if (ring == NULL || mmio == NULL || mmio->base == NULL ||
        ring_storage == NULL || ring->initialized != 0U ||
        ring_bytes < AMD_POLARIS_GFX_RING_MIN_BYTES ||
        ring_bytes > AMD_POLARIS_GFX_RING_MAX_BYTES ||
        !is_power_of_two(ring_bytes) || (ring_bytes & 7U) != 0U ||
        (ring_gpu_address & 0xFFU) != 0U ||
        (rptr_gpu_address & 3U) != 0U ||
        ring_gpu_address > AMD_POLARIS_GPU_ADDRESS_MAX ||
        rptr_gpu_address > AMD_POLARIS_GPU_ADDRESS_MAX ||
        ring_gpu_address > AMD_POLARIS_GPU_ADDRESS_MAX - (ring_bytes - 1U) ||
        mmio->length < REG_CP_RB0_BASE_HI + sizeof(uint32_t))
        return AMD_POLARIS_GFX_RING_INVALID_ARGUMENT;
    if (!device_quiesced)
        return AMD_POLARIS_GFX_RING_NOT_QUIESCED;
    if (!firmware_ready)
        return AMD_POLARIS_GFX_RING_FIRMWARE_NOT_READY;

    buffer_size_log2 = log2_size(ring_bytes / 8U);
    if (buffer_size_log2 < 2U || buffer_size_log2 > CP_RB_BUFSZ_MASK ||
        buffer_size_log2 - 2U > (CP_RB_BLKSZ_MASK >> 8U))
        return AMD_POLARIS_GFX_RING_INVALID_ARGUMENT;

    memset(ring, 0, sizeof(*ring));
    ring->mmio = mmio;
    ring->ring_storage = ring_storage;
    ring->ring_bytes = ring_bytes;
    ring->ring_gpu_address = ring_gpu_address;

    if (!halt_cp(mmio))
        goto mmio_fail;

    /* GFX8 ring format: size fields encode log2(dwords); base is in 256-byte units. */
    control = (buffer_size_log2 & CP_RB_BUFSZ_MASK) |
              ((buffer_size_log2 - 2U) << 8U) |
              (CP_RB_MTYPE_COHERENT << 15U) |
              (CP_RB_MIN_IB_AVAILSZ_1 << 22U);
    if (!write32(mmio, REG_CP_RB0_CNTL, control | CP_RB_RPTR_WRITE_ENABLE) ||
        !readback(mmio, REG_CP_RB0_CNTL,
                  control | CP_RB_RPTR_WRITE_ENABLE) ||
        !write32(mmio, REG_CP_RB0_WPTR, 0U) ||
        !write32(mmio, REG_CP_RB_WPTR_DELAY, 0U) ||
        !write32(mmio, REG_CP_RB_VMID, 0U) ||
        !write32(mmio, REG_CP_RB0_RPTR_ADDR, (uint32_t)rptr_gpu_address) ||
        !write32(mmio, REG_CP_RB0_RPTR_ADDR_HI,
                 (uint32_t)(rptr_gpu_address >> 32U) & 0xFFFFU) ||
        !write32(mmio, REG_CP_RB_WPTR_POLL_CNTL, 0U) ||
        !write32(mmio, REG_CP_RB0_BASE, (uint32_t)(ring_gpu_address >> 8U)) ||
        !write32(mmio, REG_CP_RB0_BASE_HI,
                 (uint32_t)(ring_gpu_address >> 40U)) ||
        !readback(mmio, REG_CP_RB0_BASE,
                  (uint32_t)(ring_gpu_address >> 8U)) ||
        !readback(mmio, REG_CP_RB0_BASE_HI,
                  (uint32_t)(ring_gpu_address >> 40U)))
        goto mmio_fail;

    /* Remove the temporary read-pointer write enable, then start the CP. */
    if (!write32(mmio, REG_CP_RB0_CNTL, control) ||
        !readback(mmio, REG_CP_RB0_CNTL, control))
        goto mmio_fail;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    if (!read32(mmio, REG_CP_ME_CNTL, &observed) ||
        !write32(mmio, REG_CP_ME_CNTL, observed & ~CP_ME_HALT_MASK) ||
        !read32(mmio, REG_CP_ME_CNTL, &observed) ||
        (observed & CP_ME_HALT_MASK) != 0U)
        goto mmio_fail;

    ring->initialized = 1U;
    return AMD_POLARIS_GFX_RING_OK;

mmio_fail:
    ring->faulted = 1U;
    (void)halt_cp(mmio);
    return AMD_POLARIS_GFX_RING_MMIO_ERROR;
}

amd_polaris_gfx_ring_status_t amd_polaris_gfx_ring_submit_fence(
    amd_polaris_gfx_ring_t *ring, const uint32_t *packet, size_t packet_dwords,
    uint64_t allowed_fence_gpu_address, uint32_t expected_value)
{
    uint32_t read_pointer, write_pointer, next_pointer;
    size_t capacity_dwords, free_dwords;
    amd_polaris_pm4_status_t packet_status;

    if (ring == NULL || ring->initialized == 0U || ring->mmio == NULL ||
        ring->ring_storage == NULL || packet == NULL)
        return AMD_POLARIS_GFX_RING_INVALID_ARGUMENT;
    if (ring->faulted != 0U)
        return AMD_POLARIS_GFX_RING_FAULTED;
    packet_status = amd_polaris_validate_fence_packet(
        packet, packet_dwords, allowed_fence_gpu_address, expected_value);
    if (packet_status != AMD_POLARIS_PM4_OK ||
        packet_dwords != AMD_POLARIS_GFX_RING_FENCE_DWORDS)
        return AMD_POLARIS_GFX_RING_PACKET_REJECTED;
    if (!read32(ring->mmio, REG_CP_RB0_RPTR, &read_pointer))
        return AMD_POLARIS_GFX_RING_MMIO_ERROR;

    capacity_dwords = ring->ring_bytes / sizeof(uint32_t);
    write_pointer = ring->write_pointer;
    read_pointer %= (uint32_t)capacity_dwords;
    free_dwords = (size_t)((read_pointer - write_pointer - 1U) &
                           ((uint32_t)capacity_dwords - 1U));
    if (free_dwords < packet_dwords)
        return AMD_POLARIS_GFX_RING_BUSY;

    for (size_t i = 0U; i < packet_dwords; ++i)
        ring->ring_storage[(write_pointer + (uint32_t)i) &
                           ((uint32_t)capacity_dwords - 1U)] = packet[i];
    __atomic_thread_fence(__ATOMIC_RELEASE);
    next_pointer = (write_pointer + (uint32_t)packet_dwords) &
                   ((uint32_t)capacity_dwords - 1U);
    if (!write32(ring->mmio, REG_CP_RB0_WPTR, next_pointer) ||
        !readback(ring->mmio, REG_CP_RB0_WPTR, next_pointer)) {
        ring->faulted = 1U;
        return AMD_POLARIS_GFX_RING_MMIO_ERROR;
    }
    ring->write_pointer = next_pointer;
    return AMD_POLARIS_GFX_RING_OK;
}

amd_polaris_gfx_ring_status_t amd_polaris_gfx_ring_wait_fence(
    amd_polaris_gfx_ring_t *ring, const volatile uint32_t *fence_cpu_address,
    uint32_t expected_value, unsigned timeout_iterations)
{
    if (ring == NULL || ring->initialized == 0U ||
        fence_cpu_address == NULL || timeout_iterations == 0U)
        return AMD_POLARIS_GFX_RING_INVALID_ARGUMENT;
    if (ring->faulted != 0U)
        return AMD_POLARIS_GFX_RING_FAULTED;
    for (unsigned i = 0U; i < timeout_iterations; ++i) {
        uint32_t observed = *fence_cpu_address;
        if (observed == expected_value) {
            __atomic_thread_fence(__ATOMIC_ACQUIRE);
            return AMD_POLARIS_GFX_RING_OK;
        }
    }
    ring->faulted = 1U;
    return AMD_POLARIS_GFX_RING_FENCE_TIMEOUT;
}

amd_polaris_gfx_ring_status_t amd_polaris_gfx_ring_shutdown(
    amd_polaris_gfx_ring_t *ring, int device_quiesced)
{
    if (ring == NULL || ring->initialized == 0U || ring->mmio == NULL)
        return AMD_POLARIS_GFX_RING_INVALID_ARGUMENT;
    if (!device_quiesced)
        return AMD_POLARIS_GFX_RING_NOT_QUIESCED;
    if (!halt_cp(ring->mmio))
        return AMD_POLARIS_GFX_RING_MMIO_ERROR;
    memset(ring, 0, sizeof(*ring));
    return AMD_POLARIS_GFX_RING_OK;
}
