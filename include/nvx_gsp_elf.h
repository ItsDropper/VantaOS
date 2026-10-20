/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_ELF_H
#define VANTA_NVX_GSP_ELF_H
#include <stdint.h>
#include "nvx_gsp_bundle.h"
#define NVX_GSP_ELF_MAX_LOAD_SEGMENTS 16U
typedef enum nvx_gsp_elf_status {
    NVX_GSP_ELF_OK=0, NVX_GSP_ELF_INVALID_ARGUMENT, NVX_GSP_ELF_TRUNCATED,
    NVX_GSP_ELF_BAD_MAGIC, NVX_GSP_ELF_UNSUPPORTED_FORMAT,
    NVX_GSP_ELF_HEADER_INVALID, NVX_GSP_ELF_PROGRAM_TABLE_INVALID,
    NVX_GSP_ELF_SECTION_TABLE_INVALID, NVX_GSP_ELF_STRING_TABLE_INVALID,
    NVX_GSP_ELF_SIGNATURE_MISSING, NVX_GSP_ELF_SIGNATURE_INVALID,
    NVX_GSP_ELF_LOAD_SEGMENT_INVALID, NVX_GSP_ELF_ENTRY_INVALID,
    NVX_GSP_ELF_SECTION_NOT_FOUND, NVX_GSP_ELF_SECTION_AMBIGUOUS,
    NVX_GSP_ELF_SECTION_NO_DATA
} nvx_gsp_elf_status_t;
typedef struct nvx_gsp_elf_segment {
    uint64_t file_offset, virtual_address, physical_address;
    uint64_t file_size, memory_size, alignment;
    uint32_t flags;
} nvx_gsp_elf_segment_t;
typedef struct nvx_gsp_elf_info {
    uint16_t type, machine;
    uint32_t flags, program_count, section_count, load_segment_count;
    uint64_t entry;
    uint64_t signature_offset, signature_size;
    uint32_t signature_section_index;
    nvx_gsp_elf_segment_t load_segments[NVX_GSP_ELF_MAX_LOAD_SEGMENTS];
} nvx_gsp_elf_info_t;
typedef struct nvx_gsp_elf_section {
    const uint8_t *data;
    uint64_t offset,size,alignment;
    uint32_t type,flags,index;
} nvx_gsp_elf_section_t;
/* Returns a bounded section view into the acquired ELF; data is not copied. */
nvx_gsp_elf_status_t nvx_gsp_elf_find_section(const nvx_gsp_component_t *,
    const char *section_name,nvx_gsp_elf_section_t *out_section);
/* Parses ELF64 LE RISC-V GSP runtime firmware; never executes or authenticates it. */
nvx_gsp_elf_status_t nvx_gsp_elf_parse(const nvx_gsp_component_t *,
    const char *signature_section_name, nvx_gsp_elf_info_t *out_info);
#endif
