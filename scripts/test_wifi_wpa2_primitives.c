/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "wifi_crypto.h"
#include "wifi_ccmp.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int from_hex(const char *hex, uint8_t *out, size_t length)
{
    for (size_t i = 0U; i < length; ++i) {
        unsigned int value;
        if (sscanf(hex + i * 2U, "%2x", &value) != 1)
            return 0;
        out[i] = (uint8_t)value;
    }
    return 1;
}

int main(void)
{
    uint8_t hmac[20], expected_hmac[20];
    static const uint8_t hmac_key[20] = {
        0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,
        0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b
    };
    static const uint8_t hmac_data[] = "Hi There";
    if (!from_hex("b617318655057264e28bc0b6fb378c8ef146be00",
                  expected_hmac, sizeof(expected_hmac)) ||
        !wifi_hmac_sha1(hmac_key, sizeof(hmac_key), hmac_data,
                        sizeof(hmac_data) - 1U, hmac) ||
        memcmp(hmac, expected_hmac, sizeof(hmac)) != 0)
        return 1;

    uint8_t pmk[WIFI_PMK_SIZE], aa[6], spa[6], anonce[32], snonce[32];
    uint8_t ptk[64], reversed_ptk[64], expected_ptk[64];
    for (size_t i = 0U; i < sizeof(pmk); ++i) pmk[i] = (uint8_t)i;
    for (size_t i = 0U; i < sizeof(aa); ++i) aa[i] = (uint8_t)i;
    for (size_t i = 0U; i < sizeof(spa); ++i) spa[i] = (uint8_t)(i + 6U);
    for (size_t i = 0U; i < sizeof(anonce); ++i) anonce[i] = (uint8_t)i;
    for (size_t i = 0U; i < sizeof(snonce); ++i) snonce[i] = (uint8_t)(i + 32U);
    if (!from_hex("38fe58cff82d4f8f2d3520d3fb1194a9f95d6f23"
                  "f95b8806a20b657fb8a3ff6151eb1f8db3bc350d"
                  "3e0c3e916a94df5c701e90114d980eae49727ec675b4870d",
                  expected_ptk, sizeof(expected_ptk)) ||
        !wifi_wpa2_prf512(pmk, aa, spa, anonce, snonce, ptk) ||
        memcmp(ptk, expected_ptk, sizeof(ptk)) != 0)
        return 2;
    if (!wifi_wpa2_prf512(pmk, spa, aa, snonce, anonce, reversed_ptk) ||
        memcmp(ptk, reversed_ptk, sizeof(ptk)) != 0)
        return 3;

    uint8_t kek[16], wrapped[24], unwrapped[32], expected_key[16];
    size_t unwrapped_length = 0U;
    if (!from_hex("000102030405060708090a0b0c0d0e0f", kek, sizeof(kek)) ||
        !from_hex("1fa68b0a8112b447aeF34bd8fb5a7b829d3e862371d2cfe5",
                  wrapped, sizeof(wrapped)) ||
        !from_hex("00112233445566778899aabbccddeeff",
                  expected_key, sizeof(expected_key)) ||
        !wifi_aes128_key_unwrap(kek, wrapped, sizeof(wrapped), unwrapped,
                                sizeof(unwrapped), &unwrapped_length) ||
        unwrapped_length != sizeof(expected_key) ||
        memcmp(unwrapped, expected_key, sizeof(expected_key)) != 0)
        return 4;

    wrapped[7] ^= 0x80U;
    memset(unwrapped, 0xA5, sizeof(unwrapped));
    if (wifi_aes128_key_unwrap(kek, wrapped, sizeof(wrapped), unwrapped,
                               sizeof(unwrapped), &unwrapped_length) != 0 ||
        unwrapped_length != 0U)
        return 5;
    for (size_t i = 0U; i < sizeof(expected_key); ++i)
        if (unwrapped[i] != 0U)
            return 6;

    puts("WPA2 HMAC/PRF and RFC 3394 key-unwrap vectors passed.");
    return 0;
}
