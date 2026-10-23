/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "intel_gen9_pte.h"

intel_gen9_pte_status_t intel_gen9_encode_pte(
    uint64_t physical_address, int writable,
    intel_gen9_cache_policy_t cache_policy, uint64_t *out_pte)
{
    uint64_t pte;

    if (out_pte == 0)
        return INTEL_GEN9_PTE_INVALID_ARGUMENT;
    *out_pte = 0U;
    if (writable != 0 && writable != 1)
        return INTEL_GEN9_PTE_INVALID_ARGUMENT;
    if ((physical_address & 0xFFFU) != 0U)
        return INTEL_GEN9_PTE_UNALIGNED_ADDRESS;
    if ((physical_address & ~INTEL_GEN9_PTE_ADDRESS_MASK) != 0U)
        return INTEL_GEN9_PTE_ADDRESS_TOO_WIDE;
    if (cache_policy != INTEL_GEN9_CACHE_UNCACHED &&
        cache_policy != INTEL_GEN9_CACHE_WRITE_THROUGH &&
        cache_policy != INTEL_GEN9_CACHE_WRITE_BACK)
        return INTEL_GEN9_PTE_INVALID_CACHE_POLICY;

    pte = physical_address | INTEL_GEN9_PTE_PRESENT;
    if (writable)
        pte |= INTEL_GEN9_PTE_WRITABLE;

    switch (cache_policy) {
    case INTEL_GEN9_CACHE_UNCACHED:
        pte |= INTEL_GEN9_PTE_PWT | INTEL_GEN9_PTE_PCD;
        break;
    case INTEL_GEN9_CACHE_WRITE_THROUGH:
        pte |= INTEL_GEN9_PTE_PCD;
        break;
    case INTEL_GEN9_CACHE_WRITE_BACK:
        pte |= INTEL_GEN9_PTE_PAT;
        break;
    default:
        return INTEL_GEN9_PTE_INVALID_CACHE_POLICY;
    }

    *out_pte = pte;
    return INTEL_GEN9_PTE_OK;
}
