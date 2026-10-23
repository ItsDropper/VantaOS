/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "intel_gen9_regs.h"

#include <string.h>

static int read_register(const gpu_mmio_region_t *region, size_t offset,
                         uint32_t *value)
{
    return gpu_mmio_read32(region, offset, value) == GPU_MMIO_OK;
}

intel_gen9_ring_snapshot_status_t intel_gen9_read_ring_snapshot(
    const gpu_mmio_region_t *gttmmadr,
    intel_gen9_ring_snapshot_t *out_snapshot)
{
    intel_gen9_ring_snapshot_t snapshot;
    uint32_t head_offset;
    uint32_t tail_offset;
    uint32_t encoded_size;

    if (gttmmadr == 0 || out_snapshot == 0)
        return INTEL_GEN9_RING_SNAPSHOT_INVALID_ARGUMENT;
    memset(out_snapshot, 0, sizeof(*out_snapshot));
    memset(&snapshot, 0, sizeof(snapshot));

    /*
     * Read into a private temporary so callers never receive a half-filled
     * snapshot if one MMIO access is out of range.
     */
    if (!read_register(gttmmadr, INTEL_GEN9_RING_HEAD, &snapshot.head) ||
        !read_register(gttmmadr, INTEL_GEN9_RING_TAIL, &snapshot.tail) ||
        !read_register(gttmmadr, INTEL_GEN9_RING_START, &snapshot.start) ||
        !read_register(gttmmadr, INTEL_GEN9_RING_CTL, &snapshot.control) ||
        !read_register(gttmmadr, INTEL_GEN9_RING_ACTHD,
                       &snapshot.active_head) ||
        !read_register(gttmmadr, INTEL_GEN9_RING_MODE, &snapshot.mode) ||
        !read_register(gttmmadr, INTEL_GEN9_RING_RESET_CTL,
                       &snapshot.reset_control))
        return INTEL_GEN9_RING_SNAPSHOT_MMIO_ERROR;

    encoded_size = snapshot.control & INTEL_GEN9_RING_CTL_SIZE_MASK;
    snapshot.ring_size_bytes = encoded_size + 0x1000U;
    if (snapshot.ring_size_bytes < 0x1000U ||
        snapshot.ring_size_bytes > 0x200000U ||
        (snapshot.ring_size_bytes & (snapshot.ring_size_bytes - 1U)) != 0U)
        return INTEL_GEN9_RING_SNAPSHOT_INVALID_RING_SIZE;

    head_offset = snapshot.head & INTEL_GEN9_RING_HEAD_ADDR_MASK;
    tail_offset = snapshot.tail & INTEL_GEN9_RING_TAIL_ADDR_MASK;
    if ((head_offset & 3U) != 0U ||
        head_offset >= snapshot.ring_size_bytes)
        return INTEL_GEN9_RING_SNAPSHOT_INVALID_HEAD;
    if ((tail_offset & 7U) != 0U ||
        tail_offset >= snapshot.ring_size_bytes)
        return INTEL_GEN9_RING_SNAPSHOT_INVALID_TAIL;

    snapshot.enabled = (uint8_t)(
        (snapshot.control & INTEL_GEN9_RING_CTL_VALID) != 0U);
    snapshot.stopped = (uint8_t)(
        (snapshot.mode & INTEL_GEN9_RING_MODE_STOP) != 0U);
    snapshot.reset_ready = (uint8_t)(
        (snapshot.reset_control & INTEL_GEN9_RING_RESET_READY) != 0U);

    *out_snapshot = snapshot;
    return INTEL_GEN9_RING_SNAPSHOT_OK;
}
