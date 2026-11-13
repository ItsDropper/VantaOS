/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
/* Emits a survey log from a synthetic BAR0 so scripts/nvx_knowledge.py can be
 * exercised end to end. Mode "good" models a register file matching the
 * assumptions; "bad" contradicts several. No GPU is involved. */
#include "nvx_survey.h"
#include "nvx_survey_table.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void sink(const char *s, void *ctx) { (void)ctx; fputs(s, stdout); }

static uint32_t bar[0x900000 / 4];
static void setreg(uint32_t off, uint32_t v) { bar[off / 4] = v; }

int main(int argc, char **argv)
{
    int bad = argc > 1 && !strcmp(argv[1], "bad");
    nvx_bringup_io_t io = { sink, NULL };
    nvx_mmio_region_t region = { (volatile uint8_t *)bar, sizeof(bar) };

    setreg(0x000000, 0x172000A1U);
    setreg(0x000A00, bad ? 0x16500000U : 0x17600001U);   /* chip id 0x176 vs 0x165 */
    setreg(0x1183A4, bad ? 0U : 12288U);
    setreg(0x1FA7C8, 1U);
    setreg(0x1FA82C, 0xFFFFFFF0U);
    setreg(0x1FA830, 0U);
    setreg(0x625F04, 0xBADF3000U);                        /* observation only */
    setreg(0x300000, bad ? 0x12345678U : 0xAA55U);
    setreg(0x1100F4, bad ? 0xBADF4100U : 0x00000003U);
    setreg(0x110100, 0U); setreg(0x111388, 0U);
    setreg(0x8400F4, 0x00000003U); setreg(0x840100, 0U);
    setreg(0x824140, 2U); setreg(0x8241C0, 2U);

    assert(nvx_survey_run(NULL, 0x10DE, 0x2503, &io) == -1);
    assert(nvx_survey_run(&region, 0x10DE, 0x2503, &io) == (int)NVX_SURVEY_TABLE_COUNT);

    /* A short mapping reports per-register read errors instead of guessing. */
    nvx_mmio_region_t small = { (volatile uint8_t *)bar, 0x1000 };
    if (argc > 2 && !strcmp(argv[2], "short"))
        assert(nvx_survey_run(&small, 0x10DE, 0x2503, &io) == (int)NVX_SURVEY_TABLE_COUNT);
    return 0;
}
