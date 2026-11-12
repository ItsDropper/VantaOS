/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_BRINGUP_H
#define VANTA_NVX_BRINGUP_H

#include <stddef.h>
#include <stdint.h>
#include "nvx_firmware.h"

/*
 * Manual, opt-in NVX bring-up stages. Nothing here runs at boot: a person has
 * to type the shell command. Every stage prints what it did, stops at the first
 * failure, and never reports a stage it did not execute. A stage that returns 0
 * is software evidence for that stage only; see docs/gpu/nvx/BRINGUP.md for what
 * each stage does and does not prove.
 */
typedef struct nvx_bringup_io {
    void (*text)(const char *s, void *context);
    void *context;
} nvx_bringup_io_t;

/* Stage 1: find the RTX 3060, map BAR0, read-only identity probe. Returns 0 on success. */
int nvx_bringup_status(const nvx_bringup_io_t *io);

/*
 * Stage 1b: read the VBIOS through the GPU's PROM window (read-only) and locate
 * the FWSEC image inside it. Does not execute FWSEC. Returns 0 on success.
 */
int nvx_bringup_vbios(const nvx_bringup_io_t *io);

/*
 * Stage 2: acquire the five GA10x GSP firmware files through provider (which
 * must verify every file against a pinned manifest) and run structural
 * preflight. Does not touch the GPU. Returns 0 on success.
 */
int nvx_bringup_firmware(const nvx_bringup_io_t *io, const nvx_firmware_provider_t *provider,
                         const char *chip, const char *version);

/* VFS-backed provider rooted at "/" (paths like nvidia/ga106/gsp/gsp-<v>.bin).
 * verify() requires SHA256SUMS next to the file and fails closed without it. */
nvx_firmware_provider_t nvx_bringup_vfs_provider(void);

#endif
