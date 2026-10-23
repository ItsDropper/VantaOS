/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_INTEL_GEN9_PTE_H
#define VANTA_INTEL_GEN9_PTE_H

#include <stdint.h>

typedef enum intel_gen9_pte_status {
    INTEL_GEN9_PTE_OK = 0,
    INTEL_GEN9_PTE_INVALID_ARGUMENT,
    INTEL_GEN9_PTE_UNALIGNED_ADDRESS,
    INTEL_GEN9_PTE_ADDRESS_TOO_WIDE,
    INTEL_GEN9_PTE_INVALID_CACHE_POLICY
} intel_gen9_pte_status_t;

typedef enum intel_gen9_cache_policy {
    INTEL_GEN9_CACHE_UNCACHED = 0,
    INTEL_GEN9_CACHE_WRITE_THROUGH,
    INTEL_GEN9_CACHE_WRITE_BACK
} intel_gen9_cache_policy_t;

#define INTEL_GEN9_PTE_PRESENT       (1ULL << 0)
#define INTEL_GEN9_PTE_WRITABLE      (1ULL << 1)
#define INTEL_GEN9_PTE_PWT           (1ULL << 3)
#define INTEL_GEN9_PTE_PCD           (1ULL << 4)
#define INTEL_GEN9_PTE_PAT           (1ULL << 7)
#define INTEL_GEN9_PTE_ADDRESS_MASK  0x0000FFFFFFFFF000ULL

/* Intel Gen8/Gen9 4 KiB PPGTT PTE format; no device programming is done. */
intel_gen9_pte_status_t intel_gen9_encode_pte(
    uint64_t physical_address, int writable,
    intel_gen9_cache_policy_t cache_policy, uint64_t *out_pte);

#endif
