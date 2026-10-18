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
                           sizeof(payload), tag) ||
        memcmp(payload, expected_ciphertext, sizeof(payload)) != 0 ||
        memcmp(tag, expected_tag, sizeof(tag)) != 0)
        return 2;

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

    if (wifi_ccmp_encrypt(NULL, nonce, aad, sizeof(aad), payload,
                          sizeof(payload), tag) != 0 ||
        wifi_ccmp_decrypt(key, nonce, aad, WIFI_CCMP_MAX_AAD + 1U,
                          payload, sizeof(payload), tag) != 0)
        return 7;

    puts("Wi-Fi AES-CCM known-answer and tamper tests passed.");
    return 0;
}
