/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_vbios.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define IMAGE_BYTES 0x1000U
#define BIT_OFFSET 0x20U
#define EXPANSION_OFFSET 0x100U
#define TABLE_OFFSET (EXPANSION_OFFSET + 0x40U)
#define DESC_OFFSET (EXPANSION_OFFSET + 0x80U)
#define IMAGE_OFFSET (DESC_OFFSET + 44U + NVX_GSP_VBIOS_RSA3K_SIGNATURE_SIZE)

static void put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8U);
}

static void put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8U);
    p[2] = (uint8_t)(value >> 16U);
    p[3] = (uint8_t)(value >> 24U);
}

static void make_image(uint8_t image[IMAGE_BYTES])
{
    uint8_t *bit = image + BIT_OFFSET;
    uint8_t *token = bit + 12U;
    uint8_t *table = image + TABLE_OFFSET;
    uint8_t *entry = table + 6U;
    uint8_t *desc = image + DESC_OFFSET;
    uint8_t checksum = 0U;
    size_t i;

    memset(image, 0, IMAGE_BYTES);

    put16(bit, 0xB8FFU);
    put32(bit + 2U, 0x00544942U);
    put16(bit + 6U, 0x0100U);
    bit[8U] = 12U;
    bit[9U] = 8U;
    bit[10U] = 1U;
    for (i = 0U; i < 11U; ++i)
        checksum = (uint8_t)(checksum + bit[i]);
    bit[11U] = (uint8_t)(0U - checksum);

    token[0U] = 0x70U;
    token[1U] = 2U;
    put16(token + 2U, 4U);
    put32(token + 4U, 0x80U);
    put32(image + 0x80U, 0x40U);

    table[0U] = 1U;
    table[1U] = 6U;
    table[2U] = 6U;
    table[3U] = 1U;
    entry[0U] = NVX_GSP_VBIOS_FWSEC_APP_PRODUCTION;
    entry[1U] = 0U;
    put32(entry + 2U, 0x80U);

    put32(desc, ((44U + NVX_GSP_VBIOS_RSA3K_SIGNATURE_SIZE) << 16U) |
                (3U << 8U) | 1U);
    put32(desc + 4U, 0x300U);
    put32(desc + 8U, 0x20U);
    put32(desc + 12U, 0x30U);
    put32(desc + 16U, 0x100U);
    put32(desc + 20U, 0x100U);
    put32(desc + 24U, 0x200U);
    put32(desc + 28U, 0x300U);
    put32(desc + 32U, 0x200U);
    put16(desc + 36U, 0x0400U);
    desc[38U] = 2U;
    desc[39U] = 1U;
    put16(desc + 40U, 1U);
    put16(desc + 42U, 0U);

    assert(IMAGE_OFFSET + 0x300U <= IMAGE_BYTES);
}

int main(void)
{
    uint8_t image[IMAGE_BYTES];
    nvx_gsp_vbios_fwsec_info_t info;
    nvx_gsp_vbios_status_t status;
    uint32_t signature_offset = UINT32_MAX;

    make_image(image);
    memset(&info, 0xA5, sizeof(info));
    status = nvx_gsp_vbios_find_ga10x_fwsec(
        image, sizeof(image), EXPANSION_OFFSET, &info);
    assert(status == NVX_GSP_VBIOS_OK);
    assert(info.bit_header_offset == BIT_OFFSET);
    assert(info.falcon_table_offset == TABLE_OFFSET);
    assert(info.descriptor_offset == DESC_OFFSET);
    assert(info.descriptor_size == 44U + NVX_GSP_VBIOS_RSA3K_SIGNATURE_SIZE);
    assert(info.image_offset == IMAGE_OFFSET);
    assert(info.image_size == 0x300U);
    assert(info.imem_load_size == 0x100U);
    assert(info.dmem_load_size == 0x200U);
    assert(info.pkc_data_offset == 0x20U);
    assert(info.engine_id_mask == NVX_GSP_VBIOS_FWSEC_ENGINE_MASK);
    assert(info.ucode_id == 2U);
    assert(info.signature_count == 1U);
    assert(info.signatures_offset == DESC_OFFSET + 44U);
    assert(info.signatures_size == NVX_GSP_VBIOS_RSA3K_SIGNATURE_SIZE);
    assert(info.descriptor_version == NVX_GSP_VBIOS_FWSEC_DESC_V3);
    assert(info.encrypted == 0U);
    assert(nvx_gsp_vbios_fwsec_signature_offset(
        &info, 0U, &signature_offset) == NVX_GSP_VBIOS_OK);
    assert(signature_offset == DESC_OFFSET + 44U);
    assert(nvx_gsp_vbios_fwsec_signature_offset(
        &info, 1U, &signature_offset) ==
        NVX_GSP_VBIOS_FUSE_VERSION_UNSUPPORTED);
    assert(signature_offset == 0U);

    /* The VBIOS bitmap packs signatures in ascending supported fuse version.
     * Bits 0, 2 and 5 mean fuse version 5 uses slot 2. */
    make_image(image);
    put32(image + DESC_OFFSET,
          ((44U + 3U * NVX_GSP_VBIOS_RSA3K_SIGNATURE_SIZE) << 16U) |
          (3U << 8U) | 1U);
    put16(image + DESC_OFFSET + 40U, 0x0025U);
    image[DESC_OFFSET + 39U] = 3U;
    assert(nvx_gsp_vbios_find_ga10x_fwsec(
        image, sizeof(image), EXPANSION_OFFSET, &info) == NVX_GSP_VBIOS_OK);
    assert(info.signature_count == 3U);
    assert(info.signature_versions == 0x0025U);
    assert(nvx_gsp_vbios_fwsec_signature_offset(
        &info, 5U, &signature_offset) == NVX_GSP_VBIOS_OK);
    assert(signature_offset == DESC_OFFSET + 44U +
           2U * NVX_GSP_VBIOS_RSA3K_SIGNATURE_SIZE);
    assert(nvx_gsp_vbios_fwsec_signature_offset(
        &info, 3U, &signature_offset) ==
        NVX_GSP_VBIOS_FUSE_VERSION_UNSUPPORTED);

    image[BIT_OFFSET + 11U] ^= 1U;
    memset(&info, 0xA5, sizeof(info));
    status = nvx_gsp_vbios_find_ga10x_fwsec(
        image, sizeof(image), EXPANSION_OFFSET, &info);
    assert(status == NVX_GSP_VBIOS_NOT_FOUND);
    {
        const uint8_t *bytes = (const uint8_t *)&info;
        size_t i;
        for (i = 0U; i < sizeof(info); ++i)
            assert(bytes[i] == 0U);
    }

    make_image(image);
    put32(image + DESC_OFFSET, (428U << 16U) | (2U << 8U) | 1U);
    assert(nvx_gsp_vbios_find_ga10x_fwsec(
        image, sizeof(image), EXPANSION_OFFSET, &info) ==
        NVX_GSP_VBIOS_UNSUPPORTED_DESCRIPTOR);

    make_image(image);
    put32(image + DESC_OFFSET + 4U, 0xFFFFFF00U);
    assert(nvx_gsp_vbios_find_ga10x_fwsec(
        image, sizeof(image), EXPANSION_OFFSET, &info) ==
        NVX_GSP_VBIOS_RANGE_ERROR);

    make_image(image);
    put16(image + DESC_OFFSET + 36U, 0x0400U);
    assert(nvx_gsp_vbios_find_ga10x_fwsec(
        image, sizeof(image), EXPANSION_OFFSET, &info) ==
        NVX_GSP_VBIOS_MALFORMED_DESCRIPTOR);

    make_image(image);
    assert(nvx_gsp_vbios_find_ga10x_fwsec(
        image, sizeof(image), EXPANSION_OFFSET,
        (nvx_gsp_vbios_fwsec_info_t *)(void *)(image + 8U)) ==
        NVX_GSP_VBIOS_OVERLAPPING_OUTPUT);

    assert(nvx_gsp_vbios_find_ga10x_fwsec(
        NULL, sizeof(image), EXPANSION_OFFSET, &info) ==
        NVX_GSP_VBIOS_INVALID_ARGUMENT);
    assert(nvx_gsp_vbios_find_ga10x_fwsec(
        image, sizeof(image), sizeof(image), &info) ==
        NVX_GSP_VBIOS_INVALID_ARGUMENT);

    puts("NVX GA10x VBIOS FWSEC parser tests passed");
    return 0;
}
