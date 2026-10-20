/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_ELF_LOAD_H
#define VANTA_NVX_GSP_ELF_LOAD_H
#include <stddef.h>
#include <stdint.h>
#include "nvx_gsp_elf.h"

typedef enum nvx_gsp_elf_load_status {
    NVX_GSP_ELF_LOAD_OK = 0,
    NVX_GSP_ELF_LOAD_INVALID_ARGUMENT,
    NVX_GSP_ELF_LOAD_BAD_IMAGE,
    NVX_GSP_ELF_LOAD_UNSUPPORTED_RELOCATION,
    NVX_GSP_ELF_LOAD_ADDRESS_OVERFLOW,
    NVX_GSP_ELF_LOAD_DESTINATION_UNAVAILABLE,
    NVX_GSP_ELF_LOAD_DESTINATION_OVERLAP
} nvx_gsp_elf_load_status_t;

/*
 * Map a firmware physical-address range to writable CPU memory. The callback
 * must validate that the whole range belongs to memory reserved for this GSP
 * image; it must not allocate memory or enable device bus mastering.
 */
typedef int (*nvx_gsp_elf_map_fn)(void *context, uint64_t physical_address,
                                  size_t length, uint8_t **out_mapping);

/*
 * Materialize PT_LOAD segments for an already authenticated GA10x runtime.
 * This supports ET_EXEC only: ET_DYN needs a separately implemented relocation
 * policy and is rejected. All mappings are resolved and checked before the
 * first byte is written, file bytes are copied, and the BSS tail is zeroed.
 * The caller is responsible for secure boot/authentication and GSP reset/launch.
 */
nvx_gsp_elf_load_status_t nvx_gsp_elf_load_segments(
    const nvx_gsp_component_t *component, nvx_gsp_elf_map_fn map,
    void *map_context, uint64_t *out_entry);
#endif
