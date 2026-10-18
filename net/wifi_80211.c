#include "net_wifi_80211.h"

#include <stddef.h>
#include <stdint.h>

#define IEEE80211_HEADER_SIZE 24U
#define IEEE80211_BEACON_FIXED_SIZE 12U
#define IEEE80211_FC_TYPE_MASK 0x0CU
#define IEEE80211_FC_SUBTYPE_MASK 0xF0U
#define IEEE80211_FC_MANAGEMENT 0x00U
#define IEEE80211_SUBTYPE_BEACON 0x80U
#define IEEE80211_SUBTYPE_PROBE_RESPONSE 0x50U
#define IEEE80211_IE_SSID 0U
#define IEEE80211_IE_DS_PARAMETER_SET 3U
#define IEEE80211_IE_HT_OPERATION 61U
#define IEEE80211_IE_RSN 48U

static int mac_is_valid_bssid(const uint8_t *mac)
{
    uint8_t any = 0U;
    uint8_t all_ff = 1U;

    for (size_t i = 0; i < NET_WIFI_BSSID_SIZE; ++i) {
        any |= mac[i];
        if (mac[i] != 0xFFU)
            all_ff = 0U;
    }
    return any != 0U && all_ff == 0U && (mac[0] & 1U) == 0U;
}

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static int suite_is(const uint8_t *suite, uint8_t type)
{
    return suite[0] == 0x00U && suite[1] == 0x0FU &&
           suite[2] == 0xACU && suite[3] == type;
}

static int cipher_supported(const uint8_t *suite)
{
    return suite_is(suite, 4U) || suite_is(suite, 8U) ||
           suite_is(suite, 9U);
}

/* Structural RSN parser. It never treats a malformed or legacy WPA IE as
 * WPA2/WPA3. RSN capability bits 6/7 are MFPR/MFPC respectively. */
static net_wifi_security_t parse_rsn(const uint8_t *p, size_t length)
{
    size_t offset = 0U;
    uint16_t pairwise_count, akm_count, capabilities = 0U;
    int pairwise_ok = 0, psk = 0, sae = 0;

    if (!p || length < 2U || read_le16(p) != 1U)
        return NET_WIFI_SECURITY_UNKNOWN;
    offset = 2U;

    if (length - offset < 4U || !cipher_supported(p + offset))
        return NET_WIFI_SECURITY_UNKNOWN;
    offset += 4U;

    if (length - offset < 2U)
        return NET_WIFI_SECURITY_UNKNOWN;
    pairwise_count = read_le16(p + offset);
    offset += 2U;
    if (pairwise_count == 0U || pairwise_count > (length - offset) / 4U)
        return NET_WIFI_SECURITY_UNKNOWN;
    for (uint16_t i = 0U; i < pairwise_count; ++i) {
        if (cipher_supported(p + offset))
            pairwise_ok = 1;
        offset += 4U;
    }
    if (!pairwise_ok || length - offset < 2U)
        return NET_WIFI_SECURITY_UNKNOWN;

    akm_count = read_le16(p + offset);
    offset += 2U;
    if (akm_count == 0U || akm_count > (length - offset) / 4U)
        return NET_WIFI_SECURITY_UNKNOWN;
    for (uint16_t i = 0U; i < akm_count; ++i) {
        if (suite_is(p + offset, 2U))
            psk = 1;
        if (suite_is(p + offset, 8U))
            sae = 1;
        offset += 4U;
    }
    if (length - offset >= 2U)
        capabilities = read_le16(p + offset);

    /* SAE is only classified as WPA3 when the AP advertises both required
     * management-frame protection capability and requirement bits. */
    if (sae && (capabilities & 0x00C0U) == 0x00C0U)
        return NET_WIFI_SECURITY_WPA3_SAE;
    if (psk)
        return NET_WIFI_SECURITY_WPA2_PSK;
    return NET_WIFI_SECURITY_UNKNOWN;
}

int net_wifi_80211_parse_scan_frame(const uint8_t *frame, size_t length,
                                    int8_t rssi_dbm,
                                    net_wifi_scan_result_t *result)
{
    size_t offset;
    int ssid_seen = 0, rsn_seen = 0;
    uint8_t ds_channel = 0U, ht_channel = 0U;
    net_wifi_security_t security = NET_WIFI_SECURITY_UNKNOWN;

    if (!frame || !result ||
        length < IEEE80211_HEADER_SIZE + IEEE80211_BEACON_FIXED_SIZE)
        return 0;

    if ((frame[0] & IEEE80211_FC_TYPE_MASK) != IEEE80211_FC_MANAGEMENT ||
        ((frame[0] & IEEE80211_FC_SUBTYPE_MASK) != IEEE80211_SUBTYPE_BEACON &&
         (frame[0] & IEEE80211_FC_SUBTYPE_MASK) != IEEE80211_SUBTYPE_PROBE_RESPONSE) ||
        (frame[1] & 0x03U) != 0U)
        return 0;
    if (!mac_is_valid_bssid(frame + 16U))
        return 0;

    offset = IEEE80211_HEADER_SIZE + IEEE80211_BEACON_FIXED_SIZE;
    while (offset < length) {
        uint8_t id, ie_length;
        const uint8_t *value;

        if (length - offset < 2U)
            return 0;
        id = frame[offset];
        ie_length = frame[offset + 1U];
        offset += 2U;
        if ((size_t)ie_length > length - offset)
            return 0;
        value = frame + offset;

        if (id == IEEE80211_IE_SSID) {
            if (ssid_seen || ie_length > NET_WIFI_SSID_MAX)
                return 0;
            ssid_seen = 1;
            result->ssid_length = ie_length;
            for (size_t i = 0U; i < ie_length; ++i) {
                if (value[i] == 0U || value[i] < 0x20U || value[i] == 0x7FU)
                    return 0;
                result->ssid[i] = value[i];
            }
        } else if (id == IEEE80211_IE_DS_PARAMETER_SET) {
            if (ie_length != 1U || ds_channel != 0U ||
                value[0] == 0U || value[0] > 196U)
                return 0;
            ds_channel = value[0];
        } else if (id == IEEE80211_IE_HT_OPERATION) {
            if (ie_length < 1U || ht_channel != 0U ||
                value[0] == 0U || value[0] > 196U)
                return 0;
            ht_channel = value[0];
        } else if (id == IEEE80211_IE_RSN) {
            if (rsn_seen)
                return 0;
            rsn_seen = 1;
            security = parse_rsn(value, ie_length);
            if (security == NET_WIFI_SECURITY_UNKNOWN)
                return 0;
        }

        offset += ie_length;
    }

    if (!ssid_seen || (ds_channel && ht_channel && ds_channel != ht_channel))
        return 0;
    {
        uint8_t channel = ds_channel ? ds_channel : ht_channel;
        if (channel == 0U)
            return 0;
        for (size_t i = 0U; i < NET_WIFI_BSSID_SIZE; ++i)
            result->bssid[i] = frame[16U + i];
        result->channel = channel;
        result->rssi_dbm = rssi_dbm;
        result->security = security;
    }
    return 1;
}
