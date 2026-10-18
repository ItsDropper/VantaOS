#include "wifi_wpa2.h"
#include "wifi_ccmp.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t sent[WIFI_WPA2_EAPOL_MAX];
    size_t sent_length;
    unsigned int random_calls;
    unsigned int send_calls;
    unsigned int install_calls;
    uint8_t installed_tk[16], installed_gtk[16], installed_gtk_id;
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
    test_context_t *ctx = (test_context_t *)opaque;
    static const uint8_t expected_bssid[6] = {0x02,0,0,0,0,2};
    static const uint8_t expected_peer[6] = {0x02,0,0,0,0,1};
    if (!bssid || !peer || !tk || !gtk || gtk_length != 16U || gtk_id != 0U ||
        memcmp(bssid, expected_bssid, 6U) != 0 ||
        memcmp(peer, expected_peer, 6U) != 0)
        return 0;
    memcpy(ctx->installed_tk, tk, 16U);
    memcpy(ctx->installed_gtk, gtk, 16U);
    ctx->installed_gtk_id = gtk_id;
    ++ctx->install_calls;
    return 1;
}

static void put_be16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
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
    uint8_t m3[131] = {0}, kde[24] = {221,22,0,15,172,1,0,0};
    uint8_t gtk[16], wrapped[32];
    size_t wrapped_length = 0U;

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

    /* Build a deterministic M3 with a wrapped GTK KDE and a valid MIC. */
    for (size_t i = 0U; i < sizeof(gtk); ++i) {
        gtk[i] = (uint8_t)(0x30U + i);
        kde[8U + i] = gtk[i];
    }
    if (!wifi_aes128_key_wrap(ptk + 16U, kde, sizeof(kde), wrapped,
                              sizeof(wrapped), &wrapped_length) ||
        wrapped_length != sizeof(wrapped))
        return 6;
    m3[0] = 2U; m3[1] = 3U; put_be16(m3 + 2U, sizeof(m3) - 4U);
    m3[4] = 2U; put_be16(m3 + 5U, 0x13caU); put_be16(m3 + 7U, 16U);
    m3[16] = 2U;
    memcpy(m3 + 17U, m1 + 17U, 32U);
    put_be16(m3 + 97U, sizeof(wrapped));
    memcpy(m3 + 99U, wrapped, sizeof(wrapped));
    if (!wifi_hmac_sha1(ptk, 16U, m3, sizeof(m3), expected_mic))
        return 7;
    memcpy(m3 + 81U, expected_mic, 16U);
    if (wifi_wpa2_receive_eapol(&supplicant, bssid, m3, sizeof(m3)) != 1 ||
        wifi_wpa2_get_state(&supplicant) != WIFI_WPA2_AUTHENTICATED ||
        context.install_calls != 1U || context.send_calls != 2U ||
        memcmp(context.installed_tk, ptk + 32U, 16U) != 0 ||
        memcmp(context.installed_gtk, gtk, 16U) != 0 ||
        context.installed_gtk_id != 0U)
        return 8;

    /*
     * A rejected reconnect attempt must invalidate the previous authenticated
     * session rather than leave stale keys/state usable by the caller.
     */
    {
        static const uint8_t invalid_passphrase[] = "short";
        if (wifi_wpa2_begin(&supplicant, &ops, ssid, sizeof(ssid)-1U,
                            invalid_passphrase, sizeof(invalid_passphrase)-1U,
                            local, bssid, rsn, sizeof(rsn)) != 0 ||
            wifi_wpa2_get_state(&supplicant) != WIFI_WPA2_IDLE)
            return 9;
        for (size_t i = 0U; i < sizeof(supplicant.pmk); ++i)
            if (supplicant.pmk[i] != 0U) return 10;
        for (size_t i = 0U; i < sizeof(supplicant.ptk); ++i)
            if (supplicant.ptk[i] != 0U) return 11;
        if (supplicant.rsn_ie_length != 0U || supplicant.have_ptk ||
            supplicant.have_replay)
            return 12;
    }
    puts("WPA2 EAPOL handshake, key-install, and failed-reconnect cleanup tests passed.");
    return 0;
}
