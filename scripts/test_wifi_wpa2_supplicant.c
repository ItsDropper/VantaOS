#include "wifi_wpa2.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t sent[WIFI_WPA2_EAPOL_MAX];
    size_t sent_length;
    unsigned int random_calls;
    unsigned int send_calls;
} test_context_t;

static int test_random(uint8_t *output, size_t length, void *opaque)
{
    test_context_t *ctx = (test_context_t *)opaque;
    ++ctx->random_calls;
    for (size_t i = 0U; i < length; ++i) output[i] = (uint8_t)(0xa0U + i);
    return 1;
}

static int test_send(const uint8_t *bssid, const uint8_t *payload,
                     size_t length, void *opaque)
{
    test_context_t *ctx = (test_context_t *)opaque;
    if (!bssid || !payload || length > sizeof(ctx->sent)) return 0;
    memcpy(ctx->sent, payload, length);
    ctx->sent_length = length;
    ++ctx->send_calls;
    return 1;
}

static int test_install(const uint8_t *bssid, const uint8_t *peer,
                        const uint8_t tk[16], const uint8_t *gtk,
                        size_t gtk_length, uint8_t gtk_id, void *opaque)
{
    (void)bssid; (void)peer; (void)tk; (void)gtk;
    (void)gtk_length; (void)gtk_id; (void)opaque;
    return 1;
}

int main(void)
{
    static const uint8_t ssid[] = "test-network";
    static const uint8_t passphrase[] = "correct-horse";
    static const uint8_t local[6] = {0x02,0,0,0,0,1};
    static const uint8_t bssid[6] = {0x02,0,0,0,0,2};
    static const uint8_t wrong_bssid[6] = {0x02,0,0,0,0,3};
    static const uint8_t rsn[] = {
        48,20, 1,0, 0,15,172,4, 1,0, 0,15,172,4,
        1,0, 0,15,172,2, 0,0
    };
    wifi_wpa2_supplicant_t supplicant;
    test_context_t context;
    wifi_wpa2_ops_t ops;
    uint8_t m1[99] = {0};
    uint8_t pmk[WIFI_PMK_SIZE], ptk[64], expected_mic[20], copy[WIFI_WPA2_EAPOL_MAX];

    memset(&supplicant, 0, sizeof(supplicant));
    memset(&context, 0, sizeof(context));
    ops.random_bytes = test_random;
    ops.send_eapol = test_send;
    ops.install_ccmp_keys = test_install;
    ops.context = &context;

    if (!wifi_wpa2_begin(&supplicant, &ops, ssid, sizeof(ssid)-1U,
                         passphrase, sizeof(passphrase)-1U, local, bssid,
                         rsn, sizeof(rsn)) ||
        wifi_wpa2_get_state(&supplicant) != WIFI_WPA2_WAIT_M1)
        return 1;

    /* A frame from a different BSSID must not affect the session. */
    m1[0] = 2U; m1[1] = 3U; m1[2] = 0U; m1[3] = 95U;
    m1[4] = 2U; m1[5] = 0U; m1[6] = 0x8aU;
    m1[7] = 0U; m1[8] = 16U;
    m1[16] = 1U;
    for (size_t i = 0U; i < 32U; ++i) m1[17U+i] = (uint8_t)(i+1U);
    if (wifi_wpa2_receive_eapol(&supplicant, wrong_bssid, m1, sizeof(m1)) != 0 ||
        wifi_wpa2_get_state(&supplicant) != WIFI_WPA2_WAIT_M1 ||
        context.send_calls != 0U)
        return 2;

    if (wifi_wpa2_receive_eapol(&supplicant, bssid, m1, sizeof(m1)) != 1 ||
        wifi_wpa2_get_state(&supplicant) != WIFI_WPA2_WAIT_M3 ||
        context.random_calls != 1U || context.send_calls != 1U ||
        context.sent_length != 99U + sizeof(rsn) ||
        context.sent[1] != 3U || context.sent[5] != 1U ||
        context.sent[6] != 0x0aU ||
        memcmp(context.sent + 99U, rsn, sizeof(rsn)) != 0)
        return 3;

    if (!wifi_pbkdf2_sha1(passphrase, sizeof(passphrase)-1U, ssid,
                          sizeof(ssid)-1U, WIFI_PBKDF2_SHA1_ITERATIONS,
                          pmk, sizeof(pmk)) ||
        !wifi_wpa2_prf512(pmk, bssid, local, m1+17U, context.sent+17U, ptk))
        return 4;
    memcpy(copy, context.sent, context.sent_length);
    memset(copy + 81U, 0, 16U);
    if (!wifi_hmac_sha1(ptk, 16U, copy, context.sent_length, expected_mic) ||
        memcmp(expected_mic, context.sent + 81U, 16U) != 0)
        return 5;

    wifi_wpa2_reset(&supplicant);
    if (wifi_wpa2_get_state(&supplicant) != WIFI_WPA2_IDLE)
        return 6;
    puts("WPA2 supplicant state and EAPOL M2 MIC tests passed.");
    return 0;
}
