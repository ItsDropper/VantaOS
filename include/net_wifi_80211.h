#ifndef VANTAOS_NET_WIFI_80211_H
#define VANTAOS_NET_WIFI_80211_H

#include "net_wifi.h"
#include <stddef.h>
#include <stdint.h>

/*
 * Bounded IEEE 802.11 management-frame helpers.
 * These encode/parse frames only; a chipset driver must provide a configured
 * radio, channel control, RX delivery, timeouts and the connection state
 * machine. Successful frame construction is never evidence of association.
 */
int net_wifi_80211_parse_scan_frame(const uint8_t *frame, size_t length,
                                    int8_t rssi_dbm,
                                    net_wifi_scan_result_t *result);

/* Build an Open System Authentication request (30 bytes). */
int net_wifi_80211_build_open_auth_request(uint8_t *frame, size_t capacity,
                                           const uint8_t destination[6],
                                           const uint8_t source[6],
                                           const uint8_t bssid[6],
                                           uint16_t sequence,
                                           size_t *frame_length);

/*
 * Build an association request with caller-supplied, complete RSN IE (including
 * Element ID and length). SSID and RSN IE are length-checked; rates are the
 * mandatory legacy 2.4-GHz rates. Returns 1 on success.
 */
int net_wifi_80211_build_assoc_request(uint8_t *frame, size_t capacity,
                                       const uint8_t destination[6],
                                       const uint8_t source[6],
                                       const uint8_t bssid[6],
                                       uint16_t sequence,
                                       const uint8_t *ssid,
                                       size_t ssid_length,
                                       const uint8_t *rsn_ie,
                                       size_t rsn_ie_length,
                                       size_t *frame_length);

/* Parse Open System Authentication response (subtype 0xB0). */
int net_wifi_80211_parse_auth_response(const uint8_t *frame, size_t length,
                                        const uint8_t expected_bssid[6],
                                        uint16_t *status_code);

/* Parse Association Response (subtype 0x10); AID is returned on success. */
int net_wifi_80211_parse_assoc_response(const uint8_t *frame, size_t length,
                                         const uint8_t expected_bssid[6],
                                         uint16_t *status_code,
                                         uint16_t *association_id);

#endif
