/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_AMD_POLARIS_PTE_H
#define VANTA_AMD_POLARIS_PTE_H

#include <stdint.h>

typedef enum amd_polaris_pte_status {
    AMD_POLARIS_PTE_OK = 0,
    AMD_POLARIS_PTE_INVALID_ARGUMENT,
    AMD_POLARIS_PTE_UNALIGNED_ADDRESS,
    AMD_POLARIS_PTE_ADDRESS_TOO_WIDE,
    AMD_POLARIS_PTE_INVALID_ACCESS
} amd_polaris_pte_status_t;

#define AMD_POLARIS_PTE_VALID       (1ULL << 0)
#define AMD_POLARIS_PTE_SYSTEM      (1ULL << 1)
#define AMD_POLARIS_PTE_SNOOPED     (1ULL << 2)
#define AMD_POLARIS_PTE_EXECUTABLE  (1ULL << 4)
#define AMD_POLARIS_PTE_READABLE    (1ULL << 5)
#define AMD_POLARIS_PTE_WRITEABLE   (1ULL << 6)
#define AMD_POLARIS_PTE_ADDR_MASK   0x000000FFFFFFF000ULL

#define AMD_POLARIS_PTE_ACCESS_READ  1U
#define AMD_POLARIS_PTE_ACCESS_WRITE 2U
#define AMD_POLARIS_PTE_ACCESS_EXEC  4U

/*
 * GCN4/GFX8 leaf PTE flag positions. System-memory mappings are snooped,
 * non-executable by default, and only receive permissions explicitly asked
 * for by the caller. This encodes one 4 KiB leaf PTE, not the page-table walk.
 */
amd_polaris_pte_status_t amd_polaris_encode_pte(
    uint64_t physical_address, unsigned access, int system_memory,
    int snooped, uint64_t *out_pte);

#endif
