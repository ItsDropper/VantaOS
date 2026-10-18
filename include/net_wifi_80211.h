#ifndef VANTAOS_NET_WIFI_80211_H
#define VANTAOS_NET_WIFI_80211_H

#include "net_wifi.h"
#include <stddef.h>
#include <stdint.h>

/*
 * Bounded parser for IEEE 802.11 Beacon and Probe Response management frames.
 * It does not transmit frames, implement a radio, or authenticate an AP.
 * A chipset driver supplies a complete received management frame and RSSI.
 *
 * Returns 1 for a structurally valid Beacon/Probe Response and fills result;
 * returns 0 for unsupported frame types, malformed fields, or invalid bounds.
 */
int net_wifi_80211_parse_scan_frame(const uint8_t *frame, size_t length,
                                    int8_t rssi_dbm,
                                    net_wifi_scan_result_t *result);

#endif
