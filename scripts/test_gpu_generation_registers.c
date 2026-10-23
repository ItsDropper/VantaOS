/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "amd_polaris_regs.h"
#include "intel_gen9_regs.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#define TEST_MMIO_BYTES 0x22000U

static uint32_t fake_mmio[TEST_MMIO_BYTES / sizeof(uint32_t)];

static void store_register(uint32_t byte_offset, uint32_t value)
{
    assert((byte_offset & 3U) == 0U);
    assert(byte_offset < TEST_MMIO_BYTES);
    fake_mmio[byte_offset / sizeof(uint32_t)] = value;
}

static void reset_fake_mmio(void)
{
    memset(fake_mmio, 0, sizeof(fake_mmio));
}

static gpu_mmio_region_t fake_region(size_t bytes)
{
    gpu_mmio_region_t region;
    region.base = (volatile uint8_t *)fake_mmio;
    region.length = bytes;
    return region;
}

static void test_intel_snapshot(void)
{
    gpu_mmio_region_t region = fake_region(TEST_MMIO_BYTES);
    intel_gen9_ring_snapshot_t snapshot;

    reset_fake_mmio();
    store_register(INTEL_GEN9_RING_HEAD, 0x100U);
    store_register(INTEL_GEN9_RING_TAIL, 0x108U);
    store_register(INTEL_GEN9_RING_START, 0x90000000U);
    store_register(INTEL_GEN9_RING_CTL, INTEL_GEN9_RING_CTL_VALID);
    store_register(INTEL_GEN9_RING_ACTHD, 0x90000100U);
    store_register(INTEL_GEN9_RING_MODE, 0U);
    store_register(INTEL_GEN9_RING_RESET_CTL, INTEL_GEN9_RING_RESET_READY);

    assert(intel_gen9_read_ring_snapshot(&region, &snapshot) ==
           INTEL_GEN9_RING_SNAPSHOT_OK);
    assert(snapshot.head == 0x100U && snapshot.tail == 0x108U);
    assert(snapshot.ring_size_bytes == 0x1000U);
    assert(snapshot.enabled && !snapshot.stopped && snapshot.reset_ready);

    store_register(INTEL_GEN9_RING_HEAD, 0x1000U);
    assert(intel_gen9_read_ring_snapshot(&region, &snapshot) ==
           INTEL_GEN9_RING_SNAPSHOT_INVALID_HEAD);

    store_register(INTEL_GEN9_RING_HEAD, 0x100U);
    store_register(INTEL_GEN9_RING_TAIL, 0x1000U);
    assert(intel_gen9_read_ring_snapshot(&region, &snapshot) ==
           INTEL_GEN9_RING_SNAPSHOT_INVALID_TAIL);

    store_register(INTEL_GEN9_RING_TAIL, 0x108U);
    store_register(INTEL_GEN9_RING_CTL, 0x001FE000U);
    assert(intel_gen9_read_ring_snapshot(&region, &snapshot) ==
           INTEL_GEN9_RING_SNAPSHOT_INVALID_RING_SIZE);

    region.length = INTEL_GEN9_RING_RESET_CTL;
    memset(&snapshot, 0xA5, sizeof(snapshot));
    assert(intel_gen9_read_ring_snapshot(&region, &snapshot) ==
           INTEL_GEN9_RING_SNAPSHOT_MMIO_ERROR);
    assert(snapshot.ring_size_bytes == 0U && snapshot.head == 0U);

    assert(intel_gen9_read_ring_snapshot(0, &snapshot) ==
           INTEL_GEN9_RING_SNAPSHOT_INVALID_ARGUMENT);
    assert(intel_gen9_read_ring_snapshot(&region, 0) ==
           INTEL_GEN9_RING_SNAPSHOT_INVALID_ARGUMENT);
}

static void test_amd_snapshot(void)
{
    gpu_mmio_region_t region = fake_region(TEST_MMIO_BYTES);
    amd_polaris_snapshot_t snapshot;

    reset_fake_mmio();
    store_register(AMD_POLARIS_REG_BYTE_OFFSET(
                       AMD_POLARIS_REG_GRBM_STATUS_INDEX), 0x11U);
    store_register(AMD_POLARIS_REG_BYTE_OFFSET(
                       AMD_POLARIS_REG_GRBM_STATUS2_INDEX), 0x22U);
    store_register(AMD_POLARIS_REG_BYTE_OFFSET(
                       AMD_POLARIS_REG_SRBM_STATUS_INDEX), 0x33U);
    store_register(AMD_POLARIS_REG_BYTE_OFFSET(
                       AMD_POLARIS_REG_CP_STAT_INDEX), 0x44U);

    assert(amd_polaris_read_snapshot(&region, &snapshot) ==
           AMD_POLARIS_SNAPSHOT_OK);
    assert(snapshot.grbm_status == 0x11U);
    assert(snapshot.grbm_status2 == 0x22U);
    assert(snapshot.srbm_status == 0x33U);
    assert(snapshot.cp_status == 0x44U);

    region.length = AMD_POLARIS_REG_BYTE_OFFSET(
                        AMD_POLARIS_REG_CP_STAT_INDEX);
    memset(&snapshot, 0xA5, sizeof(snapshot));
    assert(amd_polaris_read_snapshot(&region, &snapshot) ==
           AMD_POLARIS_SNAPSHOT_MMIO_ERROR);
    assert(snapshot.grbm_status == 0U && snapshot.cp_status == 0U);

    assert(amd_polaris_read_snapshot(0, &snapshot) ==
           AMD_POLARIS_SNAPSHOT_INVALID_ARGUMENT);
    assert(amd_polaris_read_snapshot(&region, 0) ==
           AMD_POLARIS_SNAPSHOT_INVALID_ARGUMENT);
}

int main(void)
{
    test_intel_snapshot();
    test_amd_snapshot();
    return 0;
}
