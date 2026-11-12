/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_fw_manifest.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void hexof(const void *d, size_t n, char out[65])
{
    nvx_sha256_t c; uint8_t h[32];
    nvx_sha256_init(&c); nvx_sha256_update(&c, d, n); nvx_sha256_final(&c, h);
    for (int i = 0; i < 32; i++) sprintf(out + i * 2, "%02x", h[i]);
}

int main(void)
{
    char hex[65];
    /* FIPS 180-4 / NIST known answers. */
    hexof("", 0, hex);
    assert(!strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    hexof("abc", 3, hex);
    assert(!strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    hexof("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, hex);
    assert(!strcmp(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    static char million[1000000]; memset(million, 'a', sizeof(million));
    hexof(million, sizeof(million), hex);
    assert(!strcmp(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    /* Streaming in odd chunks equals one-shot. */
    nvx_sha256_t c; uint8_t h[32]; char hex2[65];
    nvx_sha256_init(&c);
    for (size_t i = 0; i < sizeof(million); i += 977U)
        nvx_sha256_update(&c, million + i, sizeof(million) - i < 977U ? sizeof(million) - i : 977U);
    nvx_sha256_final(&c, h);
    for (int i = 0; i < 32; i++) sprintf(hex2 + i * 2, "%02x", h[i]);
    assert(!strcmp(hex, hex2));

    /* Manifest behaviour: fail closed. */
    const char *m =
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  nvidia/ga106/gsp/gsp-1.bin\r\n"
        "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD *bootloader-1.bin\n"
        "zz7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  broken-1.bin\n";
    size_t ml = strlen(m);
    assert(nvx_fw_manifest_verify(m, ml, "nvidia/ga106/gsp/gsp-1.bin", "abc", 3) == NVX_FW_MANIFEST_OK);
    assert(nvx_fw_manifest_verify(m, ml, "gsp-1.bin", "abd", 3) == NVX_FW_MANIFEST_MISMATCH);
    assert(nvx_fw_manifest_verify(m, ml, "x/bootloader-1.bin", "abc", 3) == NVX_FW_MANIFEST_OK);
    assert(nvx_fw_manifest_verify(m, ml, "ucodes-1.bin", "abc", 3) == NVX_FW_MANIFEST_NO_ENTRY);
    assert(nvx_fw_manifest_verify(m, ml, "broken-1.bin", "abc", 3) == NVX_FW_MANIFEST_MALFORMED);
    assert(nvx_fw_manifest_verify("", 0, "gsp-1.bin", "abc", 3) == NVX_FW_MANIFEST_NO_ENTRY);
    assert(nvx_fw_manifest_verify(NULL, 0, "gsp-1.bin", "abc", 3) == NVX_FW_MANIFEST_INVALID_ARGUMENT);
    /* A name that merely ends with the wanted name must not match. */
    assert(nvx_fw_manifest_verify(m, ml, "sp-1.bin", "abc", 3) == NVX_FW_MANIFEST_NO_ENTRY);
    return 0;
}
