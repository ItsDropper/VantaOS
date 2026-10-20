/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 *
 * The metadata contract is independently decoded from NVIDIA's MIT-licensed
 * Open GPU Kernel Modules, particularly kernel_gsp_fwsec.c. No Nouveau source
 * is used by this implementation.
 */
#include "nvx_gsp_vbios.h"

#include <limits.h>
#include <string.h>

#define BIT_HEADER_ID 0xB8FFU
#define BIT_SIGNATURE 0x00544942U
#define BIT_HEADER_MIN_SIZE 12U
#define BIT_TOKEN_BIOSDATA 0x42U
#define BIT_TOKEN_FALCON_DATA 0x70U
#define BIT_FALCON_DATA_VERSION 2U
#define FALCON_TABLE_VERSION 1U
#define FALCON_TABLE_HEADER_MIN_SIZE 6U
#define FALCON_TABLE_ENTRY_MIN_SIZE 6U
#define FALCON_DESC_V3_SIZE 44U
#define FALCON_DESC_AVAILABLE 0x01U
#define FALCON_DESC_ENCRYPTED 0x04U
#define FALCON_DESC_RESERVED_FLAGS 0xFAU

static int range_ok(size_t offset, size_t length, size_t limit)
{
    return offset <= limit && length <= limit - offset;
}

static int add_size(size_t a, size_t b, size_t *out)
{
    if (a > SIZE_MAX - b)
        return 0;
    *out = a + b;
    return 1;
}

static int ranges_overlap(const void *a, size_t an, const void *b, size_t bn)
{
    uintptr_t x = (uintptr_t)a;
    uintptr_t y = (uintptr_t)b;
    if (!an || !bn)
        return 0;
    if (x > UINTPTR_MAX - an || y > UINTPTR_MAX - bn)
        return 1;
    return x < y + bn && y < x + an;
}

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8U));
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}

static int read16_at(const uint8_t *image, size_t size, size_t offset,
                     uint16_t *out)
{
    if (!out || !range_ok(offset, 2U, size))
        return 0;
    *out = le16(image + offset);
    return 1;
}

static int read32_at(const uint8_t *image, size_t size, size_t offset,
                     uint32_t *out)
{
    if (!out || !range_ok(offset, 4U, size))
        return 0;
    *out = le32(image + offset);
    return 1;
}

static int bit_header_valid(const uint8_t *image, size_t size, size_t offset)
{
    uint16_t id;
    uint32_t signature;
    size_t header_size;
    size_t i;
    uint8_t checksum = 0U;

    if (!read16_at(image, size, offset, &id) || id != BIT_HEADER_ID ||
        !read32_at(image, size, offset + 2U, &signature) ||
        signature != BIT_SIGNATURE ||
        !range_ok(offset, BIT_HEADER_MIN_SIZE, size))
        return 0;

    header_size = image[offset + 8U];
    if (header_size < BIT_HEADER_MIN_SIZE ||
        !range_ok(offset, header_size, size))
        return 0;

    for (i = 0; i < header_size; ++i)
        checksum = (uint8_t)(checksum + image[offset + i]);
    return checksum == 0U;
}

static nvx_gsp_vbios_status_t parse_fwsec_from_bit(
    const uint8_t *image, size_t size, size_t expansion_offset,
    size_t bit_offset, nvx_gsp_vbios_fwsec_info_t *out)
{
    const uint8_t *bit = image + bit_offset;
    size_t header_size = bit[8U];
    size_t token_size = bit[9U];
    size_t token_count = bit[10U];
    size_t token_base;
    size_t token_bytes;
    size_t token_end;
    size_t token_index;

    if (token_size < 6U || !add_size(bit_offset, header_size, &token_base) ||
        token_count > SIZE_MAX / token_size)
        return NVX_GSP_VBIOS_MALFORMED_BIT;
    token_bytes = token_count * token_size;
    if (!range_ok(token_base, token_bytes, size) ||
        !add_size(token_base, token_bytes, &token_end))
        return NVX_GSP_VBIOS_MALFORMED_BIT;
    (void)token_end;

    for (token_index = 0U; token_index < token_count; ++token_index) {
        size_t token_offset = token_base + token_index * token_size;
        const uint8_t *token = image + token_offset;
        uint8_t token_id = token[0U];
        uint8_t data_version = token[1U];
        uint16_t data_size = le16(token + 2U);
        uint32_t data_pointer;
        size_t table_offset;
        const uint8_t *table;
        uint8_t table_version;
        uint8_t table_header_size;
        uint8_t table_entry_size;
        uint8_t table_entry_count;
        size_t entry_index;

        if (token_size >= 8U)
            data_pointer = le32(token + 4U);
        else
            data_pointer = le16(token + 4U);

        if (token_id != BIT_TOKEN_FALCON_DATA ||
            data_version != BIT_FALCON_DATA_VERSION || data_size < 4U)
            continue;

        if (!add_size(expansion_offset, (size_t)data_pointer, &table_offset) ||
            !range_ok(table_offset, FALCON_TABLE_HEADER_MIN_SIZE, size))
            return NVX_GSP_VBIOS_MALFORMED_TABLE;

        table = image + table_offset;
        table_version = table[0U];
        table_header_size = table[1U];
        table_entry_size = table[2U];
        table_entry_count = table[3U];
        if (table_version != FALCON_TABLE_VERSION ||
            table_header_size < FALCON_TABLE_HEADER_MIN_SIZE ||
            table_entry_size < FALCON_TABLE_ENTRY_MIN_SIZE)
            return NVX_GSP_VBIOS_MALFORMED_TABLE;
        if (!range_ok(table_offset, table_header_size, size) ||
            (size_t)table_entry_count >
                (SIZE_MAX - table_header_size) / table_entry_size ||
            !range_ok(table_offset + table_header_size,
                (size_t)table_entry_count * table_entry_size, size))
            return NVX_GSP_VBIOS_MALFORMED_TABLE;

        for (entry_index = 0U; entry_index < table_entry_count; ++entry_index) {
            size_t entry_offset = table_offset + table_header_size +
                                  entry_index * table_entry_size;
            const uint8_t *entry = image + entry_offset;
            uint8_t app_id = entry[0U];
            uint32_t descriptor_pointer = le32(entry + 2U);
            size_t descriptor_offset;
            uint32_t descriptor_word;
            uint8_t descriptor_version;
            uint16_t descriptor_size;
            uint32_t stored_size;
            uint32_t pkc_data_offset;
            uint32_t interface_offset;
            uint32_t imem_phys;
            uint32_t imem_size;
            uint32_t imem_virt;
            uint32_t dmem_phys;
            uint32_t dmem_size;
            uint16_t engine_mask;
            uint8_t ucode_id;
            uint8_t signature_count;
            uint16_t signature_versions;
            uint16_t reserved;
            size_t image_offset;
            size_t aligned_image_size;
            size_t signatures_offset;
            size_t signatures_size;
            nvx_gsp_vbios_fwsec_info_t result;

            /* Production FWSEC only; do not silently select debug firmware. */
            if (app_id != NVX_GSP_VBIOS_FWSEC_APP_PRODUCTION)
                continue;
            if (!add_size(expansion_offset, (size_t)descriptor_pointer,
                          &descriptor_offset) ||
                !read32_at(image, size, descriptor_offset, &descriptor_word))
                return NVX_GSP_VBIOS_RANGE_ERROR;

            if ((descriptor_word & FALCON_DESC_RESERVED_FLAGS) != 0U ||
                (descriptor_word & FALCON_DESC_AVAILABLE) == 0U)
                return NVX_GSP_VBIOS_MALFORMED_DESCRIPTOR;
            descriptor_version = (uint8_t)((descriptor_word >> 8U) & 0xFFU);
            descriptor_size = (uint16_t)(descriptor_word >> 16U);
            if (descriptor_version != NVX_GSP_VBIOS_FWSEC_DESC_V3)
                return NVX_GSP_VBIOS_UNSUPPORTED_DESCRIPTOR;
            if (descriptor_size < FALCON_DESC_V3_SIZE ||
                !range_ok(descriptor_offset, descriptor_size, size))
                return NVX_GSP_VBIOS_MALFORMED_DESCRIPTOR;

            stored_size = le32(image + descriptor_offset + 4U);
            pkc_data_offset = le32(image + descriptor_offset + 8U);
            interface_offset = le32(image + descriptor_offset + 12U);
            imem_phys = le32(image + descriptor_offset + 16U);
            imem_size = le32(image + descriptor_offset + 20U);
            imem_virt = le32(image + descriptor_offset + 24U);
            dmem_phys = le32(image + descriptor_offset + 28U);
            dmem_size = le32(image + descriptor_offset + 32U);
            engine_mask = le16(image + descriptor_offset + 36U);
            ucode_id = image[descriptor_offset + 38U];
            signature_count = image[descriptor_offset + 39U];
            signature_versions = le16(image + descriptor_offset + 40U);
            reserved = le16(image + descriptor_offset + 42U);

            if (reserved != 0U || stored_size == 0U || imem_size == 0U ||
                dmem_size == 0U || imem_size > stored_size ||
                dmem_size > stored_size - imem_size ||
                engine_mask != 0x0001U || ucode_id == 0U ||
                signature_count == 0U ||
                signature_count > NVX_GSP_VBIOS_FWSEC_MAX_SIGNATURES)
                return NVX_GSP_VBIOS_MALFORMED_DESCRIPTOR;

            signatures_size = (size_t)descriptor_size - FALCON_DESC_V3_SIZE;
            if (signatures_size !=
                (size_t)signature_count * NVX_GSP_VBIOS_RSA3K_SIGNATURE_SIZE)
                return NVX_GSP_VBIOS_MALFORMED_DESCRIPTOR;

            if (!add_size(descriptor_offset, descriptor_size, &image_offset))
                return NVX_GSP_VBIOS_RANGE_ERROR;
            aligned_image_size = ((size_t)stored_size + 255U) & ~(size_t)255U;
            if (aligned_image_size < stored_size ||
                !range_ok(image_offset, aligned_image_size, size))
                return NVX_GSP_VBIOS_RANGE_ERROR;

            /* PKC signature destination must fit inside the loaded DMEM. */
            if (pkc_data_offset > dmem_size ||
                NVX_GSP_VBIOS_RSA3K_SIGNATURE_SIZE > dmem_size - pkc_data_offset)
                return NVX_GSP_VBIOS_MALFORMED_DESCRIPTOR;
            if (!add_size(descriptor_offset, FALCON_DESC_V3_SIZE,
                          &signatures_offset))
                return NVX_GSP_VBIOS_RANGE_ERROR;

            memset(&result, 0, sizeof(result));
            result.bit_header_offset = (uint32_t)bit_offset;
            result.falcon_table_offset = (uint32_t)table_offset;
            result.descriptor_offset = (uint32_t)descriptor_offset;
            result.descriptor_size = descriptor_size;
            result.image_offset = (uint32_t)image_offset;
            result.image_size = stored_size;
            result.imem_physical_base = imem_phys;
            result.imem_load_size = imem_size;
            result.imem_virtual_base = imem_virt;
            result.dmem_physical_base = dmem_phys;
            result.dmem_load_size = dmem_size;
            result.interface_offset = interface_offset;
            result.pkc_data_offset = pkc_data_offset;
            result.signatures_offset = (uint32_t)signatures_offset;
            result.signatures_size = (uint32_t)signatures_size;
            result.engine_id_mask = engine_mask;
            result.signature_versions = signature_versions;
            result.ucode_id = ucode_id;
            result.signature_count = signature_count;
            result.descriptor_version = descriptor_version;
            result.encrypted = (uint8_t)((descriptor_word & FALCON_DESC_ENCRYPTED) != 0U);
            *out = result;
            return NVX_GSP_VBIOS_OK;
        }
    }
    return NVX_GSP_VBIOS_NOT_FOUND;
}

nvx_gsp_vbios_status_t nvx_gsp_vbios_find_ga10x_fwsec(
    const void *vbios, size_t vbios_size, size_t expansion_rom_offset,
    nvx_gsp_vbios_fwsec_info_t *out_info)
{
    const uint8_t *image = (const uint8_t *)vbios;
    size_t offset;

    if (!vbios || !out_info || vbios_size < BIT_HEADER_MIN_SIZE ||
        expansion_rom_offset >= vbios_size)
        return NVX_GSP_VBIOS_INVALID_ARGUMENT;
    if (ranges_overlap(vbios, vbios_size, out_info, sizeof(*out_info)))
        return NVX_GSP_VBIOS_OVERLAPPING_OUTPUT;
    memset(out_info, 0, sizeof(*out_info));

    for (offset = 0U; offset <= vbios_size - BIT_HEADER_MIN_SIZE; ++offset) {
        nvx_gsp_vbios_status_t status;
        if (!bit_header_valid(image, vbios_size, offset))
            continue;
        status = parse_fwsec_from_bit(image, vbios_size, expansion_rom_offset,
                                      offset, out_info);
        if (status == NVX_GSP_VBIOS_OK)
            return status;
        if (status != NVX_GSP_VBIOS_NOT_FOUND) {
            memset(out_info, 0, sizeof(*out_info));
            return status;
        }
    }
    return NVX_GSP_VBIOS_NOT_FOUND;
}
