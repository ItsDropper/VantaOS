/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_INTEL_GEN9_GGTT_H
#define VANTA_INTEL_GEN9_GGTT_H

#include "gpu_mmio.h"

#define INTEL_GEN9_GTTMMADR_SIZE 0x01000000U
#define INTEL_GEN9_GGTT_PTE_BASE 0x00800000U
#define INTEL_GEN9_GGTT_PTE_COUNT 1048576U
#define INTEL_GEN9_GFX_FLSH_CNTL 0x00101008U

typedef enum intel_gen9_ggtt_status {
    INTEL_GEN9_GGTT_OK = 0,
    INTEL_GEN9_GGTT_INVALID_ARGUMENT,
    INTEL_GEN9_GGTT_UNALIGNED,
    INTEL_GEN9_GGTT_OUT_OF_RANGE,
    INTEL_GEN9_GGTT_MMIO_ERROR,
    INTEL_GEN9_GGTT_VERIFY_FAILED,
    INTEL_GEN9_GGTT_FORCEWAKE_FAILED,
    INTEL_GEN9_GGTT_STATE_UNCERTAIN
} intel_gen9_ggtt_status_t;

typedef struct intel_gen9_ggtt_forcewake_ops {
    void *context;
    int (*acquire_render)(void *context);
    int (*release_render)(void *context);
} intel_gen9_ggtt_forcewake_ops_t;

/* GTTMMADR must be a mapped, uncached 16 MiB Gen8/Gen9 BAR0 aperture. */
intel_gen9_ggtt_status_t intel_gen9_ggtt_map_page(
    const gpu_mmio_region_t *gttmmadr, uint64_t ggtt_address,
    uint64_t physical_address);
intel_gen9_ggtt_status_t intel_gen9_ggtt_unmap_page(
    const gpu_mmio_region_t *gttmmadr, uint64_t ggtt_address);
/* Transactional contiguous DMA mapping. Caller serializes GGTT updates. */
intel_gen9_ggtt_status_t intel_gen9_ggtt_map_contiguous(
    const gpu_mmio_region_t *gttmmadr, uint64_t ggtt_address,
    uint64_t physical_address, size_t size,
    const intel_gen9_ggtt_forcewake_ops_t *forcewake);
intel_gen9_ggtt_status_t intel_gen9_ggtt_unmap_contiguous(
    const gpu_mmio_region_t *gttmmadr, uint64_t ggtt_address,
    size_t size, const intel_gen9_ggtt_forcewake_ops_t *forcewake);

/* Flush after a batch of PTE writes; render forcewake must be acquired/released. */
intel_gen9_ggtt_status_t intel_gen9_ggtt_invalidate(
    const gpu_mmio_region_t *gttmmadr,
    const intel_gen9_ggtt_forcewake_ops_t *forcewake);

/* Gen9 render forcewake request/acknowledgement with bounded polling. */
int intel_gen9_forcewake_render_acquire(const gpu_mmio_region_t *gttmmadr,
                                        unsigned timeout_iterations);
int intel_gen9_forcewake_render_release(const gpu_mmio_region_t *gttmmadr,
                                        unsigned timeout_iterations);

#endif
