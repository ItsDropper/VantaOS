/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_HWVAL_KERNEL_H
#define VANTA_NVX_HWVAL_KERNEL_H

#include "nvx_bringup.h"

/*
 * Kernel runner for the 12-stage RTX 3060 hardware validation sequence
 * (see nvx_hwval.h for the evidence policy). Manual and opt-in: `nvx hwval <ver>`.
 *
 * Stages 1-5 are produced by ONE coordinator transaction (nvx_gsp_adapter_start);
 * their per-stage verdicts are derived from the adapter's recorded progress, not
 * inferred. Stage 12 runs nvx_gsp_adapter_stop() whenever hardware may own DMA.
 * Stages 6-11 are reported NOT_IMPLEMENTED until the RM-device, GPU-memory and
 * copy-channel paths are composed into the hardware session; the runner never
 * substitutes mock results for them.
 *
 * Output lines are prefixed "NVXHW|" for capture from the serial log. Returns 0
 * only for verdict HARDWARE_VERIFIED.
 */
int nvx_hwval_kernel_run(const nvx_bringup_io_t *io, const char *firmware_version);

#endif
