/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_FW_MANIFEST_H
#define VANTA_NVX_FW_MANIFEST_H

#include <stddef.h>
#include <stdint.h>

/* SHA-256 (FIPS 180-4). Streaming interface; no allocation. */
typedef struct nvx_sha256 {
    uint32_t state[8];
    uint64_t total_bytes;
    uint8_t block[64];
    uint32_t block_used;
} nvx_sha256_t;

void nvx_sha256_init(nvx_sha256_t *ctx);
void nvx_sha256_update(nvx_sha256_t *ctx, const void *data, size_t size);
void nvx_sha256_final(nvx_sha256_t *ctx, uint8_t out[32]);

typedef enum nvx_fw_manifest_status {
    NVX_FW_MANIFEST_OK = 0,
    NVX_FW_MANIFEST_INVALID_ARGUMENT,
    NVX_FW_MANIFEST_NO_ENTRY,     /* file is not listed: fail closed */
    NVX_FW_MANIFEST_MALFORMED,    /* matching line is not "<64 hex>  <name>" */
    NVX_FW_MANIFEST_MISMATCH      /* digest differs from the pinned value */
} nvx_fw_manifest_status_t;

/*
 * Check blob against a coreutils-style SHA256SUMS manifest the user pinned:
 * "<64 lowercase/uppercase hex>  <file name>" per line ('*' binary marker
 * accepted). Only the final path component of file_name is compared. The
 * manifest is the trust anchor; an absent entry is a failure, never a pass.
 * Note this proves the file is the one the user pinned, not that NVIDIA
 * signed it; the GPU's own boot ROM authenticates the signed images.
 */
nvx_fw_manifest_status_t nvx_fw_manifest_verify(
    const char *manifest, size_t manifest_size,
    const char *file_name, const void *blob, size_t blob_size);

#endif
