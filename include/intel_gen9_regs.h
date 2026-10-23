/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_INTEL_GEN9_REGS_H
#define VANTA_INTEL_GEN9_REGS_H

#include <stdint.h>
#include "gpu_mmio.h"
#include "intel_gen9.h"

/*
 * Skylake Gen9 render-ring register offsets. These are byte offsets in the
 * GTTMMADR MMIO mapping, not PCI configuration offsets.
 */
#define INTEL_GEN9_RENDER_RING_BASE       0x2000U
#define INTEL_GEN9_RING_TAIL              (INTEL_GEN9_RENDER_RING_BASE + 0x30U)
#define INTEL_GEN9_RING_HEAD              (INTEL_GEN9_RENDER_RING_BASE + 0x34U)
#define INTEL_GEN9_RING_START             (INTEL_GEN9_RENDER_RING_BASE + 0x38U)
#define INTEL_GEN9_RING_CTL               (INTEL_GEN9_RENDER_RING_BASE + 0x3CU)
#define INTEL_GEN9_RING_ACTHD             (INTEL_GEN9_RENDER_RING_BASE + 0x74U)
#define INTEL_GEN9_RING_MODE              (INTEL_GEN9_RENDER_RING_BASE + 0x9CU)
#define INTEL_GEN9_RING_RESET_CTL         (INTEL_GEN9_RENDER_RING_BASE + 0xD0U)

#define INTEL_GEN9_RING_HEAD_ADDR_MASK    0x001FFFFCU
#define INTEL_GEN9_RING_TAIL_ADDR_MASK    0x001FFFF8U
#define INTEL_GEN9_RING_CTL_VALID         0x00000001U
#define INTEL_GEN9_RING_CTL_SIZE_MASK     0x001FF000U
#define INTEL_GEN9_RING_MODE_STOP         (1U << 8)
#define INTEL_GEN9_RING_RESET_READY       (1U << 1)
#define INTEL_GEN9_RING_RESET_REQUEST     (1U << 0)

typedef enum intel_gen9_ring_snapshot_status {
    INTEL_GEN9_RING_SNAPSHOT_OK = 0,
    INTEL_GEN9_RING_SNAPSHOT_INVALID_ARGUMENT,
    INTEL_GEN9_RING_SNAPSHOT_MMIO_ERROR,
    INTEL_GEN9_RING_SNAPSHOT_INVALID_HEAD,
    INTEL_GEN9_RING_SNAPSHOT_INVALID_TAIL,
    INTEL_GEN9_RING_SNAPSHOT_INVALID_RING_SIZE
} intel_gen9_ring_snapshot_status_t;

typedef struct intel_gen9_ring_snapshot {
    uint32_t head;
    uint32_t tail;
    uint32_t start;
    uint32_t control;
    uint32_t active_head;
    uint32_t mode;
    uint32_t reset_control;
    uint32_t ring_size_bytes;
    uint8_t enabled;
    uint8_t stopped;
    uint8_t reset_ready;
} intel_gen9_ring_snapshot_t;

/*
 * Read-only diagnostic snapshot. It never requests reset, changes forcewake,
 * writes a register, or enables the engine. The caller must already own a
 * valid GTTMMADR mapping and have performed any required forcewake protocol.
 */
intel_gen9_ring_snapshot_status_t intel_gen9_read_ring_snapshot(
    const gpu_mmio_region_t *gttmmadr,
    intel_gen9_ring_snapshot_t *out_snapshot);

#endif
