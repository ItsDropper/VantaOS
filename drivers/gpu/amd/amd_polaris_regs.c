/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "amd_polaris_regs.h"

#include <string.h>

static int read_register(const gpu_mmio_region_t *region, uint32_t index,
                         uint32_t *value)
{
    return gpu_mmio_read32(region, AMD_POLARIS_REG_BYTE_OFFSET(index),
                           value) == GPU_MMIO_OK;
}

amd_polaris_snapshot_status_t amd_polaris_read_snapshot(
    const gpu_mmio_region_t *register_aperture,
    amd_polaris_snapshot_t *out_snapshot)
{
    amd_polaris_snapshot_t snapshot;

    if (register_aperture == 0 || out_snapshot == 0)
        return AMD_POLARIS_SNAPSHOT_INVALID_ARGUMENT;
    memset(out_snapshot, 0, sizeof(*out_snapshot));
    memset(&snapshot, 0, sizeof(snapshot));

    /*
     * Publish the snapshot atomically from the caller's perspective: a short
     * mapping or any failed access returns an all-zero output structure.
     */
    if (!read_register(register_aperture,
                       AMD_POLARIS_REG_GRBM_STATUS_INDEX,
                       &snapshot.grbm_status) ||
        !read_register(register_aperture,
                       AMD_POLARIS_REG_GRBM_STATUS2_INDEX,
                       &snapshot.grbm_status2) ||
        !read_register(register_aperture,
                       AMD_POLARIS_REG_SRBM_STATUS_INDEX,
                       &snapshot.srbm_status) ||
        !read_register(register_aperture,
                       AMD_POLARIS_REG_CP_STAT_INDEX,
                       &snapshot.cp_status))
        return AMD_POLARIS_SNAPSHOT_MMIO_ERROR;

    *out_snapshot = snapshot;
    return AMD_POLARIS_SNAPSHOT_OK;
}
