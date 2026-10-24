/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "intel_gen9_pte.h"
#include "amd_polaris_pte.h"

#include <assert.h>
#include <stdint.h>

static void test_intel_pte(void)
{
    uint64_t pte = UINT64_MAX;

    assert(intel_gen9_encode_pte(0x12345000U, 1,
               INTEL_GEN9_CACHE_WRITE_BACK, &pte) == INTEL_GEN9_PTE_OK);
    assert(pte == (0x12345000ULL | INTEL_GEN9_PTE_PRESENT |
                   INTEL_GEN9_PTE_WRITABLE | INTEL_GEN9_PTE_PAT));

    assert(intel_gen9_encode_pte(0x2000U, 0,
               INTEL_GEN9_CACHE_UNCACHED, &pte) == INTEL_GEN9_PTE_OK);
    assert(pte == (0x2000ULL | INTEL_GEN9_PTE_PRESENT |
                   INTEL_GEN9_PTE_PWT | INTEL_GEN9_PTE_PCD));

    assert(intel_gen9_encode_pte(0x3000U, 1,
               INTEL_GEN9_CACHE_WRITE_THROUGH, &pte) == INTEL_GEN9_PTE_OK);
    assert(pte == (0x3000ULL | INTEL_GEN9_PTE_PRESENT |
                   INTEL_GEN9_PTE_WRITABLE | INTEL_GEN9_PTE_PCD));

    assert(intel_gen9_encode_pte(0x1234U, 1,
               INTEL_GEN9_CACHE_WRITE_BACK, &pte) ==
           INTEL_GEN9_PTE_UNALIGNED_ADDRESS);
    assert(pte == 0U);
    assert(intel_gen9_encode_pte(0x0001000000000000ULL, 1,
               INTEL_GEN9_CACHE_WRITE_BACK, &pte) ==
           INTEL_GEN9_PTE_ADDRESS_TOO_WIDE);
    assert(intel_gen9_encode_pte(0x1000U, 1,
               (intel_gen9_cache_policy_t)99, &pte) ==
           INTEL_GEN9_PTE_INVALID_CACHE_POLICY);
    assert(intel_gen9_encode_pte(0x1000U, 2,
               INTEL_GEN9_CACHE_WRITE_BACK, &pte) ==
           INTEL_GEN9_PTE_INVALID_ARGUMENT);
    assert(intel_gen9_encode_pte(0x1000U, 1,
               INTEL_GEN9_CACHE_WRITE_BACK, 0) ==
           INTEL_GEN9_PTE_INVALID_ARGUMENT);
}

static void test_amd_pte(void)
{
    uint64_t pte = UINT64_MAX;

    assert(amd_polaris_encode_pte(0x12345000U,
               AMD_POLARIS_PTE_ACCESS_READ |
               AMD_POLARIS_PTE_ACCESS_WRITE,
               1, 1, &pte) == AMD_POLARIS_PTE_OK);
    assert(pte == (0x12345000ULL | AMD_POLARIS_PTE_VALID |
                   AMD_POLARIS_PTE_SYSTEM | AMD_POLARIS_PTE_SNOOPED |
                   AMD_POLARIS_PTE_READABLE | AMD_POLARIS_PTE_WRITEABLE));

    assert(amd_polaris_encode_pte(0x2000U,
               AMD_POLARIS_PTE_ACCESS_READ |
               AMD_POLARIS_PTE_ACCESS_EXEC,
               0, 0, &pte) == AMD_POLARIS_PTE_OK);
    assert(pte == (0x2000ULL | AMD_POLARIS_PTE_VALID |
                   AMD_POLARIS_PTE_READABLE | AMD_POLARIS_PTE_EXECUTABLE));

    assert(amd_polaris_encode_pte(0x1001U,
               AMD_POLARIS_PTE_ACCESS_READ, 1, 1, &pte) ==
           AMD_POLARIS_PTE_UNALIGNED_ADDRESS);
    assert(pte == 0U);
    assert(amd_polaris_encode_pte(0x0000010000000000ULL,
               AMD_POLARIS_PTE_ACCESS_READ, 1, 1, &pte) ==
           AMD_POLARIS_PTE_ADDRESS_TOO_WIDE);
    assert(amd_polaris_encode_pte(0x0001000000000000ULL,
               AMD_POLARIS_PTE_ACCESS_READ, 1, 1, &pte) ==
           AMD_POLARIS_PTE_ADDRESS_TOO_WIDE);
    assert(amd_polaris_encode_pte(0x1000U, 0U, 1, 1, &pte) ==
           AMD_POLARIS_PTE_INVALID_ACCESS);
    assert(amd_polaris_encode_pte(0x1000U, 8U, 1, 1, &pte) ==
           AMD_POLARIS_PTE_INVALID_ACCESS);
    assert(amd_polaris_encode_pte(0x1000U,
               AMD_POLARIS_PTE_ACCESS_READ, 2, 1, &pte) ==
           AMD_POLARIS_PTE_INVALID_ACCESS);
    assert(amd_polaris_encode_pte(0x1000U,
               AMD_POLARIS_PTE_ACCESS_READ, 1, 1, 0) ==
           AMD_POLARIS_PTE_INVALID_ARGUMENT);
}

int main(void)
{
    test_intel_pte();
    test_amd_pte();
    return 0;
}
