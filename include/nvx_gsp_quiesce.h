/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_QUIESCE_H
#define VANTA_NVX_GSP_QUIESCE_H

#include <stdint.h>

#include "nvx_attach.h"
#include "nvx_gsp_falcon.h"
/* ga10x.h must precede startup.h: msgq_ga10x.h defines the same macros. */
#include "nvx_gsp_ga10x.h"
#include "nvx_gsp_startup.h"

/*
 * GA10x SEC2/GSP quiesce. Implements nvx_gsp_platform_quiesce_fn.
 *
 * Scope: this proves only that the SEC2 and GSP falcon/RISC-V cores are held
 * out of execution after an engine reset. It does NOT stop the copy engines,
 * graphics engines or any channel work they were given; callers that created
 * RM channels must establish engine-level quiescence (channel disable plus
 * engine idle/reset) separately before reclaiming channel-reachable memory.
 *
 * Fail-closed rules:
 *  - an all-ones read (device gone / link down) is reported as DEVICE_LOST and
 *    returns "not quiesced"; a missing device is not proof that DMA stopped
 *    for the purposes of memory reclamation on a hot-plug platform;
 *  - a PRIV-locked window, reset/scrub timeout or MMIO failure returns "not
 *    quiesced" and leaves the caller's ownership untouched;
 *  - success requires post-reset readback showing the core not started/active.
 */
typedef enum nvx_gsp_quiesce_result {
    NVX_GSP_QUIESCE_NOT_RUN = 0,
    NVX_GSP_QUIESCE_OK,
    NVX_GSP_QUIESCE_INVALID_ARGUMENT,
    NVX_GSP_QUIESCE_BAR_TOO_SMALL,
    NVX_GSP_QUIESCE_DEVICE_LOST,
    NVX_GSP_QUIESCE_PRIV_LOCKED,
    NVX_GSP_QUIESCE_RESET_FAILED,
    NVX_GSP_QUIESCE_STILL_RUNNING,
    NVX_GSP_QUIESCE_MMIO_ERROR
} nvx_gsp_quiesce_result_t;

/* Raw per-engine evidence captured around the reset. */
typedef struct nvx_gsp_quiesce_engine_report {
    nvx_gsp_quiesce_result_t result;
    nvx_gsp_falcon_status_t reset_status;
    uint32_t hwcfg2_before;
    uint32_t cpuctl_before;
    uint32_t riscv_cpuctl_before;
    uint32_t hwcfg2_after;
    uint32_t cpuctl_after;
    uint32_t riscv_cpuctl_after;
    uint8_t attempted;
} nvx_gsp_quiesce_engine_report_t;

typedef struct nvx_gsp_quiesce_context {
    nvx_gsp_delay_ms_fn delay_ms;
    void *delay_context;
    uint32_t timeout_ms;
    uint32_t poll_interval_ms;
    /* Written by the callback; zeroed at entry. */
    nvx_gsp_quiesce_engine_report_t gsp;
    nvx_gsp_quiesce_engine_report_t sec2;
} nvx_gsp_quiesce_context_t;

/* Returns nonzero only if both GSP and SEC2 were verified held in reset-idle state. */
int nvx_gsp_ga10x_quiesce(
    nvx_attached_device_t *device,
    const nvx_gsp_startup_result_t *startup_result,
    void *context);

#endif
