/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_SURVEY_H
#define VANTA_NVX_SURVEY_H

#include <stdint.h>
#include "nvx_bringup.h"
#include "nvx_mmio.h"

/*
 * Read-only register survey: the controlled experiment that lets a run on real
 * hardware establish (or contradict) facts in docs/gpu/nvx/knowledge/facts.tsv.
 * It only reads the offsets in the generated NVX_SURVEY_TABLE, never writes, and
 * prints one machine-parseable line per observation:
 *
 *   NVXSURVEY begin pci=10de:2503
 *   NVXOBS id=NVX-F-0001 off=0x00000000 val=0x172000A1
 *   NVXOBS id=NVX-F-0030 off=0x001100F4 error=read-failed
 *   NVXSURVEY end count=16
 *
 * scripts/nvx_knowledge.py analyze consumes that output. Returns the number of
 * observations emitted, or -1 on invalid arguments.
 */
int nvx_survey_run(const nvx_mmio_region_t *bar0, uint16_t vendor_id, uint16_t device_id,
                   const nvx_bringup_io_t *io);

#endif
