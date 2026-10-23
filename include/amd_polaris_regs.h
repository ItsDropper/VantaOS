/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_AMD_POLARIS_REGS_H
#define VANTA_AMD_POLARIS_REGS_H

#include <stdint.h>
#include "gpu_mmio.h"
#include "amd_polaris.h"

/*
 * GCN4/GFX8 register indices from the Polaris register interface. The
 * MMIO byte offset is the register index multiplied by four; these are not
 * PCI configuration offsets. Access is read-only in this diagnostic module.
 */
#define AMD_POLARIS_REG_GRBM_STATUS_INDEX   0x8010U
#define AMD_POLARIS_REG_GRBM_STATUS2_INDEX  0x8011U
#define AMD_POLARIS_REG_SRBM_STATUS_INDEX   0x0E50U
#define AMD_POLARIS_REG_CP_STAT_INDEX       0x8684U

#define AMD_POLARIS_REG_BYTE_OFFSET(index)  ((uint32_t)(index) * 4U)

typedef enum amd_polaris_snapshot_status {
    AMD_POLARIS_SNAPSHOT_OK = 0,
    AMD_POLARIS_SNAPSHOT_INVALID_ARGUMENT,
    AMD_POLARIS_SNAPSHOT_MMIO_ERROR
} amd_polaris_snapshot_status_t;

typedef struct amd_polaris_snapshot {
    uint32_t grbm_status;
    uint32_t grbm_status2;
    uint32_t srbm_status;
    uint32_t cp_status;
} amd_polaris_snapshot_t;

/*
 * Reads a bounded set of GCN4 status registers. It does not reset engines,
 * load firmware, change power state, enable bus mastering, or submit work.
 * Caller must own a valid Polaris register-aperture mapping.
 */
amd_polaris_snapshot_status_t amd_polaris_read_snapshot(
    const gpu_mmio_region_t *register_aperture,
    amd_polaris_snapshot_t *out_snapshot);

#endif
