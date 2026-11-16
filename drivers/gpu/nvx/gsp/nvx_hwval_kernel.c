/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_hwval_kernel.h"

#include <string.h>

#include "nvx_gsp_adapter.h"
#include "nvx_gsp_platform.h"
#include "nvx_gsp_startup.h"
#include "nvx_hwval.h"

#ifndef VANTAOS_GIT_COMMIT
#define VANTAOS_GIT_COMMIT "unknown"
#endif


/* Bounded, always-terminated copy; the freestanding kernel has no strncpy. */
static void copy_text(char *dst, size_t cap, const char *src)
{
    size_t i = 0U;
    if (!dst || cap == 0U)
        return;
    for (; src && src[i] && i + 1U < cap; ++i)
        dst[i] = src[i];
    dst[i] = '\0';
}

static const nvx_bringup_io_t *g_io;

static void io_text(const char *s)
{
    if (g_io && g_io->text)
        g_io->text(s, g_io->context);
}

static void emit_line(const char *line, void *ctx)
{
    (void)ctx;
    io_text(line);
    io_text("\n");
}

static const nvx_gsp_adapter_progress_t *prog(void)
{
    return nvx_gsp_adapter_progress();
}

/* ---- Stages 1-5: evaluate what the adapter actually recorded ------------- */

static nvx_hwval_stage_status_t st_discover(void *c, nvx_hwval_stage_record_t *r)
{
    const nvx_gsp_adapter_progress_t *p = prog();
    (void)c;
    if (!p->pci_found) {
        r->reason = "no supported RTX 3060 on PCI";
        return NVX_HWVAL_FAIL;
    }
    nvx_hwval_add_u64(r, "pci_vendor", p->pci_vendor_id);
    nvx_hwval_add_u64(r, "pci_device", p->pci_device_id);
    nvx_hwval_add_u64(r, "pci_revision", p->pci_revision);
    return NVX_HWVAL_PASS;
}

static nvx_hwval_stage_status_t st_pci_resources(void *c, nvx_hwval_stage_record_t *r)
{
    const nvx_gsp_adapter_progress_t *p = prog();
    (void)c;
    if (!p->bar_valid) {
        r->reason = "BAR0 invalid or size unknown";
        return NVX_HWVAL_FAIL;
    }
    nvx_hwval_add_u64(r, "bar0_base", p->bar0_base);
    nvx_hwval_add_u64(r, "bar0_size", p->bar0_size);
    return NVX_HWVAL_PASS;
}

static nvx_hwval_stage_status_t st_safe_init(void *c, nvx_hwval_stage_record_t *r)
{
    const nvx_gsp_adapter_progress_t *p = prog();
    (void)c;
    nvx_hwval_add_u64(r, "handoff_result", p->handoff.result);
    nvx_hwval_add_u64(r, "handoff_pci_command", p->handoff.pci_command);
    nvx_hwval_add_u64(r, "handoff_gsp_hwcfg2", p->handoff.hwcfg2);
    nvx_hwval_add_u64(r, "handoff_riscv_cpuctl", p->handoff.riscv_cpuctl);
    nvx_hwval_add_u64(r, "handoff_sec2_cpuctl", p->handoff.sec2_cpuctl);
    if (!p->handoff_ran || p->handoff.result != NVX_GSP_HANDOFF_CLEAR) {
        r->reason = "handoff check refused the device; nothing was changed";
        return NVX_HWVAL_FAIL;
    }
    nvx_hwval_add_u64(r, "coordinator_status", p->coordinator_status);
    nvx_hwval_add_u64(r, "startup_attempted", p->startup_attempted);
    nvx_hwval_add_u64(r, "bus_master_enabled", p->bus_master_enabled);
    if (!p->start_attempted || !p->startup_attempted) {
        r->reason = "coordinator did not reach the SEC2/GSP transaction";
        return NVX_HWVAL_FAIL;
    }
    return NVX_HWVAL_PASS;
}

static nvx_hwval_stage_status_t st_firmware(void *c, nvx_hwval_stage_record_t *r)
{
    const nvx_gsp_adapter_progress_t *p = prog();
    (void)c;
    nvx_hwval_add_text(r, "firmware_version", p->firmware_version);
    nvx_hwval_add_u64(r, "startup_status", p->startup_status);
    nvx_hwval_add_u64(r, "bootstrap_status", p->bootstrap_status);
    nvx_hwval_add_u64(r, "booter_mailbox1", p->booter_mailbox1);
    /* Bootstrap (FWSEC/FRTS, SEC2 Booter, RISC-V active) must have succeeded. */
    if (!p->startup_attempted || p->bootstrap_status != NVX_GSP_FALCON_OK ||
        p->startup_status == NVX_GSP_STARTUP_INVALID_ARGUMENT ||
        p->startup_status == NVX_GSP_STARTUP_QUEUE_PREPARE_FAILED ||
        p->startup_status == NVX_GSP_STARTUP_BOOTSTRAP_FAILED) {
        r->reason = "SEC2/FWSEC/GSP bootstrap did not complete";
        return NVX_HWVAL_FAIL;
    }
    return NVX_HWVAL_PASS;
}

static nvx_hwval_stage_status_t st_gsp_rm(void *c, nvx_hwval_stage_record_t *r)
{
    const nvx_gsp_adapter_progress_t *p = prog();
    (void)c;
    nvx_hwval_add_u64(r, "queue_status", p->queue_status);
    nvx_hwval_add_u64(r, "gmcapi_status", p->gmcapi_status);
    nvx_hwval_add_u64(r, "gsp_init_remote_status", p->gsp_init_remote_status);
    nvx_hwval_add_u64(r, "gsp_rm_initialized", p->gsp_rm_initialized);
    nvx_hwval_add_u64(r, "rpc_initialized", p->rpc_initialized);
    /* The init RPC alone is not the independent RPC validation: that is stage 6. */
    nvx_hwval_add_u64(r, "independent_rpc_validated", 0U);
    if (p->coordinator_status != NVX_GSP_PLATFORM_OK || !p->gsp_rm_initialized ||
        !p->rpc_initialized) {
        r->reason = "GSP-RM or RPC transport did not initialize";
        return NVX_HWVAL_FAIL;
    }
    return NVX_HWVAL_PASS;
}

/* ---- Teardown ------------------------------------------------------------ */

static int hw_touched(void *c)
{
    (void)c;
    return prog()->session_live;
}

static int do_teardown(void *c, nvx_hwval_stage_record_t *r)
{
    (void)c;
    int rc = nvx_gsp_adapter_stop(g_io);
    const nvx_gsp_adapter_progress_t *p = prog();
    nvx_hwval_add_u64(r, "shutdown_status", p->shutdown_status);
    nvx_hwval_add_u64(r, "gsp_quiesce_result", p->quiesce_gsp.result);
    nvx_hwval_add_u64(r, "gsp_hwcfg2_before", p->quiesce_gsp.hwcfg2_before);
    nvx_hwval_add_u64(r, "gsp_hwcfg2_after", p->quiesce_gsp.hwcfg2_after);
    nvx_hwval_add_u64(r, "gsp_riscv_cpuctl_after", p->quiesce_gsp.riscv_cpuctl_after);
    nvx_hwval_add_u64(r, "sec2_quiesce_result", p->quiesce_sec2.result);
    nvx_hwval_add_u64(r, "sec2_cpuctl_after", p->quiesce_sec2.cpuctl_after);
    /* Scope: SEC2/GSP only. CE/GR engines and channels are not covered. */
    nvx_hwval_add_u64(r, "scope_sec2_gsp_only", 1U);
    if (rc != 0)
        r->reason = "quiescence not proven; DMA retained";
    return rc == 0;
}

int nvx_hwval_kernel_run(const nvx_bringup_io_t *io, const char *firmware_version)
{
    static nvx_hwval_report_t report;
    nvx_hwval_ops_t ops;
    char env[NVX_HWVAL_TEXT_MAX];

    g_io = io;
    if (!firmware_version || !firmware_version[0]) {
        io_text("NVXHW|error=firmware version required\n");
        return 1;
    }
    if (prog()->session_live) {
        io_text("NVXHW|error=a session already owns the device; run 'nvx gsp stop' first\n");
        return 1;
    }

    copy_text(env, sizeof(env), firmware_version);
    io_text("NVXHW|env|build=" VANTAOS_GIT_COMMIT "|firmware_version=");
    io_text(env);
    io_text("|chip=ga106|target=RTX3060\n");

    /* One coordinator transaction produces the evidence for stages 1-5. */
    (void)nvx_gsp_adapter_start(io, firmware_version);

    memset(&ops, 0, sizeof(ops));
    ops.stage[0] = st_discover;
    ops.stage[1] = st_pci_resources;
    ops.stage[2] = st_safe_init;
    ops.stage[3] = st_firmware;
    ops.stage[4] = st_gsp_rm;
    ops.not_implemented_reason[5] =
        "RM device creation is not wired to the hardware session; first independent RPC unvalidated";
    ops.not_implemented_reason[6] =
        "RM system memory/CTXDMA/NVOS46 lifecycle is host-tested only; no CPU mapping path";
    ops.not_implemented_reason[7] =
        "channel lifecycle host-tested; needs stage 7 mappings; USERD/doorbell ABI unaudited";
    ops.not_implemented_reason[8] = "CE submit/wait host-tested; needs a real channel from stage 8";
    ops.not_implemented_reason[9] = "needs a submitted operation from stage 9";
    ops.not_implemented_reason[10] =
        "CPU verifier ready (nvx_hwval_*); needs a GPU-written destination";
    ops.teardown = do_teardown;
    ops.hardware_touched = hw_touched;

    nvx_hwval_verdict_t v = nvx_hwval_run(&ops, NVX_HWVAL_SOURCE_HARDWARE, &report);
    nvx_hwval_emit_report(&report, emit_line, NULL);
    return v == NVX_HWVAL_VERDICT_HARDWARE_VERIFIED ? 0 : 1;
}
