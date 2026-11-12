/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_wpr_policy.h"

#include <string.h>

#define MB (UINT64_C(1) << 20)
#define GB (UINT64_C(1) << 30)

static uint64_t up(uint64_t v, uint64_t a) { return (v + a - 1U) / a * a; }

nvx_gsp_wpr_policy_status_t nvx_gsp_ga10x_fb_facts_read(
    const nvx_mmio_region_t *bar0, nvx_gsp_ga10x_fb_facts_t *out)
{
    if (!bar0 || !out) return NVX_GSP_WPR_POLICY_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    const struct { size_t off; uint32_t *dst; } regs[] = {
        { NVX_GA10X_REG_MMU_LOCK_PLM, &out->mmu_lock_plm },
        { NVX_GA10X_REG_MMU_LOCK_LO,  &out->mmu_lock_lo_reg },
        { NVX_GA10X_REG_MMU_LOCK_HI,  &out->mmu_lock_hi_reg },
        { NVX_GA10X_REG_VGA_WORKSPACE,&out->vga_workspace_reg },
    };
    for (size_t i = 0; i < sizeof(regs) / sizeof(regs[0]); i++)
        if (nvx_mmio_read32(bar0, regs[i].off, regs[i].dst) != NVX_MMIO_OK)
            return NVX_GSP_WPR_POLICY_MMIO_FAILED;
    /* Reference: memmgrReadMmuLock_GA100 requires read protection level 0. */
    if (!(out->mmu_lock_plm & 1U)) return NVX_GSP_WPR_POLICY_MMU_LOCK_UNREADABLE;
    return NVX_GSP_WPR_POLICY_OK;
}

int nvx_gsp_ga10x_wpr_heap_size(void *context, uint64_t pre, uint64_t post, uint64_t *out)
{
    const nvx_gsp_ga10x_fb_facts_t *f = context;
    if (!f || !out || !f->usable_fb_bytes) return 0;

    /* kgspGetFwHeapSize: no scrubber ucode on GA10x, so limit to prescrubbed top of FB. */
    uint64_t anterior_posterior = pre + post;
    if (pre && anterior_posterior < pre) return 0;
    if (anterior_posterior > NVX_GA10X_PRESCRUBBED_TOP_FB) return 0; /* NVIDIA's u64 would wrap */
    uint64_t max_scrubbed_mb = (NVX_GA10X_PRESCRUBBED_TOP_FB - anterior_posterior) >> 20;

    /* _kgspCalculateFwHeapSize */
    uint64_t gb = up(f->usable_fb_bytes, GB) >> 30;
    uint64_t heap = NVX_GA10X_HEAP_OS_CARVEOUT + NVX_GA10X_HEAP_BASE_RM +
                    up(NVX_GA10X_HEAP_PER_GB * gb, MB) +
                    up(NVX_GA10X_HEAP_CLIENT_ALLOC, MB) + NVX_GA10X_HEAP_UCODE_RECLAIM;
    if (heap < NVX_GA10X_HEAP_MIN) heap = NVX_GA10X_HEAP_MIN;
    if (heap > (max_scrubbed_mb << 20)) heap = max_scrubbed_mb << 20;
    if (!heap) return 0;
    *out = heap;
    return 1;
}

nvx_gsp_wpr_policy_status_t nvx_gsp_ga10x_wpr_plan_config(
    const nvx_gsp_ga10x_fb_facts_t *f, nvx_gsp_wpr_plan_config_t *c)
{
    if (!f || !c || !f->usable_fb_bytes || (f->usable_fb_bytes & (MB - 1U)))
        return NVX_GSP_WPR_POLICY_INVALID_ARGUMENT;
    memset(c, 0, sizeof(*c));
    uint64_t fb = f->usable_fb_bytes;

    /* kgspPopulateWprMeta_TU102: VGA workspace. */
    uint32_t vga = f->vga_workspace_reg;
    int vga_present = ((vga >> 16) != 0xBADFU) && ((vga >> 3) & 1U);
    uint64_t vga_off;
    if (vga_present) {
        vga_off = (uint64_t)(vga >> 8) << 16;
        if (vga_off < fb - NVX_GA10X_PRAMIN_SIZE) vga_off = fb - NVX_GA10X_VBIOS_WORKSPACE_SIZE;
    } else {
        vga_off = fb - NVX_GA10X_PRAMIN_SIZE;
    }
    c->framebuffer_size = fb;
    c->vga_workspace_offset = vga_off;

    /* memmgrReadMmuLock_GA100: VAL is bits 31:4, shifted by ALIGNMENT (12). */
    uint64_t lo = (uint64_t)((f->mmu_lock_lo_reg >> 4) & 0x0FFFFFFFU) << 12;
    uint64_t hi = (uint64_t)((f->mmu_lock_hi_reg >> 4) & 0x0FFFFFFFU) << 12;
    c->mmu_lock_valid = hi >= lo;
    c->mmu_lock_low = lo;

    c->wpr_end_margin = 0U;                          /* first boot: no recovery margin */
    c->frts_size = NVX_GA10X_FRTS_SIZE;
    c->nonwpr_heap_size = up(NVX_GA10X_NONWPR_HEAP_PORTION, NVX_GA10X_NONWPR_ALIGN);
    c->compute_wpr_heap_size = nvx_gsp_ga10x_wpr_heap_size;
    c->heap_policy_context = (void *)f;
    return NVX_GSP_WPR_POLICY_OK;
}
