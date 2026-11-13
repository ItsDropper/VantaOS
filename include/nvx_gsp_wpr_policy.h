/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_WPR_POLICY_H
#define VANTA_NVX_GSP_WPR_POLICY_H

#include <stddef.h>
#include <stdint.h>
#include "nvx_gsp_wpr_meta.h"
#include "nvx_mmio.h"

/*
 * GA10x (GA102-GA107, baremetal, no heap override) framebuffer/WPR sizing
 * policy.
 *
 * PROVENANCE: reference-informed hypothesis, NOT established NVX knowledge.
 * The constants and formulas below follow the policy of NVIDIA's reference
 * driver (NVX-F-0050, NVX-F-0051, NVX-F-0052; source class external-impl,
 * status unverified, see docs/gpu/nvx/INDEPENDENCE.md). They are a reference
 * driver's choices, not hardware facts. Experiments E4/E5 must establish the
 * values the firmware really requires before this module drives a real boot;
 * until then a different, independently derived policy may replace it.
 * Register offsets used here are listed in docs/gpu/nvx/knowledge/facts.tsv
 * (NVX-F-0011 to NVX-F-0014). scripts/test_nvx_wpr_policy.c proves internal
 * consistency only.
 *
 * Deliberate gaps, all fail-closed rather than guessed:
 *  - gpuFuseSupportsDisplay() is not read. A VGA-workspace register that reads
 *    as a PRI error (0xBADFxxxx) is treated as "no display".
 *  - NV_PRAMIN size is taken as 1 MiB (not independently established).
 *  - Recovery margins (bootAttempts > 0) are not implemented; first boot only.
 */
#define NVX_GA10X_FRTS_SIZE            (UINT64_C(1) << 20)
#define NVX_GA10X_PRAMIN_SIZE          (UINT64_C(1) << 20)
#define NVX_GA10X_VBIOS_WORKSPACE_SIZE UINT64_C(0x20000)
#define NVX_GA10X_PRESCRUBBED_TOP_FB   (UINT64_C(256) << 20)
#define NVX_GA10X_HEAP_BASE_RM         (UINT64_C(8) << 20)
#define NVX_GA10X_HEAP_UCODE_RECLAIM   (UINT64_C(55) << 20)
#define NVX_GA10X_HEAP_OS_CARVEOUT     (UINT64_C(7) << 20)
#define NVX_GA10X_HEAP_PER_GB          (UINT64_C(96) << 10)
#define NVX_GA10X_HEAP_CLIENT_ALLOC    ((UINT64_C(48) << 10) * 2048U)
#define NVX_GA10X_HEAP_MIN             (UINT64_C(37) << 20)
#define NVX_GA10X_NONWPR_HEAP_PORTION  (UINT64_C(1) << 20)
#define NVX_GA10X_NONWPR_ALIGN         UINT64_C(65536)

#define NVX_GA10X_REG_MMU_LOCK_PLM     0x001FA7C8U
#define NVX_GA10X_REG_MMU_LOCK_LO      0x001FA82CU
#define NVX_GA10X_REG_MMU_LOCK_HI      0x001FA830U
#define NVX_GA10X_REG_VGA_WORKSPACE    0x00625F04U

typedef enum nvx_gsp_wpr_policy_status {
    NVX_GSP_WPR_POLICY_OK = 0,
    NVX_GSP_WPR_POLICY_INVALID_ARGUMENT,
    NVX_GSP_WPR_POLICY_MMIO_FAILED,
    NVX_GSP_WPR_POLICY_MMU_LOCK_UNREADABLE,
    NVX_GSP_WPR_POLICY_HEAP_RANGE
} nvx_gsp_wpr_policy_status_t;

/* Raw hardware facts; no interpretation beyond bit extraction. */
typedef struct nvx_gsp_ga10x_fb_facts {
    uint64_t usable_fb_bytes;
    uint32_t vga_workspace_reg;
    uint32_t mmu_lock_plm, mmu_lock_lo_reg, mmu_lock_hi_reg;
} nvx_gsp_ga10x_fb_facts_t;

/* Read-only register reads from mapped GA10x BAR0. */
nvx_gsp_wpr_policy_status_t nvx_gsp_ga10x_fb_facts_read(
    const nvx_mmio_region_t *bar0, nvx_gsp_ga10x_fb_facts_t *out);

/* Pure: turn facts into the planner configuration (framebuffer, VGA workspace,
 * MMU lock, FRTS, non-WPR heap, margin 0, heap-size policy). */
nvx_gsp_wpr_policy_status_t nvx_gsp_ga10x_wpr_plan_config(
    const nvx_gsp_ga10x_fb_facts_t *facts, nvx_gsp_wpr_plan_config_t *out);

/* _kgspCalculateFwHeapSize + kgspGetFwHeapSize for GA10x baremetal. The context
 * is a const nvx_gsp_ga10x_fb_facts_t *. Returns 1 on success, 0 on failure
 * (matches nvx_gsp_wpr_heap_size_fn). */
int nvx_gsp_ga10x_wpr_heap_size(void *context, uint64_t pre_heap_size,
                                uint64_t post_heap_size, uint64_t *out_heap_size);

#endif
