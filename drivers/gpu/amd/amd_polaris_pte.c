/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "amd_polaris_pte.h"

amd_polaris_pte_status_t amd_polaris_encode_pte(
    uint64_t physical_address, unsigned access, int system_memory,
    int snooped, uint64_t *out_pte)
{
    uint64_t pte;

    if (out_pte == 0)
        return AMD_POLARIS_PTE_INVALID_ARGUMENT;
    *out_pte = 0U;
    if ((access & ~(AMD_POLARIS_PTE_ACCESS_READ |
                    AMD_POLARIS_PTE_ACCESS_WRITE |
                    AMD_POLARIS_PTE_ACCESS_EXEC)) != 0U ||
        (access & (AMD_POLARIS_PTE_ACCESS_READ |
                   AMD_POLARIS_PTE_ACCESS_WRITE |
                   AMD_POLARIS_PTE_ACCESS_EXEC)) == 0U ||
        (system_memory != 0 && system_memory != 1) ||
        (snooped != 0 && snooped != 1))
        return AMD_POLARIS_PTE_INVALID_ACCESS;
    if ((physical_address & 0xFFFU) != 0U)
        return AMD_POLARIS_PTE_UNALIGNED_ADDRESS;
    if ((physical_address & ~AMD_POLARIS_PTE_ADDR_MASK) != 0U)
        return AMD_POLARIS_PTE_ADDRESS_TOO_WIDE;

    pte = physical_address | AMD_POLARIS_PTE_VALID;
    if (system_memory)
        pte |= AMD_POLARIS_PTE_SYSTEM;
    if (snooped)
        pte |= AMD_POLARIS_PTE_SNOOPED;
    if ((access & AMD_POLARIS_PTE_ACCESS_READ) != 0U)
        pte |= AMD_POLARIS_PTE_READABLE;
    if ((access & AMD_POLARIS_PTE_ACCESS_WRITE) != 0U)
        pte |= AMD_POLARIS_PTE_WRITEABLE;
    if ((access & AMD_POLARIS_PTE_ACCESS_EXEC) != 0U)
        pte |= AMD_POLARIS_PTE_EXECUTABLE;

    *out_pte = pte;
    return AMD_POLARIS_PTE_OK;
}
