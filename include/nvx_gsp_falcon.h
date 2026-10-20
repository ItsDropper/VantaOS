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
#define NVX_SEC2_FALCON_BASE            0x00840000U
#define NVX_SEC2_FALCON2_BASE           0x00841000U
#define NVX_GA10X_FALCON_BROM_MOD_SEL(base2)     ((base2) + 0x180U)
#define NVX_GA10X_FALCON_BROM_UCODE_ID(base2)    ((base2) + 0x198U)
#define NVX_GA10X_FALCON_BROM_ENGINE_MASK(base2) ((base2) + 0x19CU)
#define NVX_GA10X_FALCON_BROM_PARAM_ADDR(base2)  ((base2) + 0x210U)
#define NVX_GA10X_BAR0_REQUIRED_BYTES (NVX_GA10X_FALCON_BROM_PARAM_ADDR(NVX_SEC2_FALCON2_BASE) + 4U)
#define NVX_GSP_FALCON_MAILBOX0          (NVX_GSP_FALCON_BASE + 0x040U)
#define NVX_GSP_FALCON_MAILBOX1          (NVX_GSP_FALCON_BASE + 0x044U)
#define NVX_GSP_FALCON_HWCFG2            (NVX_GSP_FALCON_BASE + 0x0F4U)
#define NVX_GSP_FALCON_CPUCTL            (NVX_GSP_FALCON_BASE + 0x100U)
#define NVX_GSP_FALCON_BOOTVEC           (NVX_GSP_FALCON_BASE + 0x104U)
#define NVX_GSP_RISCV_CPUCTL             (NVX_GSP_FALCON2_BASE + 0x388U)
#define NVX_GSP_FALCON_BROM_MOD_SEL       (NVX_GSP_FALCON2_BASE + 0x180U)
#define NVX_GSP_FALCON_BROM_UCODE_ID      (NVX_GSP_FALCON2_BASE + 0x198U)
#define NVX_GSP_FALCON_BROM_ENGINE_MASK   (NVX_GSP_FALCON2_BASE + 0x19CU)
#define NVX_GSP_FALCON_BROM_PARAM_ADDR    (NVX_GSP_FALCON2_BASE + 0x210U)
#define NVX_GSP_FALCON_BROM_RSA3K         1U
#define NVX_GSP_FALCON_BROM_UCODE_COUNT   16U
#define NVX_GSP_FALCON_BROM_REQUIRED_BYTES (NVX_GSP_FALCON_BROM_PARAM_ADDR + 4U)
#define NVX_GA10X_FALCON_DMACTL(base)       ((base) + 0x10CU)
#define NVX_GA10X_FALCON_DMATRFBASE(base)   ((base) + 0x110U)
#define NVX_GA10X_FALCON_DMATRFMOFFS(base)  ((base) + 0x114U)
#define NVX_GA10X_FALCON_DMATRFCMD(base)    ((base) + 0x118U)
#define NVX_GA10X_FALCON_DMATRFFBOFFS(base) ((base) + 0x11CU)
#define NVX_GA10X_FALCON_DMATRFBASE1(base)  ((base) + 0x128U)
#define NVX_GA10X_FALCON_FBIF_TRANSCFG0(base) ((base) + 0x600U)
#define NVX_GA10X_FALCON_FBIF_CTL(base)     ((base) + 0x624U)
#define NVX_GA10X_FALCON_ENGINE(base)       ((base) + 0x3C0U)
#define NVX_GA10X_FALCON_DMA_FULL           (1U << 0U)
#define NVX_GA10X_FALCON_DMA_IDLE           (1U << 1U)
#define NVX_GA10X_FALCON_DMA_IMEM           (1U << 4U)
#define NVX_GA10X_FALCON_DMA_WRITE          (1U << 5U)
#define NVX_GA10X_FALCON_DMA_SIZE_256B      (6U << 8U)
#define NVX_GA10X_FALCON_DMA_SET_DMTAG      (1U << 16U)
#define NVX_GA10X_FALCON_DMA_ALLOW_PHYS     (1U << 7U)
#define NVX_GA10X_FALCON_DMA_COHERENT_PHYS  5U
#define NVX_GA10X_FALCON_DMA_CHUNK_BYTES    256U
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
    NVX_GSP_FALCON_RISCV_TIMEOUT,
    NVX_GSP_FALCON_BROM_INVALID_PARAMETERS,
    NVX_GSP_FALCON_NOT_HALTED,
    NVX_GSP_FALCON_BOOT_VECTOR_INVALID,
    NVX_GSP_FALCON_DMA_INVALID_TRANSFER,
    NVX_GSP_FALCON_DMA_TIMEOUT,
    NVX_GSP_FALCON_RESET_TIMEOUT
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

/*
 * Program GA102+ Falcon Boot ROM verification parameters for a validated
 * firmware descriptor. This does not load firmware or start SEC2/FWSEC/GSP.
 * The Falcon must be halted and its PRIV window accessible. engine_id_mask
 * must identify exactly one documented engine; ucode_id is 1-based.
 */
nvx_gsp_falcon_status_t nvx_gsp_ga10x_program_brom(
    const nvx_mmio_region_t *bar0, uint32_t engine_id_mask,
    uint32_t ucode_id, uint32_t pkc_data_offset);
/*
 * Engine-aware forms cover GA102 GSP and SEC2. Pass the matching documented
 * engine/base pair; all addresses are BAR0-relative.
 */
nvx_gsp_falcon_status_t nvx_ga10x_falcon_program_brom(
    const nvx_mmio_region_t *bar0, uint32_t falcon_base,
    uint32_t falcon2_base, uint32_t engine_id_mask,
    uint32_t ucode_id, uint32_t pkc_data_offset);
nvx_gsp_falcon_status_t nvx_ga10x_falcon_start(
    const nvx_mmio_region_t *bar0, uint32_t falcon_base,
    uint32_t boot_vector);
/* GSP-specific wrappers retained for current callers. */
nvx_gsp_falcon_status_t nvx_gsp_ga10x_start_falcon(
    const nvx_mmio_region_t *bar0, uint32_t boot_vector);
typedef enum nvx_ga10x_falcon_memory {
    NVX_GA10X_FALCON_SECURE_IMEM = 0,
    NVX_GA10X_FALCON_NONSECURE_IMEM,
    NVX_GA10X_FALCON_DMEM
} nvx_ga10x_falcon_memory_t;
/*
 * DMA a 256-byte-aligned SYSRAM span into Falcon IMEM/DMEM. Caller must have
 * reset the engine and prepared a coherent, physically contiguous DMA buffer.
 * Timeout/poll interval are milliseconds; delay callback must yield/sleep.
 */
/* Reset the engine, pulse reset for at least 1 ms, then wait for scrub completion. */
nvx_gsp_falcon_status_t nvx_ga10x_falcon_reset(
    const nvx_mmio_region_t *bar0, uint32_t falcon_base,
    uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_delay_ms_fn delay_ms, void *context);
nvx_gsp_falcon_status_t nvx_ga10x_falcon_dma_upload(
    const nvx_mmio_region_t *bar0, uint32_t falcon_base,
    uint64_t source_phys, uint32_t source_offset,
    uint32_t destination_offset, uint32_t length,
    nvx_ga10x_falcon_memory_t target, uint8_t set_dm_tag,
    uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_delay_ms_fn delay_ms, void *context);

#endif
