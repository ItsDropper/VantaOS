/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_NVKV_H
#define VANTA_NVX_GSP_NVKV_H

#include <stddef.h>
#include <stdint.h>

typedef enum nvx_gsp_nvkv_status {
    NVX_GSP_NVKV_OK = 0,
    NVX_GSP_NVKV_INVALID_ARGUMENT,
    NVX_GSP_NVKV_NO_SPACE,
    NVX_GSP_NVKV_INVALID_COUNT,
    NVX_GSP_NVKV_BAD_STREAM
} nvx_gsp_nvkv_status_t;

typedef struct nvx_gsp_nvkv_writer {
    uint8_t *buffer;
    size_t capacity;
    size_t words_written;
    uint8_t initialized;
} nvx_gsp_nvkv_writer_t;

/*
 * NVKV instructions are little-endian 64-bit words:
 * count/value[63:32], opcode[31:28], index[27:16], key[15:0].
 * Variable payloads are padded to an 8-byte boundary with zero bytes.
 * This is a wire-format writer; keys and values must come from the caller's
 * real platform/device state and the matching GSP firmware ABI.
 */
nvx_gsp_nvkv_status_t nvx_gsp_nvkv_begin(
    nvx_gsp_nvkv_writer_t *writer, void *buffer, size_t capacity);
nvx_gsp_nvkv_status_t nvx_gsp_nvkv_put_imm32(
    nvx_gsp_nvkv_writer_t *writer, uint16_t index, uint16_t key, uint32_t value);
nvx_gsp_nvkv_status_t nvx_gsp_nvkv_put_seq32(
    nvx_gsp_nvkv_writer_t *writer, uint16_t index, uint16_t first_key,
    const uint32_t *values, size_t count);
nvx_gsp_nvkv_status_t nvx_gsp_nvkv_put_seq64(
    nvx_gsp_nvkv_writer_t *writer, uint16_t index, uint16_t first_key,
    const uint64_t *values, size_t count);
nvx_gsp_nvkv_status_t nvx_gsp_nvkv_put_array8(
    nvx_gsp_nvkv_writer_t *writer, uint16_t index, uint16_t key,
    const uint8_t *values, size_t count);
nvx_gsp_nvkv_status_t nvx_gsp_nvkv_put_array32(
    nvx_gsp_nvkv_writer_t *writer, uint16_t index, uint16_t key,
    const uint32_t *values, size_t count);
nvx_gsp_nvkv_status_t nvx_gsp_nvkv_put_array64(
    nvx_gsp_nvkv_writer_t *writer, uint16_t index, uint16_t key,
    const uint64_t *values, size_t count);
size_t nvx_gsp_nvkv_size_bytes(const nvx_gsp_nvkv_writer_t *writer);
nvx_gsp_nvkv_status_t nvx_gsp_nvkv_validate(
    const void *buffer, size_t size_bytes);

#endif
