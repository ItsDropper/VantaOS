/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
/*
 * Known-answer tests for the GA10x WPR sizing policy. Expected values were
 * derived separately from NVIDIA's kgspPopulateWprMeta_TU102 and
 * _kgspCalculateFwHeapSize (open-gpu-kernel-modules), not from NVX code.
 * Software evidence only: no GPU accepted these layouts.
 */
#include "nvx_gsp_wpr_policy.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define GB (UINT64_C(1) << 30)

static nvx_gsp_wpr_layout_t plan(const nvx_gsp_ga10x_fb_facts_t *f,
                                 uint64_t radix3, uint64_t boot)
{
    nvx_gsp_wpr_plan_config_t cfg;
    nvx_gsp_wpr_layout_t res, out;
    memset(&res, 0, sizeof(res));
    res.radix3_size = radix3; res.bootloader_size = boot; res.signature_size = 0x1000;
    res.sysmem_radix3_addr = 0x10000000; res.sysmem_bootloader_addr = 0x20000000;
    res.sysmem_signature_addr = 0x30000000;
    res.bootloader_code_offset = 0x100; res.bootloader_data_offset = 0x200; res.bootloader_manifest_offset = 0x300;
    assert(nvx_gsp_ga10x_wpr_plan_config(f, &cfg) == NVX_GSP_WPR_POLICY_OK);
    nvx_gsp_wpr_status_t st = nvx_gsp_wpr_layout_plan(&res, &cfg, &out);
    if (st != NVX_GSP_WPR_OK) { fprintf(stderr, "plan status %d fb=%llx vga=%llx lock=%d/%llx\n", (int)st, (unsigned long long)cfg.framebuffer_size, (unsigned long long)cfg.vga_workspace_offset, cfg.mmu_lock_valid, (unsigned long long)cfg.mmu_lock_low); }
    assert(st == NVX_GSP_WPR_OK);
    return out;
}

int main(void)
{
    /* A: 12 GiB, display register absent (PRI error), MMU lock unset (hi < lo). */
    nvx_gsp_ga10x_fb_facts_t a = { 12 * GB, 0xBADF3000U, 1U, 0xFFFFFFF0U, 0U };
    nvx_gsp_wpr_layout_t l = plan(&a, 0x1D4A123, 0x9000);
    assert(l.vga_workspace_offset == 0x2fff00000ULL);
    assert(l.wpr_end == 0x2fff00000ULL);
    assert(l.frts_offset == 0x2ffe00000ULL && l.frts_size == 0x100000ULL);
    assert(l.boot_bin_offset == 0x2ffdf7000ULL);
    assert(l.firmware_offset == 0x2fe0a0000ULL);
    assert(l.wpr_heap_offset == 0x2f3800000ULL && l.wpr_heap_size == 0xa800000ULL);
    assert(l.wpr_start == 0x2f3700000ULL);
    assert(l.nonwpr_heap_offset == 0x2f3600000ULL && l.nonwpr_heap_size == 0x100000ULL);

    /* B: VGA workspace valid near the top, MMU lock below it wins. */
    uint32_t vga = (uint32_t)(((12 * GB - 0x20000) >> 16) << 8) | 8U;
    uint64_t lock_lo = 12 * GB - 0x300000;
    nvx_gsp_ga10x_fb_facts_t b = { 12 * GB, vga,
        1U, (uint32_t)((lock_lo >> 12) << 4), (uint32_t)(((12 * GB) >> 12) << 4) };
    /* hi register above 32 bits cannot be encoded; use lo<=hi in field units. */
    b.mmu_lock_hi_reg = 0xFFFFFFF0U;
    l = plan(&b, 0x1D4A123, 0x9000);
    assert(l.vga_workspace_offset == 0x2fffe0000ULL);
    assert(l.wpr_end == 0x2ffd00000ULL);
    assert(l.frts_offset == 0x2ffc00000ULL);
    assert(l.firmware_offset == 0x2fdea0000ULL);
    assert(l.wpr_heap_offset == 0x2f3600000ULL && l.wpr_heap_size == 0xa800000ULL);
    assert(l.wpr_start == 0x2f3500000ULL && l.nonwpr_heap_offset == 0x2f3400000ULL);

    /* C: 8 GiB, VGA base far below the top is moved to fb - 128 KiB. */
    nvx_gsp_ga10x_fb_facts_t c = { 8 * GB, (0x1000U << 8) | 8U, 1U, 0xFFFFFFF0U, 0U };
    l = plan(&c, 0x2000000, 0x8000);
    assert(l.vga_workspace_offset == 0x1fffe0000ULL);
    assert(l.firmware_offset == 0x1fded0000ULL);
    assert(l.wpr_heap_size == 0xa700000ULL);          /* 167 MiB for an 8 GiB card */
    assert(l.nonwpr_heap_offset == 0x1f3500000ULL);

    /* Heap policy edge cases. */
    uint64_t heap = 0;
    assert(nvx_gsp_ga10x_wpr_heap_size(&a, 2U << 20, 40U << 20, &heap) && heap == (168ULL << 20));
    /* Tiny prescrubbed headroom clamps the heap down (NVIDIA clamps below min too). */
    assert(nvx_gsp_ga10x_wpr_heap_size(&a, 2U << 20, 240U << 20, &heap) && heap == (14ULL << 20));
    /* Reserved regions exceeding the 256 MiB prescrubbed window fail closed. */
    assert(!nvx_gsp_ga10x_wpr_heap_size(&a, 2U << 20, 255U << 20, &heap));
    assert(!nvx_gsp_ga10x_wpr_heap_size(&a, 0, 0, NULL));

    /* Facts reader: PLM without read protection is an error, never a guess. */
    nvx_gsp_wpr_plan_config_t cfg;
    nvx_gsp_ga10x_fb_facts_t bad = { 0 };
    assert(nvx_gsp_ga10x_wpr_plan_config(&bad, &cfg) == NVX_GSP_WPR_POLICY_INVALID_ARGUMENT);
    bad.usable_fb_bytes = GB + 4096U;                  /* not 1 MiB aligned */
    assert(nvx_gsp_ga10x_wpr_plan_config(&bad, &cfg) == NVX_GSP_WPR_POLICY_INVALID_ARGUMENT);
    /* Facts reader against synthetic BAR0: exact register offsets, fail-closed PLM. */
    static uint32_t bar[0x626000 / 4];
    nvx_mmio_region_t region = { (volatile uint8_t *)bar, sizeof(bar) };
    nvx_gsp_ga10x_fb_facts_t got;
    bar[0x1FA7C8 / 4] = 0U; bar[0x1FA82C / 4] = 0x12345670U;
    bar[0x1FA830 / 4] = 0x23456780U; bar[0x625F04 / 4] = 0xAB00U | 8U;
    assert(nvx_gsp_ga10x_fb_facts_read(&region, &got) == NVX_GSP_WPR_POLICY_MMU_LOCK_UNREADABLE);
    bar[0x1FA7C8 / 4] = 1U;
    assert(nvx_gsp_ga10x_fb_facts_read(&region, &got) == NVX_GSP_WPR_POLICY_OK);
    assert(got.mmu_lock_lo_reg == 0x12345670U && got.mmu_lock_hi_reg == 0x23456780U);
    assert(got.vga_workspace_reg == (0xAB00U | 8U));
    nvx_mmio_region_t small = { (volatile uint8_t *)bar, 0x1000 };
    assert(nvx_gsp_ga10x_fb_facts_read(&small, &got) == NVX_GSP_WPR_POLICY_MMIO_FAILED);
    return 0;
}
