/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "amd_polaris_gfx_ring.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#define MMIO_BYTES 0xD000U
#define REG_CP_ME_CNTL (0x21B6U * 4U)
#define REG_CP_RB0_RPTR (0x21C0U * 4U)
#define REG_CP_RB0_CNTL (0x3041U * 4U)
#define REG_CP_RB0_BASE (0x3040U * 4U)
#define REG_CP_RB0_BASE_HI (0x30B1U * 4U)
#define REG_CP_RB0_WPTR (0x3045U * 4U)
#define CP_ME_HALT_MASK (0x10000000U | 0x04000000U | 0x01000000U)

static uint32_t registers[MMIO_BYTES / sizeof(uint32_t)];
static uint32_t ring_storage[AMD_POLARIS_GFX_RING_MIN_BYTES / sizeof(uint32_t)];

static gpu_mmio_region_t make_mmio(void)
{
    gpu_mmio_region_t mmio = {
        .base = (volatile uint8_t *)registers,
        .length = sizeof(registers)
    };
    return mmio;
}

static uint32_t reg(size_t offset)
{
    return registers[offset / sizeof(uint32_t)];
}

static void test_prerequisites_and_setup(void)
{
    gpu_mmio_region_t mmio = make_mmio();
    amd_polaris_gfx_ring_t ring = {0};

    memset(registers, 0, sizeof(registers));
    assert(amd_polaris_gfx_ring_init(&ring, &mmio, ring_storage,
        0x400000U, sizeof(ring_storage), 0x500000U, 0, 1) ==
        AMD_POLARIS_GFX_RING_FIRMWARE_NOT_READY);
    assert(ring.initialized == 0U);
    assert(amd_polaris_gfx_ring_init(&ring, &mmio, ring_storage,
        0x400000U, sizeof(ring_storage), 0x500000U, 1, 0) ==
        AMD_POLARIS_GFX_RING_NOT_QUIESCED);
    assert(amd_polaris_gfx_ring_init(&ring, &mmio, ring_storage,
        0x400001U, sizeof(ring_storage), 0x500000U, 1, 1) ==
        AMD_POLARIS_GFX_RING_INVALID_ARGUMENT);

    assert(amd_polaris_gfx_ring_init(&ring, &mmio, ring_storage,
        0x400000U, sizeof(ring_storage), 0x500000U, 1, 1) ==
        AMD_POLARIS_GFX_RING_OK);
    assert(ring.initialized != 0U);
    assert(reg(REG_CP_RB0_BASE) == 0x4000U);
    assert(reg(REG_CP_RB0_BASE_HI) == 0U);
    assert(reg(REG_CP_RB0_WPTR) == 0U);
    assert((reg(REG_CP_RB0_CNTL) & 0x80000000U) == 0U);
    assert((reg(REG_CP_ME_CNTL) & CP_ME_HALT_MASK) == 0U);
    assert(amd_polaris_gfx_ring_shutdown(&ring, 0) ==
        AMD_POLARIS_GFX_RING_NOT_QUIESCED);
    assert(amd_polaris_gfx_ring_shutdown(&ring, 1) ==
        AMD_POLARIS_GFX_RING_OK);
}

static void test_fence_submission_and_timeout(void)
{
    gpu_mmio_region_t mmio = make_mmio();
    amd_polaris_gfx_ring_t ring = {0};
    uint32_t packet[AMD_POLARIS_PM4_FENCE_DWORDS];
    size_t packet_count = 0U;
    volatile uint32_t fence = 0U;

    memset(registers, 0, sizeof(registers));
    memset(ring_storage, 0, sizeof(ring_storage));
    assert(amd_polaris_gfx_ring_init(&ring, &mmio, ring_storage,
        0x400000U, sizeof(ring_storage), 0x500000U, 1, 1) ==
        AMD_POLARIS_GFX_RING_OK);
    assert(amd_polaris_build_fence_packet(packet,
        AMD_POLARIS_PM4_FENCE_DWORDS, 0x600000U, 0x12345678U,
        &packet_count) == AMD_POLARIS_PM4_OK);
    assert(packet_count == AMD_POLARIS_PM4_FENCE_DWORDS);

    /* The packet is rejected if it attempts to write outside the authorized fence. */
    assert(amd_polaris_gfx_ring_submit_fence(&ring, packet, packet_count,
        0x600004U, 0x12345678U) ==
        AMD_POLARIS_GFX_RING_PACKET_REJECTED);

    assert(amd_polaris_gfx_ring_submit_fence(&ring, packet, packet_count,
        0x600000U, 0x12345678U) == AMD_POLARIS_GFX_RING_OK);
    assert(reg(REG_CP_RB0_WPTR) == AMD_POLARIS_PM4_FENCE_DWORDS);
    for (size_t i = 0U; i < packet_count; ++i)
        assert(ring_storage[i] == packet[i]);

    assert(amd_polaris_gfx_ring_wait_fence(&ring, &fence, 0x12345678U, 2U) ==
        AMD_POLARIS_GFX_RING_FENCE_TIMEOUT);
    assert(amd_polaris_gfx_ring_submit_fence(&ring, packet, packet_count,
        0x600000U, 0x12345678U) == AMD_POLARIS_GFX_RING_FAULTED);
    assert(amd_polaris_gfx_ring_shutdown(&ring, 1) ==
        AMD_POLARIS_GFX_RING_OK);
}

static void test_ring_full_and_wrap(void)
{
    gpu_mmio_region_t mmio = make_mmio();
    amd_polaris_gfx_ring_t ring = {0};
    uint32_t packet[AMD_POLARIS_PM4_FENCE_DWORDS];
    size_t packet_count = 0U;

    memset(registers, 0, sizeof(registers));
    memset(ring_storage, 0, sizeof(ring_storage));
    assert(amd_polaris_gfx_ring_init(&ring, &mmio, ring_storage,
        0x400000U, sizeof(ring_storage), 0x500000U, 1, 1) ==
        AMD_POLARIS_GFX_RING_OK);
    assert(amd_polaris_build_fence_packet(packet, AMD_POLARIS_PM4_FENCE_DWORDS,
        0x600000U, 1U, &packet_count) == AMD_POLARIS_PM4_OK);

    /* With five dwords free-space unavailable, submission must not overwrite the ring. */
    registers[REG_CP_RB0_RPTR / 4U] =
        (uint32_t)(sizeof(ring_storage) / sizeof(uint32_t) -
                   AMD_POLARIS_PM4_FENCE_DWORDS);
    assert(amd_polaris_gfx_ring_submit_fence(&ring, packet, packet_count,
        0x600000U, 1U) == AMD_POLARIS_GFX_RING_BUSY);

    /* Exercise wraparound by placing the producer just before the end. */
    ring.write_pointer = (uint32_t)(sizeof(ring_storage) / sizeof(uint32_t) - 2U);
    registers[REG_CP_RB0_RPTR / 4U] = 0U;
    assert(amd_polaris_gfx_ring_submit_fence(&ring, packet, packet_count,
        0x600000U, 1U) == AMD_POLARIS_GFX_RING_OK);
    assert(reg(REG_CP_RB0_WPTR) == AMD_POLARIS_PM4_FENCE_DWORDS - 2U);
    assert(ring_storage[sizeof(ring_storage) / sizeof(uint32_t) - 2U] == packet[0]);
    assert(ring_storage[sizeof(ring_storage) / sizeof(uint32_t) - 1U] == packet[1]);
    assert(ring_storage[0] == packet[2]);
    assert(ring_storage[1] == packet[3]);
    assert(ring_storage[2] == packet[4]);
    assert(amd_polaris_gfx_ring_shutdown(&ring, 1) ==
        AMD_POLARIS_GFX_RING_OK);
}

int main(void)
{
    test_prerequisites_and_setup();
    test_fence_submission_and_timeout();
    test_ring_full_and_wrap();
    return 0;
}
