/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_FALCON_H
#define VANTA_NVX_GSP_FALCON_H

#include <stddef.h>
#include <stdint.h>
#include "nvx_mmio.h"

/*
 * GA102+ GSP Falcon diagnostic registers. Offsets are BAR0-relative:
 * PFalconBase = 0x110000 and PFalcon2Base = 0x111000.
 * Inspection is read-only. It never resets a Falcon, writes a mailbox, starts
 * firmware, or infers successful boot merely from a readable register.
 */
#define NVX_GSP_FALCON_BASE             0x00110000U
#define NVX_GSP_FALCON2_BASE            0x00111000U
#define NVX_GSP_FALCON_MAILBOX0          (NVX_GSP_FALCON_BASE + 0x040U)
#define NVX_GSP_FALCON_MAILBOX1          (NVX_GSP_FALCON_BASE + 0x044U)
#define NVX_GSP_FALCON_HWCFG2            (NVX_GSP_FALCON_BASE + 0x0F4U)
#define NVX_GSP_FALCON_CPUCTL            (NVX_GSP_FALCON_BASE + 0x100U)
#define NVX_GSP_RISCV_CPUCTL             (NVX_GSP_FALCON2_BASE + 0x388U)
#define NVX_GSP_FALCON_DIAGNOSTIC_BYTES  (NVX_GSP_RISCV_CPUCTL + 4U)

#define NVX_GSP_HWCFG2_RESET_READY       (1U << 31)
#define NVX_GSP_HWCFG2_MEMORY_SCRUBBING  (1U << 12)
#define NVX_GSP_HWCFG2_RISCV_PRESENT     (1U << 10)
#define NVX_GSP_CPUCTL_HALTED             (1U << 4)
#define NVX_GSP_CPUCTL_START              (1U << 1)
#define NVX_GSP_RISCV_ACTIVE              (1U << 7)
#define NVX_GSP_RISCV_HALTED              (1U << 4)
#define NVX_GSP_MAILBOX0_SUSPENDED        (1U << 31)

typedef enum nvx_gsp_falcon_status {
    NVX_GSP_FALCON_OK = 0,
    NVX_GSP_FALCON_INVALID_ARGUMENT,
    NVX_GSP_FALCON_BAR_TOO_SMALL,
    NVX_GSP_FALCON_MMIO_ERROR,
    NVX_GSP_FALCON_PRIVILEGED_WINDOW_LOCKED,
    NVX_GSP_FALCON_RISCV_TIMEOUT
} nvx_gsp_falcon_status_t;

typedef struct nvx_gsp_falcon_snapshot {
    uint32_t mailbox0;
    uint32_t mailbox1;
    uint32_t hwcfg2;
    uint32_t cpuctl;
    uint32_t riscv_cpuctl;
    uint8_t reset_ready;
    uint8_t memory_scrubbing;
    uint8_t riscv_present;
    uint8_t falcon_halted;
    uint8_t falcon_start_requested;
    uint8_t riscv_active;
    uint8_t riscv_halted;
    uint8_t mailbox_suspended;
    uint8_t snapshot_valid;
} nvx_gsp_falcon_snapshot_t;

/* Output is zeroed on failure. This routine performs only bounded MMIO reads. */
nvx_gsp_falcon_status_t nvx_gsp_ga10x_inspect_falcon(
    const nvx_mmio_region_t *bar0,
    nvx_gsp_falcon_snapshot_t *out_snapshot);

/*
 * Wait for the GA102+ RISC-V active bit using a caller-supplied scheduler delay.
 * Both timeout and polling interval are in milliseconds; no busy-spin is used.
 * The callback must sleep/yield for at least the requested interval.
 */
typedef void (*nvx_gsp_delay_ms_fn)(uint32_t milliseconds, void *context);
nvx_gsp_falcon_status_t nvx_gsp_ga10x_wait_riscv_active(
    const nvx_mmio_region_t *bar0, uint32_t timeout_ms,
    uint32_t poll_interval_ms, nvx_gsp_delay_ms_fn delay_ms, void *context);

#endif
