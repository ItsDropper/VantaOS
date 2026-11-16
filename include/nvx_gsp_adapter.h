/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_ADAPTER_H
#define VANTA_NVX_GSP_ADAPTER_H

#include <stdint.h>

#include "nvx_bringup.h"
#include "nvx_gsp_falcon.h"
#include "nvx_mmio.h"

/*
 * Production platform adapter for the GA106 coordinator
 * (nvx_gsp_platform_start / nvx_gsp_platform_shutdown).
 *
 * It supplies the kernel services the coordinator deliberately does not own:
 * PCI discovery, BAR0 resource, VFS-backed manifest-verified firmware, a
 * scheduler-backed delay, the SEC2/GSP quiesce callback, and persistent
 * session storage. It never invents GPU/platform facts: the NVKV system-info
 * facts passed to the coordinator are empty unless the caller supplies them.
 *
 * Opt-in and manual: nothing here runs at boot.
 *
 * Handoff rule: before any state-changing access, the adapter performs a
 * read-only handoff check. It refuses to take over a device whose owner is
 * unknown (bus mastering already enabled, GSP RISC-V already active, or the
 * device not responding). There is no override: taking over an active device
 * needs a validated handoff procedure that does not exist yet.
 */

typedef enum nvx_gsp_handoff_result {
    NVX_GSP_HANDOFF_CLEAR = 0,
    NVX_GSP_HANDOFF_INVALID_ARGUMENT,
    NVX_GSP_HANDOFF_DEVICE_LOST,
    NVX_GSP_HANDOFF_BUS_MASTER_ACTIVE,
    NVX_GSP_HANDOFF_FIRMWARE_ACTIVE,
    NVX_GSP_HANDOFF_MMIO_ERROR
} nvx_gsp_handoff_result_t;

typedef struct nvx_gsp_handoff_report {
    nvx_gsp_handoff_result_t result;
    uint16_t pci_command;
    uint32_t hwcfg2;
    uint32_t cpuctl;
    uint32_t riscv_cpuctl;
    uint32_t sec2_cpuctl;
} nvx_gsp_handoff_report_t;

/* Read-only. pci_command is the PCI command register (bit 2 = bus master). */
nvx_gsp_handoff_result_t nvx_gsp_adapter_check_handoff(
    const nvx_mmio_region_t *bar0, uint16_t pci_command,
    nvx_gsp_handoff_report_t *out_report);

/*
 * Discover the RTX 3060, run the handoff check, then run the coordinator start
 * with the given exact firmware version. Returns 0 only if GSP-RM and the RPC
 * transport initialized. Prints structured stage evidence to io.
 * A failed start after hardware was touched keeps the session (and its DMA)
 * owned until nvx_gsp_adapter_stop() proves SEC2/GSP quiescence.
 */
int nvx_gsp_adapter_start(const nvx_bringup_io_t *io, const char *firmware_version);

/* Quiesce SEC2/GSP, then reclaim. Returns 0 only when reclaim was permitted. */
int nvx_gsp_adapter_stop(const nvx_bringup_io_t *io);

#endif
