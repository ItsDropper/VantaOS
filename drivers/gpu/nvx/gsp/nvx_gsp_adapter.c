/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_adapter.h"

#include <string.h>

#include "nvx_gsp_platform.h"
#include "nvx_gsp_quiesce.h"
#include "nvx_resource.h"
#include "pci.h"

#ifndef NVX_GSP_ADAPTER_HOST_TEST
#include "timer.h"
#endif

#define PCI_COMMAND_OFFSET      0x04U
#define PCI_COMMAND_BUS_MASTER  (1U << 2)
#define HANDOFF_ALL_ONES        0xFFFFFFFFU
/* Queue sizes: a page-aligned size accepted by the coordinator preflight. */
#define ADAPTER_QUEUE_BYTES     (64U * 1024U)
#define ADAPTER_TIMEOUT_MS      2000U
#define ADAPTER_POLL_MS         5U

nvx_gsp_handoff_result_t nvx_gsp_adapter_check_handoff(
    const nvx_mmio_region_t *bar0, uint16_t pci_command,
    nvx_gsp_handoff_report_t *out)
{
    nvx_gsp_handoff_report_t local;

    if (!out)
        return NVX_GSP_HANDOFF_INVALID_ARGUMENT;
    memset(&local, 0, sizeof(local));
    local.pci_command = pci_command;

    if (!bar0 || !bar0->base || bar0->length < NVX_GA10X_BAR0_REQUIRED_BYTES) {
        local.result = NVX_GSP_HANDOFF_INVALID_ARGUMENT;
        goto done;
    }
    if (nvx_mmio_read32(bar0, NVX_GSP_FALCON_HWCFG2, &local.hwcfg2) != NVX_MMIO_OK ||
        nvx_mmio_read32(bar0, NVX_GSP_FALCON_CPUCTL, &local.cpuctl) != NVX_MMIO_OK ||
        nvx_mmio_read32(bar0, NVX_GSP_RISCV_CPUCTL, &local.riscv_cpuctl) != NVX_MMIO_OK ||
        nvx_mmio_read32(bar0, NVX_SEC2_FALCON_BASE + 0x100U, &local.sec2_cpuctl) != NVX_MMIO_OK) {
        local.result = NVX_GSP_HANDOFF_MMIO_ERROR;
        goto done;
    }
    if (local.hwcfg2 == HANDOFF_ALL_ONES || local.riscv_cpuctl == HANDOFF_ALL_ONES) {
        local.result = NVX_GSP_HANDOFF_DEVICE_LOST;
        goto done;
    }
    /* Unknown DMA owner: refuse rather than adopt an active device. */
    if (pci_command & PCI_COMMAND_BUS_MASTER) {
        local.result = NVX_GSP_HANDOFF_BUS_MASTER_ACTIVE;
        goto done;
    }
    if ((local.riscv_cpuctl & NVX_GSP_RISCV_ACTIVE) != 0U ||
        (local.cpuctl & NVX_GSP_CPUCTL_START) != 0U ||
        (local.sec2_cpuctl & NVX_GSP_CPUCTL_START) != 0U) {
        local.result = NVX_GSP_HANDOFF_FIRMWARE_ACTIVE;
        goto done;
    }
    local.result = NVX_GSP_HANDOFF_CLEAR;
done:
    *out = local;
    return local.result;
}

#ifndef NVX_GSP_ADAPTER_HOST_TEST

static nvx_gsp_platform_session_t g_session;      /* long-lived; one per function */
static nvx_gsp_quiesce_context_t g_quiesce;
static nvx_firmware_provider_t g_provider;
static nvx_gsp_system_info_t g_facts;             /* intentionally empty */
static uint8_t g_session_live;

static void adapter_delay(uint32_t ms, void *ctx)
{
    (void)ctx;
    sleep_ms(ms);
}

static void say(const nvx_bringup_io_t *io, const char *s)
{
    if (io && io->text)
        io->text(s, io->context);
}

static void say_hex(const nvx_bringup_io_t *io, const char *label, uint64_t v)
{
    static const char d[] = "0123456789abcdef";
    char b[24];
    size_t n = 0U;
    int started = 0;

    b[n++] = '0';
    b[n++] = 'x';
    for (int shift = 60; shift >= 0; shift -= 4) {
        unsigned nib = (unsigned)((v >> shift) & 0xFU);
        if (nib || started || shift == 0) {
            b[n++] = d[nib];
            started = 1;
        }
    }
    b[n++] = '\n';
    b[n] = '\0';
    say(io, label);
    say(io, b);
}

static const struct pci_device *find_gpu(void)
{
    for (int i = 0, n = pci_get_device_count(); i < n; i++) {
        const struct pci_device *d = pci_get_device(i);
        if (d && nvx_match_pci_device(d))
            return d;
    }
    return NULL;
}

int nvx_gsp_adapter_start(const nvx_bringup_io_t *io, const char *firmware_version)
{
    const struct pci_device *pci;
    nvx_bar_resource_t bar0_res;
    void *mapping = NULL;
    nvx_gsp_handoff_report_t handoff;
    nvx_gsp_platform_config_t config;
    nvx_gsp_platform_status_t status;

    if (!firmware_version || !firmware_version[0]) {
        say(io, "nvx gsp: firmware version required.\n");
        return 1;
    }
    if (g_session_live) {
        say(io, "nvx gsp: a session already owns the device; run 'nvx gsp stop' first.\n");
        return 1;
    }
    if (!(pci = find_gpu())) {
        say(io, "nvx gsp: no supported RTX 3060 found on PCI.\n");
        return 1;
    }
    say_hex(io, "nvx gsp: stage 1 PCI device id ", pci->device_id);

    if (nvx_bar_resource_read(pci, 0U, &bar0_res) != NVX_RESOURCE_OK ||
        nvx_bar_resource_validate(&bar0_res) != NVX_RESOURCE_OK) {
        say(io, "nvx gsp: stage 2 BAR0 invalid or size unknown; refusing.\n");
        return 1;
    }
    say_hex(io, "nvx gsp: stage 2 BAR0 base ", bar0_res.base);
    say_hex(io, "nvx gsp: stage 2 BAR0 size ", bar0_res.size);

    /* Read-only handoff check through a temporary mapping. */
    if (nvx_bar_mmio_map(&bar0_res, &mapping) != NVX_RESOURCE_OK || !mapping) {
        say(io, "nvx gsp: stage 2 BAR0 map failed.\n");
        return 1;
    }
    {
        nvx_mmio_region_t region = { (volatile uint8_t *)mapping, (size_t)bar0_res.size };
        (void)nvx_gsp_adapter_check_handoff(
            &region, pci_read_config16(pci, PCI_COMMAND_OFFSET), &handoff);
    }
    nvx_bar_mmio_unmap(&bar0_res, mapping);
    say_hex(io, "nvx gsp: stage 3 handoff result ", (uint64_t)handoff.result);
    say_hex(io, "nvx gsp: stage 3 pci command   ", handoff.pci_command);
    say_hex(io, "nvx gsp: stage 3 gsp hwcfg2    ", handoff.hwcfg2);
    say_hex(io, "nvx gsp: stage 3 gsp cpuctl    ", handoff.cpuctl);
    say_hex(io, "nvx gsp: stage 3 riscv cpuctl  ", handoff.riscv_cpuctl);
    say_hex(io, "nvx gsp: stage 3 sec2 cpuctl   ", handoff.sec2_cpuctl);
    if (handoff.result != NVX_GSP_HANDOFF_CLEAR) {
        say(io, "nvx gsp: device state not safe to take over; nothing was changed.\n");
        return 1;
    }

    memset(&g_session, 0, sizeof(g_session));
    memset(&g_quiesce, 0, sizeof(g_quiesce));
    memset(&g_facts, 0, sizeof(g_facts));
    g_provider = nvx_bringup_vfs_provider();

    memset(&config, 0, sizeof(config));
    config.pci_device = pci;
    config.trusted_bar0 = &bar0_res;
    config.firmware_provider = &g_provider;
    config.firmware_chip = "ga106";
    config.firmware_version = firmware_version;
    config.trusted_gpu_facts = &g_facts;
    config.command_queue_bytes = ADAPTER_QUEUE_BYTES;
    config.status_queue_bytes = ADAPTER_QUEUE_BYTES;
    config.timeout_ms = ADAPTER_TIMEOUT_MS;
    config.poll_interval_ms = ADAPTER_POLL_MS;
    config.delay_ms = adapter_delay;

    status = nvx_gsp_platform_validate_config(&config);
    if (status != NVX_GSP_PLATFORM_OK) {
        say_hex(io, "nvx gsp: stage 4 config rejected, status ", (uint64_t)status);
        return 1;
    }

    status = nvx_gsp_platform_start(&config, &g_session);
    say_hex(io, "nvx gsp: stage 5 coordinator status ", (uint64_t)status);
    if (g_session.startup_attempted || g_session.bus_master_enabled)
        g_session_live = 1U;   /* hardware may own DMA: stop must prove quiescence */
    if (status != NVX_GSP_PLATFORM_OK) {
        say(io, g_session_live
            ? "nvx gsp: start failed after hardware was touched; DMA retained. Run 'nvx gsp stop'.\n"
            : "nvx gsp: start failed before hardware was touched; resources unwound.\n");
        return 1;
    }
    g_session_live = 1U;
    say(io, "nvx gsp: GSP-RM and RPC transport initialized (NOT hardware-verified until a "
            "logged run on a physical RTX 3060 exists).\n");
    return 0;
}

int nvx_gsp_adapter_stop(const nvx_bringup_io_t *io)
{
    nvx_gsp_platform_status_t status;

    if (!g_session_live) {
        say(io, "nvx gsp: no session to stop.\n");
        return 1;
    }
    memset(&g_quiesce, 0, sizeof(g_quiesce));
    g_quiesce.delay_ms = adapter_delay;
    g_quiesce.timeout_ms = ADAPTER_TIMEOUT_MS;
    g_quiesce.poll_interval_ms = ADAPTER_POLL_MS;

    status = nvx_gsp_platform_shutdown(&g_session, nvx_gsp_ga10x_quiesce, &g_quiesce);
    say_hex(io, "nvx gsp: gsp quiesce result  ", (uint64_t)g_quiesce.gsp.result);
    say_hex(io, "nvx gsp: sec2 quiesce result ", (uint64_t)g_quiesce.sec2.result);
    say_hex(io, "nvx gsp: shutdown status     ", (uint64_t)status);
    if (status != NVX_GSP_PLATFORM_OK) {
        say(io, "nvx gsp: quiescence not proven; DMA memory retained.\n");
        return 1;
    }
    g_session_live = 0U;
    say(io, "nvx gsp: SEC2/GSP quiesced and resources reclaimed.\n");
    return 0;
}

#endif /* NVX_GSP_ADAPTER_HOST_TEST */
