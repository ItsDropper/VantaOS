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
    uint8_t key[16], nonce[13], aad[8], payload[8], expected_ciphertext[8];
    uint8_t tag[8], expected_tag[8], original[16];

    if (!from_hex("c0c1c2c3c4c5c6c7c8c9cacbcccdcecf", key, sizeof(key)) ||
        !from_hex("00000003020100a0a1a2a3a4a5", nonce, sizeof(nonce)) ||
        !from_hex("0001020304050607", aad, sizeof(aad)) ||
        !from_hex("08090a0b0c0d0e0f", payload, sizeof(payload)) ||
        !from_hex("588c979a61c663d2",
                  expected_ciphertext, sizeof(expected_ciphertext)) ||
        !from_hex("ea378169c1bc6209", expected_tag, sizeof(expected_tag)))
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

    {
        wifi_ccmp_replay_t replay;
        uint64_t pn = 0U;
        uint8_t key_id = 0U, header[WIFI_CCMP_HEADER_SIZE];
        uint8_t nonce[WIFI_CCMP_NONCE_SIZE];
        const uint8_t transmitter[6] = {0x02U, 1U, 2U, 3U, 4U, 5U};

        wifi_ccmp_replay_reset(&replay);
        if (!wifi_ccmp_next_tx_pn(&replay, &pn) || pn != 1U ||
            !wifi_ccmp_next_tx_pn(&replay, &pn) || pn != 2U)
            return 14;
        if (!wifi_ccmp_header_encode(header, pn, 2U) ||
            !wifi_ccmp_header_decode(header, &pn, &key_id) ||
            pn != 2U || key_id != 2U)
            return 15;
        header[2] = 1U;
        if (wifi_ccmp_header_decode(header, &pn, &key_id))
            return 16;
        header[2] = 0U;
        header[3] &= (uint8_t)~0x20U;
        if (wifi_ccmp_header_decode(header, &pn, &key_id))
            return 17;
        if (!wifi_ccmp_header_encode(header, WIFI_CCMP_PN_MAX, 3U) ||
            !wifi_ccmp_header_decode(header, &pn, &key_id) ||
            pn != WIFI_CCMP_PN_MAX || key_id != 3U ||
            wifi_ccmp_header_encode(header, 0U, 0U) ||
            wifi_ccmp_header_encode(header, WIFI_CCMP_PN_MAX + 1U, 0U))
            return 18;

        wifi_ccmp_build_nonce(nonce, 5U, transmitter, UINT64_C(0x010203040506));
        if (nonce[0] != 5U || memcmp(nonce + 1U, transmitter, 6U) != 0 ||
            memcmp(nonce + 7U, "\\x01\\x02\\x03\\x04\\x05\\x06", 6U) != 0)
            return 19;

        wifi_ccmp_replay_reset(&replay);
        if (!wifi_ccmp_accept_rx_pn(&replay, 10U) ||
            !wifi_ccmp_accept_rx_pn(&replay, 12U) ||
            !wifi_ccmp_accept_rx_pn(&replay, 11U) ||
            wifi_ccmp_accept_rx_pn(&replay, 11U) ||
            wifi_ccmp_accept_rx_pn(&replay, 0U) ||
            wifi_ccmp_accept_rx_pn(&replay, WIFI_CCMP_PN_MAX + 1U))
            return 20;
        if (!wifi_ccmp_accept_rx_pn(&replay, 100U) ||
            wifi_ccmp_accept_rx_pn(&replay, 35U))
            return 21; /* A packet older than the 64-PN window is rejected. */

        wifi_ccmp_replay_reset(&replay);
        replay.tx_next_pn = WIFI_CCMP_PN_MAX;
        if (!wifi_ccmp_next_tx_pn(&replay, &pn) || pn != WIFI_CCMP_PN_MAX ||
            wifi_ccmp_next_tx_pn(&replay, &pn))
            return 22; /* PN exhaustion must never wrap and reuse a nonce. */
    }

    puts("Wi-Fi AES-CCM, CCMP header/nonce, PN replay-window and tamper tests passed.");
    return 0;
}
