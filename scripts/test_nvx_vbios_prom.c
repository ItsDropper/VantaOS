/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
/* Synthetic-PROM tests for the NVIDIA-derived VBIOS extraction logic. Software
 * evidence only: real boards may expose the PROM differently. */
#include "nvx_gsp_vbios_prom.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct { const uint8_t *d; size_t n; unsigned reads; int fail_at; } rom_t;

static int rd32(void *c, uint32_t off, uint32_t *out)
{
    rom_t *r = c;
    assert((off & 3U) == 0U);                 /* only aligned dword reads are allowed */
    r->reads++;
    if (r->fail_at >= 0 && (int)off >= r->fail_at) return 0;
    uint32_t v = 0U;
    for (int i = 3; i >= 0; i--) v = (v << 8) | (off + (uint32_t)i < r->n ? r->d[off + (uint32_t)i] : 0U);
    *out = v; return 1;
}

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }

/* One ROM image of `blocks` 512-byte blocks at o. */
static void image(uint8_t *rom, uint32_t o, uint32_t blocks, uint8_t type, int last, int npde)
{
    put16(rom + o, 0xAA55U); put16(rom + o + 0x18, 0x40U);
    put32(rom + o + 0x40, 0x52494350U); put16(rom + o + 0x4A, 0x18U);
    put16(rom + o + 0x50, blocks); rom[o + 0x54] = type; rom[o + 0x55] = last ? 0x80 : 0;
    memset(rom + o + 0x100, (int)(0x10 + type), blocks * 512U - 0x100U);   /* payload marker */
    if (npde) {
        put32(rom + o + 0x60, 0x4544504EU); put16(rom + o + 0x64, 0x101U);
        put16(rom + o + 0x66, 0xBU); put16(rom + o + 0x68, blocks); rom[o + 0x6A] = last ? 0x80 : 0;
    }
}

static uint8_t rom[0x20000], out[0x20000];

static void chain(uint32_t at)
{
    image(rom, at,          2U, 0x00, 0, 0);   /* base,   1024 */
    image(rom, at + 1024U,  1U, 0x03, 0, 0);   /* EFI,     512 */
    image(rom, at + 1536U,  3U, 0xE0, 1, 1);   /* ext,    1536, last via NPDE */
}

int main(void)
{
    size_t size = 0, exp = 0;
    rom_t r = { rom, sizeof(rom), 0, -1 };

    /* Plain ROM at offset 0. Expansion offset = ext(1536) - base(1024). */
    memset(rom, 0, sizeof(rom)); chain(0);
    assert(nvx_gsp_vbios_read_with(rd32, &r, out, sizeof(out), &size, &exp) == NVX_VBIOS_PROM_OK);
    assert(size == 1536U + 3U * 512U && exp == 512U);
    assert(!memcmp(out, rom, size));

    /* IFR v3 wrapper: image starts where the ROM directory says. */
    memset(rom, 0, sizeof(rom));
    put32(rom + 0, 0x4947564EU); put32(rom + 4, (3U << 8) | (0x10U << 16)); put32(rom + 8, 0x40U);
    put32(rom + 0x40, 0x1000U);                         /* flash status offset */
    put32(rom + 0x1000 + 4096U, 0x44524652U);           /* "RFRD" at flash + 4096 */
    put32(rom + 0x1000 + 4096U + 8U, 0x4000U);          /* image offset */
    chain(0x4000);
    r.d = rom;
    assert(nvx_gsp_vbios_read_with(rd32, &r, out, sizeof(out), &size, &exp) == NVX_VBIOS_PROM_OK);
    assert(size == 3072U && exp == 512U && !memcmp(out, rom + 0x4000, size));

    /* IFR v1: image offset stored at FIXED_DATA_SIZE + 4. */
    memset(rom, 0, sizeof(rom));
    put32(rom + 0, 0x4947564EU); put32(rom + 4, (1U << 8) | (0x20U << 16));
    put32(rom + 0x20 + 4U, 0x2000U); chain(0x2000);
    assert(nvx_gsp_vbios_read_with(rd32, &r, out, sizeof(out), &size, &exp) == NVX_VBIOS_PROM_OK);
    assert(size == 3072U && !memcmp(out, rom + 0x2000, size));

    /* Failure modes fail closed. */
    memset(rom, 0, sizeof(rom)); chain(0);
    assert(nvx_gsp_vbios_read_with(rd32, &r, out, 1024U, &size, &exp) == NVX_VBIOS_PROM_BUFFER_TOO_SMALL);
    memset(rom, 0xFF, sizeof(rom));                    /* PRI-error style garbage */
    assert(nvx_gsp_vbios_read_with(rd32, &r, out, sizeof(out), &size, &exp) != NVX_VBIOS_PROM_OK);
    memset(rom, 0, sizeof(rom));
    assert(nvx_gsp_vbios_read_with(rd32, &r, out, sizeof(out), &size, &exp) == NVX_VBIOS_PROM_NO_ROM_SIGNATURE);
    memset(rom, 0, sizeof(rom)); put32(rom, 0x4947564EU); put32(rom + 4, 9U << 8);
    assert(nvx_gsp_vbios_read_with(rd32, &r, out, sizeof(out), &size, &exp) == NVX_VBIOS_PROM_BAD_IFR);
    memset(rom, 0, sizeof(rom)); chain(0); rom[0x55] = 0;  /* base marked not-last */
    memset(rom + 1536U, 0, 512U);                          /* ...next chain link destroyed */
    assert(nvx_gsp_vbios_read_with(rd32, &r, out, sizeof(out), &size, &exp) != NVX_VBIOS_PROM_OK);
    memset(rom, 0, sizeof(rom)); chain(0); r.fail_at = 0x400;
    assert(nvx_gsp_vbios_read_with(rd32, &r, out, sizeof(out), &size, &exp) == NVX_VBIOS_PROM_MMIO_FAILED);
    r.fail_at = -1;
    assert(nvx_gsp_vbios_read_with(NULL, &r, out, sizeof(out), &size, &exp) == NVX_VBIOS_PROM_INVALID_ARGUMENT);

    /* BAR0 wrapper reads at 0x300000 and rejects a mapping that is too small. */
    static uint8_t bar[0x300000 + 0x2000];
    memset(bar, 0, sizeof(bar));
    memset(rom, 0, sizeof(rom)); chain(0);
    memcpy(bar + 0x300000, rom, 0x1800);
    nvx_mmio_region_t reg = { bar, sizeof(bar) };
    assert(nvx_gsp_vbios_read_from_prom(&reg, out, sizeof(out), &size, &exp) == NVX_VBIOS_PROM_OK);
    assert(size == 3072U && exp == 512U);
    nvx_mmio_region_t tiny = { bar, 0x1000 };
    assert(nvx_gsp_vbios_read_from_prom(&tiny, out, sizeof(out), &size, &exp) == NVX_VBIOS_PROM_MMIO_FAILED);
    return 0;
}
