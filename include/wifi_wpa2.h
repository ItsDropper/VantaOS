/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef VANTAOS_WIFI_WPA2_H
#define VANTAOS_WIFI_WPA2_H

#include <stddef.h>
#include <stdint.h>
#include "wifi_crypto.h"

#define WIFI_WPA2_EAPOL_MAX 512U
#define WIFI_WPA2_RSN_IE_MAX 64U
#define WIFI_WPA2_GTK_MAX 32U

typedef enum {
    WIFI_WPA2_IDLE = 0,
    WIFI_WPA2_WAIT_M1,
    WIFI_WPA2_WAIT_M3,
    WIFI_WPA2_AUTHENTICATED,
    WIFI_WPA2_FAILED
} wifi_wpa2_state_t;

typedef struct {
    /* random_bytes must use a kernel CSPRNG; it must fail if entropy is weak. */
    int (*random_bytes)(uint8_t *output, size_t length, void *context);
    /* Send an EAPOL payload to the AP. The transport adds the required 802.11
       LLC/SNAP and radio headers; this callback must not report queued as sent. */
    int (*send_eapol)(const uint8_t *bssid, const uint8_t *payload,
                      size_t length, void *context);
    /* Atomically install CCMP-128 keys; failure leaves no key installed. */
    int (*install_ccmp_keys)(const uint8_t *bssid, const uint8_t *peer,
                             const uint8_t temporal_key[16],
                             const uint8_t *gtk, size_t gtk_length,
                             uint8_t gtk_id, void *context);
    void *context;
} wifi_wpa2_ops_t;

typedef struct {
    wifi_wpa2_ops_t ops;
    wifi_wpa2_state_t state;
    uint8_t local_mac[6], bssid[6];
    uint8_t pmk[WIFI_PMK_SIZE], ptk[64];
    uint8_t anonce[32], snonce[32];
    uint8_t rsn_ie[WIFI_WPA2_RSN_IE_MAX];
    size_t rsn_ie_length;
    uint8_t replay_counter[8];
    uint8_t have_replay, have_ptk;
} wifi_wpa2_supplicant_t;

/* WPA2-Personal, CCMP-128, PSK AKM only. SAE, TKIP and PMF are not supported.
   rsn_ie is the exact RSN IE included in the station's Association Request. */
int wifi_wpa2_begin(wifi_wpa2_supplicant_t *supplicant,
                    const wifi_wpa2_ops_t *ops,
                    const uint8_t *ssid, size_t ssid_length,
                    const uint8_t *passphrase, size_t passphrase_length,
                    const uint8_t local_mac[6], const uint8_t bssid[6],
                    const uint8_t *rsn_ie, size_t rsn_ie_length);

/* Feed one complete EAPOL payload (EtherType 0x888e payload, no Ethernet
   header) received from the selected BSSID. Returns 1 when consumed, 0 if
   unrelated/invalid; invalid authentication messages fail the current session. */
int wifi_wpa2_receive_eapol(wifi_wpa2_supplicant_t *supplicant,
                            const uint8_t *source_mac,
                            const uint8_t *payload, size_t length);
/* Retransmit the current handshake response after a bounded timeout. */
int wifi_wpa2_retry(wifi_wpa2_supplicant_t *supplicant);
void wifi_wpa2_reset(wifi_wpa2_supplicant_t *supplicant);
wifi_wpa2_state_t wifi_wpa2_get_state(const wifi_wpa2_supplicant_t *supplicant);

#endif
