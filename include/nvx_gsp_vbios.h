/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_VBIOS_H
#define VANTA_NVX_GSP_VBIOS_H

#include <stddef.h>
#include <stdint.h>

/*
 * Read-only GA10x FWSEC VBIOS metadata parser.
 *
 * The format is independently decoded from NVIDIA's MIT-licensed Open GPU
 * Kernel Modules (kernel_gsp_fwsec.c) and its public BIT/Falcon descriptor
 * definitions. This does not reuse Nouveau implementation code.
 *
 * expansion_rom_offset is the offset of the PCI expansion-ROM image within
 * the supplied byte buffer; Falcon table/descriptor pointers are relative to
 * that image, while BIT token pointers are absolute VBIOS offsets.
 */
#define NVX_GSP_VBIOS_FWSEC_APP_PRODUCTION 0x85U
#define NVX_GSP_VBIOS_FWSEC_APP_LICENSE   0x05U
#define NVX_GSP_VBIOS_FWSEC_DESC_V3       3U
/* FWSEC V3 is executed by the GA10x GSP Falcon, not SEC2. */
#define NVX_GSP_VBIOS_FWSEC_ENGINE_MASK_GSP 0x0400U
#define NVX_GSP_VBIOS_RSA3K_SIGNATURE_SIZE 384U
#define NVX_GSP_VBIOS_FWSEC_MAX_SIGNATURES 16U

typedef enum nvx_gsp_vbios_status {
    NVX_GSP_VBIOS_OK = 0,
    NVX_GSP_VBIOS_INVALID_ARGUMENT,
    NVX_GSP_VBIOS_NOT_FOUND,
    NVX_GSP_VBIOS_MALFORMED_BIT,
    NVX_GSP_VBIOS_MALFORMED_TABLE,
    NVX_GSP_VBIOS_UNSUPPORTED_DESCRIPTOR,
    NVX_GSP_VBIOS_MALFORMED_DESCRIPTOR,
    NVX_GSP_VBIOS_RANGE_ERROR,
    NVX_GSP_VBIOS_OVERLAPPING_OUTPUT,
    NVX_GSP_VBIOS_FUSE_VERSION_UNSUPPORTED
} nvx_gsp_vbios_status_t;

typedef struct nvx_gsp_vbios_fwsec_info {
    uint32_t bit_header_offset;
    uint32_t falcon_table_offset;
    uint32_t descriptor_offset;
    uint32_t descriptor_size;
    uint32_t image_offset;
    uint32_t image_size;
    uint32_t imem_physical_base;
    uint32_t imem_load_size;
    uint32_t imem_virtual_base;
    uint32_t dmem_physical_base;
    uint32_t dmem_load_size;
    uint32_t interface_offset;
    uint32_t pkc_data_offset;
    uint32_t signatures_offset;
    uint32_t signatures_size;
    uint16_t engine_id_mask;
    uint16_t signature_versions;
    uint8_t ucode_id;
    uint8_t signature_count;
    uint8_t descriptor_version;
    uint8_t encrypted;
} nvx_gsp_vbios_fwsec_info_t;

/*
 * Find the production FWSEC V3 descriptor and its adjacent signed image.
 * This parses metadata only; it does not authenticate signatures, upload
 * firmware, or execute SEC2.
 */
nvx_gsp_vbios_status_t nvx_gsp_vbios_find_ga10x_fwsec(
    const void *vbios, size_t vbios_size, size_t expansion_rom_offset,
    nvx_gsp_vbios_fwsec_info_t *out_info);

/*
 * Resolve the VBIOS signature slot for the hardware ucode fuse version.
 * The result is an absolute offset into the original VBIOS buffer. The caller
 * must still validate the buffer extent and copy/authenticate the signature.
 */
nvx_gsp_vbios_status_t nvx_gsp_vbios_fwsec_signature_offset(
    const nvx_gsp_vbios_fwsec_info_t *info, uint32_t fuse_version,
    uint32_t *out_offset);

#endif
