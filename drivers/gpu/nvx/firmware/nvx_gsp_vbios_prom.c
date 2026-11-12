/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_vbios_prom.h"

#include <string.h>

/* Constants from NVIDIA pci_exp_table.h and tu102 dev_bus.h (open-gpu-kernel-modules). */
#define ROM_SIG_AA55 0xAA55U
#define ROM_SIG_NV   0x4E56U
#define ROM_SIG_NV2  0xBB77U
#define PCIR_SIG     0x52494350U
#define NPDS_SIG     0x5344504EU
#define RGIS_SIG     0x53494752U
#define NPDE_SIG     0x4544504EU
#define NPDE_REV_10  0x100U
#define NPDE_REV_11  0x101U
#define BLOCK        512U
#define IFR_SIG      0x4947564EU
#define ROM_DIR_SIG  0x44524652U            /* "RFRD" */
#define TYPE_BASE    0x00U
#define TYPE_EXT     0xE0U

typedef struct reader {
    nvx_vbios_prom_read32_fn read32;
    void *ctx;
    uint32_t base, max;
    int failed;
    nvx_gsp_vbios_prom_status_t status;
} reader_t;

/* s_romImgReadGeneric: unaligned 1/2/4-byte little-endian read built from aligned dwords. */
static uint32_t rd(reader_t *r, uint32_t off, unsigned n)
{
    if (r->failed) return 0U;
    uint32_t abs;
    if (off > UINT32_MAX - r->base) { r->failed = 1; r->status = NVX_VBIOS_PROM_BAD_OFFSET; return 0U; }
    abs = off + r->base;
    if (abs > UINT32_MAX - n || abs + n > r->max) {
        r->failed = 1; r->status = NVX_VBIOS_PROM_BAD_OFFSET; return 0U;
    }
    uint32_t idx = abs & 3U, aligned = abs - idx;
    uint8_t buf[8]; uint32_t w;
    if (!r->read32(r->ctx, aligned, &w)) { r->failed = 1; r->status = NVX_VBIOS_PROM_MMIO_FAILED; return 0U; }
    memcpy(buf, &w, 4);                      /* host is little-endian on x86-64 */
    if (idx + n > 4U) {
        if (!r->read32(r->ctx, aligned + 4U, &w)) { r->failed = 1; r->status = NVX_VBIOS_PROM_MMIO_FAILED; return 0U; }
        memcpy(buf + 4, &w, 4);
    }
    uint32_t v = 0U;
    for (unsigned i = n; i > 0U; i--) v = (v << 8) | buf[idx + i - 1U];
    return v;
}

static int valid_rom_sig(uint32_t s) { return s == ROM_SIG_AA55 || s == ROM_SIG_NV || s == ROM_SIG_NV2; }
static int valid_data_sig(uint32_t s) { return s == PCIR_SIG || s == NPDS_SIG || s == RGIS_SIG; }

/* kvbiosRomImgFindPciHeader_TU102 */
static uint32_t ifr_size(reader_t *r)
{
    uint32_t f0 = rd(r, 0, 4), f1 = rd(r, 4, 4), f2 = rd(r, 8, 4), image = 0U;
    if (r->failed) return 0U;
    if (f0 == IFR_SIG) {
        uint32_t ver = (f1 >> 8) & 0xFFU;
        if (ver == 1U || ver == 2U) {
            uint32_t ext = (f1 >> 16) & 0x7FFFU;
            image = rd(r, ext + 4U, 4);
        } else if (ver == 3U) {
            uint32_t total = f2 & 0xFFFFFU;
            uint32_t flash = rd(r, total, 4);
            uint32_t dir = flash + 4096U;
            if (rd(r, dir, 4) != ROM_DIR_SIG) { if (!r->failed) { r->failed = 1; r->status = NVX_VBIOS_PROM_BAD_IFR; } return 0U; }
            image = rd(r, dir + 8U, 4);
        } else {
            r->failed = 1; r->status = NVX_VBIOS_PROM_BAD_IFR; return 0U;
        }
    }
    if (!r->failed && (image & 3U)) { r->failed = 1; r->status = NVX_VBIOS_PROM_BAD_IFR; }
    return image;
}

/* s_locateExpansionRoms */
static void locate(reader_t *r, uint32_t pci_offset, uint32_t *bios_size, uint32_t *exp_off)
{
    uint32_t cur = pci_offset, ext_off = 0U, base_size = 0U, block_off = 0U, block_size = 0U;
    for (unsigned guard = 0; guard < 64U; guard++) {
        reader_t c = *r;
        c.base = cur;
        uint32_t ptr = rd(&c, 0x18U, 2);
        c.base = cur + ptr;
        uint32_t sig = rd(&c, 0U, 4);
        if (c.failed) { *r = c; r->base = pci_offset; return; }
        if (!valid_data_sig(sig)) { r->failed = 1; r->status = NVX_VBIOS_PROM_BAD_PCI_DATA; return; }
        int last = (rd(&c, 0x15U, 1) & 0x80U) != 0U;
        uint32_t img_len = rd(&c, 0x10U, 2), sub_len = img_len;
        uint32_t dlen = rd(&c, 0xAU, 2);
        uint32_t ext_at = (c.base + dlen + 0xFU) & ~0xFU;
        reader_t e = c; e.base = ext_at;
        if (rd(&e, 0U, 4) == NPDE_SIG) {
            uint32_t rev = rd(&e, 4U, 2);
            if (rev == NPDE_REV_10 || rev == NPDE_REV_11) {
                uint32_t elen = rd(&e, 6U, 2);
                sub_len = rd(&e, 8U, 2);
                if (0xAU + 1U <= elen) last = (rd(&e, 0xAU, 1) & 0x80U) != 0U;
                else if (sub_len < img_len) last = 0;
            }
        }
        uint32_t type = rd(&c, 0x14U, 1);
        if (c.failed || e.failed) { r->failed = 1; r->status = c.failed ? c.status : e.status; return; }
        block_off = cur - pci_offset;
        block_size = sub_len * BLOCK;
        if (ext_off == 0U && type == TYPE_EXT) ext_off = block_off;
        else if (base_size == 0U && type == TYPE_BASE) base_size = block_size;
        if (last) goto done;
        if (sub_len == 0U) { r->failed = 1; r->status = NVX_VBIOS_PROM_BAD_PCI_DATA; return; }
        cur += sub_len * BLOCK;
    }
    r->failed = 1; r->status = NVX_VBIOS_PROM_BAD_PCI_DATA; return;   /* runaway chain */
done:
    *bios_size = block_off + block_size;
    *exp_off = (ext_off > 0U && base_size > 0U) ? ext_off - base_size : 0U;
}

nvx_gsp_vbios_prom_status_t nvx_gsp_vbios_read_with(
    nvx_vbios_prom_read32_fn read32, void *context, void *buffer, size_t capacity,
    size_t *out_size, size_t *out_expansion_rom_offset)
{
    if (!read32 || !buffer || !out_size || !out_expansion_rom_offset)
        return NVX_VBIOS_PROM_INVALID_ARGUMENT;
    *out_size = 0U; *out_expansion_rom_offset = 0U;
    reader_t r = { read32, context, 0U, NVX_VBIOS_PROM_MAX_SIZE, 0, NVX_VBIOS_PROM_OK };

    uint32_t pci_offset = 0U;
    uint32_t sig = rd(&r, 0U, 2);
    if (r.failed) return r.status;
    if (!valid_rom_sig(sig)) {
        pci_offset = ifr_size(&r);
        if (r.failed) return r.status;
        r.base = pci_offset;
        sig = rd(&r, 0U, 2);
        if (r.failed) return r.status;
    }
    if (!valid_rom_sig(sig)) return NVX_VBIOS_PROM_NO_ROM_SIGNATURE;

    uint32_t bios_size = 0U, exp = 0U;
    locate(&r, pci_offset, &bios_size, &exp);
    if (r.failed) return r.status;
    if (bios_size > NVX_VBIOS_PROM_MAX_SIZE) return NVX_VBIOS_PROM_TOO_LARGE;
    if (bios_size > capacity) return NVX_VBIOS_PROM_BUFFER_TOO_SMALL;

    /* Copy: aligned dwords, then tail bytes (reference reads image from pciOffset). */
    uint8_t *dst = buffer;
    r.base = 0U; r.max = NVX_VBIOS_PROM_MAX_SIZE; r.failed = 0;
    uint32_t aligned_end = bios_size & ~3U, i = 0U;
    for (; i < aligned_end; i += 4U) {
        uint32_t w = rd(&r, pci_offset + i, 4);
        if (r.failed) return r.status;
        memcpy(dst + i, &w, 4);
    }
    for (; i < bios_size; i++) {
        dst[i] = (uint8_t)rd(&r, pci_offset + i, 1);
        if (r.failed) return r.status;
    }
    *out_size = bios_size;
    *out_expansion_rom_offset = exp;
    return NVX_VBIOS_PROM_OK;
}

static int bar0_read32(void *ctx, uint32_t offset, uint32_t *out)
{
    return nvx_mmio_read32((const nvx_mmio_region_t *)ctx,
                           (size_t)NVX_VBIOS_PROM_BASE + offset, out) == NVX_MMIO_OK;
}

nvx_gsp_vbios_prom_status_t nvx_gsp_vbios_read_from_prom(
    const nvx_mmio_region_t *bar0, void *buffer, size_t capacity,
    size_t *out_size, size_t *out_expansion_rom_offset)
{
    if (!bar0) return NVX_VBIOS_PROM_INVALID_ARGUMENT;
    return nvx_gsp_vbios_read_with(bar0_read32, (void *)bar0, buffer, capacity,
                                   out_size, out_expansion_rom_offset);
}
