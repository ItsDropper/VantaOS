/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_fw_manifest.h"

#include <string.h>

static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32U - (n))))

static void compress(uint32_t s[8], const uint8_t b[64])
{
    uint32_t w[64], v[8];
    for (unsigned i = 0; i < 16U; i++)
        w[i] = ((uint32_t)b[i*4] << 24) | ((uint32_t)b[i*4+1] << 16) |
               ((uint32_t)b[i*4+2] << 8) | (uint32_t)b[i*4+3];
    for (unsigned i = 16; i < 64U; i++) {
        uint32_t s0 = ROR(w[i-15], 7) ^ ROR(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = ROR(w[i-2], 17) ^ ROR(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    memcpy(v, s, sizeof(v));
    for (unsigned i = 0; i < 64U; i++) {
        uint32_t t1 = v[7] + (ROR(v[4], 6) ^ ROR(v[4], 11) ^ ROR(v[4], 25)) +
                      ((v[4] & v[5]) ^ (~v[4] & v[6])) + K[i] + w[i];
        uint32_t t2 = (ROR(v[0], 2) ^ ROR(v[0], 13) ^ ROR(v[0], 22)) +
                      ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
        v[7] = v[6]; v[6] = v[5]; v[5] = v[4]; v[4] = v[3] + t1;
        v[3] = v[2]; v[2] = v[1]; v[1] = v[0]; v[0] = t1 + t2;
    }
    for (unsigned i = 0; i < 8U; i++) s[i] += v[i];
}

void nvx_sha256_init(nvx_sha256_t *c)
{
    static const uint32_t iv[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                                   0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    memcpy(c->state, iv, sizeof(iv));
    c->total_bytes = 0U;
    c->block_used = 0U;
}

void nvx_sha256_update(nvx_sha256_t *c, const void *data, size_t size)
{
    const uint8_t *p = data;
    c->total_bytes += (uint64_t)size;
    while (size) {
        size_t n = 64U - c->block_used;
        if (n > size) n = size;
        memcpy(c->block + c->block_used, p, n);
        c->block_used += (uint32_t)n; p += n; size -= n;
        if (c->block_used == 64U) { compress(c->state, c->block); c->block_used = 0U; }
    }
}

void nvx_sha256_final(nvx_sha256_t *c, uint8_t out[32])
{
    uint64_t bits = c->total_bytes * 8U;
    uint8_t pad = 0x80U;
    uint8_t zero = 0U;
    uint8_t len[8];
    nvx_sha256_update(c, &pad, 1U);
    while (c->block_used != 56U) nvx_sha256_update(c, &zero, 1U);
    for (unsigned i = 0; i < 8U; i++) len[i] = (uint8_t)(bits >> (56U - 8U * i));
    nvx_sha256_update(c, len, 8U);
    for (unsigned i = 0; i < 8U; i++) {
        out[i*4] = (uint8_t)(c->state[i] >> 24); out[i*4+1] = (uint8_t)(c->state[i] >> 16);
        out[i*4+2] = (uint8_t)(c->state[i] >> 8); out[i*4+3] = (uint8_t)c->state[i];
    }
}

static int hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static const char *base_name(const char *p)
{
    const char *b = p;
    for (; *p; p++) if (*p == '/') b = p + 1;
    return b;
}

nvx_fw_manifest_status_t nvx_fw_manifest_verify(
    const char *manifest, size_t manifest_size,
    const char *file_name, const void *blob, size_t blob_size)
{
    if (!manifest || !file_name || (!blob && blob_size)) return NVX_FW_MANIFEST_INVALID_ARGUMENT;
    const char *want = base_name(file_name);
    size_t want_len = strlen(want);
    if (!want_len) return NVX_FW_MANIFEST_INVALID_ARGUMENT;

    size_t pos = 0U;
    while (pos < manifest_size) {
        size_t end = pos;
        while (end < manifest_size && manifest[end] != '\n') end++;
        const char *line = manifest + pos;
        size_t n = end - pos;
        pos = end + 1U;
        while (n && line[n-1] == '\r') n--;
        if (n < 64U + 2U) continue;
        const char *name = line + 64U;
        size_t name_len = n - 64U;
        if (name[0] != ' ' || (name[1] != ' ' && name[1] != '*')) continue;
        name += 2; name_len -= 2U;
        const char *entry = base_name(name);
        size_t entry_len = name_len - (size_t)(entry - name);
        if (entry_len != want_len || memcmp(entry, want, want_len) != 0) continue;

        uint8_t pinned[32];
        for (unsigned i = 0; i < 32U; i++) {
            int hi = hex_value(line[i*2]), lo = hex_value(line[i*2+1]);
            if (hi < 0 || lo < 0) return NVX_FW_MANIFEST_MALFORMED;
            pinned[i] = (uint8_t)((hi << 4) | lo);
        }
        nvx_sha256_t ctx; uint8_t got[32]; uint8_t diff = 0U;
        nvx_sha256_init(&ctx);
        nvx_sha256_update(&ctx, blob, blob_size);
        nvx_sha256_final(&ctx, got);
        for (unsigned i = 0; i < 32U; i++) diff |= (uint8_t)(got[i] ^ pinned[i]);
        return diff ? NVX_FW_MANIFEST_MISMATCH : NVX_FW_MANIFEST_OK;
    }
    return NVX_FW_MANIFEST_NO_ENTRY;
}
