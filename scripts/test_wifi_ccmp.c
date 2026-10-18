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
    uint8_t key[16], nonce[13], aad[8], payload[16], expected_ciphertext[16];
    uint8_t tag[8], expected_tag[8], original[16];

    if (!from_hex("c0c1c2c3c4c5c6c7c8c9cacbcccdcecf", key, sizeof(key)) ||
        !from_hex("00000003020100a0a1a2a3a4a5", nonce, sizeof(nonce)) ||
        !from_hex("0001020304050607", aad, sizeof(aad)) ||
        !from_hex("08090a0b0c0d0e0f1011121314151617", payload, sizeof(payload)) ||
        !from_hex("588c979a61c663d2f066d0c2c0f98980",
                  expected_ciphertext, sizeof(expected_ciphertext)) ||
        !from_hex("6d5f6b61dac38417", expected_tag, sizeof(expected_tag)))
        return 1;

    memcpy(original, payload, sizeof(payload));
    if (!wifi_ccmp_encrypt(key, nonce, aad, sizeof(aad), payload,
                           sizeof(payload), tag)) {
        puts("FAIL: wifi_ccmp_encrypt rejected RFC 3610 vector");
        return 2;
    }
    if (memcmp(payload, expected_ciphertext, sizeof(payload)) != 0) {
        puts("FAIL: AES-CCM ciphertext mismatch");
        for (size_t i = 0U; i < sizeof(payload); ++i) printf("%02x", payload[i]);
        puts("");
        return 3;
    }
    if (memcmp(tag, expected_tag, sizeof(tag)) != 0) {
        puts("FAIL: AES-CCM authentication tag mismatch");
        for (size_t i = 0U; i < sizeof(tag); ++i) printf("%02x", tag[i]);
        puts("");
        return 4;
    }

    if (!wifi_ccmp_decrypt(key, nonce, aad, sizeof(aad), payload,
                           sizeof(payload), tag) ||
        memcmp(payload, original, sizeof(payload)) != 0)
        return 3;

    if (!wifi_ccmp_encrypt(key, nonce, aad, sizeof(aad), payload,
                           sizeof(payload), tag))
        return 4;
    tag[0] ^= 1U;
    if (wifi_ccmp_decrypt(key, nonce, aad, sizeof(aad), payload,
                          sizeof(payload), tag) != 0)
        return 5;
    for (size_t i = 0U; i < sizeof(payload); ++i)
        if (payload[i] != 0U)
            return 6;

    /* Ciphertext tampering must fail authentication and erase plaintext. */
    memcpy(payload, original, sizeof(payload));
    if (!wifi_ccmp_encrypt(key, nonce, aad, sizeof(aad), payload,
                           sizeof(payload), tag))
        return 7;
    payload[3] ^= 0x80U;
    if (wifi_ccmp_decrypt(key, nonce, aad, sizeof(aad), payload,
                          sizeof(payload), tag) != 0)
        return 8;
    for (size_t i = 0U; i < sizeof(payload); ++i)
        if (payload[i] != 0U)
            return 9;

    /* AAD tampering must also be detected, not just a corrupted MIC. */
    memcpy(payload, original, sizeof(payload));
    if (!wifi_ccmp_encrypt(key, nonce, aad, sizeof(aad), payload,
                           sizeof(payload), tag))
        return 10;
    aad[0] ^= 0x01U;
    if (wifi_ccmp_decrypt(key, nonce, aad, sizeof(aad), payload,
                          sizeof(payload), tag) != 0)
        return 11;
    aad[0] ^= 0x01U;
    for (size_t i = 0U; i < sizeof(payload); ++i)
        if (payload[i] != 0U)
            return 12;

    if (wifi_ccmp_encrypt(NULL, nonce, aad, sizeof(aad), payload,
                          sizeof(payload), tag) != 0 ||
        wifi_ccmp_decrypt(key, nonce, aad, WIFI_CCMP_MAX_AAD + 1U,
                          payload, sizeof(payload), tag) != 0)
        return 13;

    puts("Wi-Fi AES-CCM known-answer and tamper tests passed.");
    return 0;
}
