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

/* Structural RSN parser. RSN capability bits 6/7 are MFPR/MFPC. */
static int parse_rsn(const uint8_t *p, size_t length,
                      net_wifi_security_t *security)
{
    size_t offset = 0U;
    uint16_t pairwise_count, akm_count, capabilities = 0U;
    int pairwise_ok = 0, psk = 0, sae = 0;

    if (!p || !security || length < 2U || read_le16(p) != 1U)
        return 0;
    offset = 2U;
    if (length - offset < 4U || !cipher_supported(p + offset))
        return 0;
    offset += 4U;
    if (length - offset < 2U)
        return 0;
    pairwise_count = read_le16(p + offset);
    offset += 2U;
    if (pairwise_count == 0U || pairwise_count > (length - offset) / 4U)
        return 0;
    for (uint16_t i = 0U; i < pairwise_count; ++i) {
        if (cipher_supported(p + offset))
            pairwise_ok = 1;
        offset += 4U;
    }
    if (!pairwise_ok || length - offset < 2U)
        return 0;
    akm_count = read_le16(p + offset);
    offset += 2U;
    if (akm_count == 0U || akm_count > (length - offset) / 4U)
        return 0;
    for (uint16_t i = 0U; i < akm_count; ++i) {
        if (suite_is(p + offset, 2U))
            psk = 1;
        if (suite_is(p + offset, 8U))
            sae = 1;
        offset += 4U;
    }
    if (length - offset == 1U)
        return 0;
    if (length - offset >= 2U) {
        capabilities = read_le16(p + offset);
        offset += 2U;
    }
    /* Parse optional PMKID count/list and group-management cipher exactly;
     * unknown optional suites are not claimed as a supported security mode. */
    if (length != offset) {
        uint16_t pmkid_count;
        if (length - offset < 2U)
            return 0;
        pmkid_count = read_le16(p + offset);
        offset += 2U;
        if (pmkid_count > (length - offset) / 16U)
            return 0;
        offset += (size_t)pmkid_count * 16U;
        if (length - offset == 4U) {
            const uint8_t *mgmt = p + offset;
            if (mgmt[0] != 0x00U || mgmt[1] != 0x0FU ||
                mgmt[2] != 0xACU ||
                (mgmt[3] != 6U && mgmt[3] != 11U &&
                 mgmt[3] != 12U && mgmt[3] != 13U))
                return 0;
            offset += 4U;
        }
        if (offset != length)
            return 0;
    }

    *security = NET_WIFI_SECURITY_UNKNOWN;
    if (sae && (capabilities & 0x00C0U) == 0x00C0U)
        *security = NET_WIFI_SECURITY_WPA3_SAE;
    else if (psk)
        *security = NET_WIFI_SECURITY_WPA2_PSK;
    /* A structurally valid RSN element with an unknown AKM remains a visible
     * scan result, but is never mislabeled as WPA2/WPA3. */
    return 1;
}

int net_wifi_80211_parse_scan_frame(const uint8_t *frame, size_t length,
                                    int8_t rssi_dbm,
                                    net_wifi_scan_result_t *result)
{
    size_t offset;
    int ssid_seen = 0, rsn_seen = 0;
    uint8_t ds_channel = 0U, ht_channel = 0U;
    net_wifi_scan_result_t parsed = {0};
    net_wifi_security_t security = NET_WIFI_SECURITY_UNKNOWN;

    if (!frame || !result ||
        length < IEEE80211_HEADER_SIZE + IEEE80211_BEACON_FIXED_SIZE)
        return 0;
    if ((frame[0] & IEEE80211_FC_TYPE_MASK) != IEEE80211_FC_MANAGEMENT ||
        ((frame[0] & IEEE80211_FC_SUBTYPE_MASK) != IEEE80211_SUBTYPE_BEACON &&
         (frame[0] & IEEE80211_FC_SUBTYPE_MASK) != IEEE80211_SUBTYPE_PROBE_RESPONSE) ||
        (frame[1] & 0x03U) != 0U ||
        !mac_is_valid_bssid(frame + 16U))
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
            parsed.ssid_length = ie_length;
            for (size_t i = 0U; i < ie_length; ++i) {
                if (value[i] == 0U || value[i] < 0x20U || value[i] == 0x7FU)
                    return 0;
                parsed.ssid[i] = value[i];
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
            if (!parse_rsn(value, ie_length, &security))
                return 0;
        }
        offset += ie_length;
    }

    if (!ssid_seen || (ds_channel && ht_channel && ds_channel != ht_channel))
        return 0;
    parsed.channel = ds_channel ? ds_channel : ht_channel;
    if (parsed.channel == 0U)
        return 0;
    for (size_t i = 0U; i < NET_WIFI_BSSID_SIZE; ++i)
        parsed.bssid[i] = frame[16U + i];
    parsed.rssi_dbm = rssi_dbm;
    parsed.security = security;
    *result = parsed;
    return 1;
}


#define IEEE80211_SUBTYPE_AUTH 0xB0U
#define IEEE80211_SUBTYPE_ASSOC_RESPONSE 0x10U
#define IEEE80211_SUBTYPE_ASSOC_REQUEST 0x00U
#define IEEE80211_SUBTYPE_AUTH_REQUEST 0xB0U
#define IEEE80211_FC_PROTECTED 0x40U
#define IEEE80211_MIN_MGMT_HEADER 24U

static void write_le16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static int mac_is_unicast(const uint8_t *mac)
{
    uint8_t any = 0U;
    if (!mac || (mac[0] & 1U) != 0U)
        return 0;
    for (size_t i = 0U; i < 6U; ++i)
        any |= mac[i];
    return any != 0U;
}

static void fill_mgmt_header(uint8_t *frame, uint8_t subtype,
                             const uint8_t destination[6],
                             const uint8_t source[6],
                             const uint8_t bssid[6], uint16_t sequence)
{
    frame[0] = subtype;
    frame[1] = 0U;
    for (size_t i = 0U; i < 6U; ++i) {
        frame[4U + i] = destination[i];
        frame[10U + i] = source[i];
        frame[16U + i] = bssid[i];
    }
    write_le16(frame + 22U, (uint16_t)((sequence & 0x0FFFU) << 4));
}

int net_wifi_80211_build_open_auth_request(uint8_t *frame, size_t capacity,
                                           const uint8_t destination[6],
                                           const uint8_t source[6],
                                           const uint8_t bssid[6],
                                           uint16_t sequence,
                                           size_t *frame_length)
{
    if (!frame || capacity < 30U || !destination || !source || !bssid ||
        !mac_is_unicast(destination) || !mac_is_unicast(source) ||
        !mac_is_unicast(bssid) || !frame_length || sequence > 0x0FFFU)
        return 0;
    fill_mgmt_header(frame, IEEE80211_SUBTYPE_AUTH_REQUEST, destination,
                     source, bssid, sequence);
    write_le16(frame + 24U, 0U); /* Open System authentication algorithm. */
    write_le16(frame + 26U, 1U); /* Transaction sequence number. */
    write_le16(frame + 28U, 0U);
    *frame_length = 30U;
    return 1;
}

int net_wifi_80211_build_assoc_request(uint8_t *frame, size_t capacity,
                                       const uint8_t destination[6],
                                       const uint8_t source[6],
                                       const uint8_t bssid[6],
                                       uint16_t sequence,
                                       const uint8_t *ssid,
                                       size_t ssid_length,
                                       const uint8_t *rsn_ie,
                                       size_t rsn_ie_length,
                                       size_t *frame_length)
{
    static const uint8_t rates[] = {
        0x01U, 0x08U, 0x82U, 0x84U, 0x8BU, 0x96U,
        0x0CU, 0x12U, 0x18U, 0x24U
    };
    const size_t fixed_and_ies = 4U + 2U + sizeof(rates);
    size_t required;
    if (!frame || !destination || !source || !bssid || !ssid || !frame_length ||
        !mac_is_unicast(destination) || !mac_is_unicast(source) ||
        !mac_is_unicast(bssid) || sequence > 0x0FFFU ||
        ssid_length == 0U || ssid_length > NET_WIFI_SSID_MAX ||
        !rsn_ie || rsn_ie_length < 2U || rsn_ie_length > 257U ||
        rsn_ie[0] != IEEE80211_IE_RSN ||
        (size_t)rsn_ie[1] + 2U != rsn_ie_length ||
        rsn_ie_length > SIZE_MAX - IEEE80211_MIN_MGMT_HEADER - fixed_and_ies ||
        ssid_length > SIZE_MAX - IEEE80211_MIN_MGMT_HEADER - fixed_and_ies - rsn_ie_length)
        return 0;
    required = IEEE80211_MIN_MGMT_HEADER + fixed_and_ies + ssid_length + rsn_ie_length;
    if (capacity < required)
        return 0;

    fill_mgmt_header(frame, IEEE80211_SUBTYPE_ASSOC_REQUEST, destination,
                     source, bssid, sequence);
    write_le16(frame + 24U, 0x0431U); /* ESS + short preamble + short slot. */
    write_le16(frame + 26U, 10U);     /* Listen interval. */
    size_t offset = 28U;
    frame[offset++] = IEEE80211_IE_SSID;
    frame[offset++] = (uint8_t)ssid_length;
    for (size_t i = 0U; i < ssid_length; ++i) {
        if (ssid[i] == 0U || ssid[i] < 0x20U || ssid[i] == 0x7FU)
            return 0;
        frame[offset++] = ssid[i];
    }
    for (size_t i = 0U; i < sizeof(rates); ++i)
        frame[offset++] = rates[i];
    for (size_t i = 0U; i < rsn_ie_length; ++i)
        frame[offset++] = rsn_ie[i];
    *frame_length = offset;
    return 1;
}

static int parse_mgmt_response(const uint8_t *frame, size_t length,
                               uint8_t expected_subtype,
                               const uint8_t expected_bssid[6])
{
    if (!frame || !expected_bssid || length < IEEE80211_MIN_MGMT_HEADER ||
        (frame[0] & IEEE80211_FC_TYPE_MASK) != IEEE80211_FC_MANAGEMENT ||
        (frame[0] & IEEE80211_FC_SUBTYPE_MASK) != expected_subtype ||
        (frame[1] & (IEEE80211_FC_PROTECTED | 0x03U)) != 0U)
        return 0;
    for (size_t i = 0U; i < 6U; ++i) {
        if (frame[10U + i] != expected_bssid[i] ||
            frame[16U + i] != expected_bssid[i])
            return 0;
    }
    return 1;
}

int net_wifi_80211_parse_auth_response(const uint8_t *frame, size_t length,
                                        const uint8_t expected_bssid[6],
                                        uint16_t *status_code)
{
    if (!status_code || length < 30U ||
        !parse_mgmt_response(frame, length, IEEE80211_SUBTYPE_AUTH,
                             expected_bssid) ||
        read_le16(frame + 24U) != 0U || read_le16(frame + 26U) != 2U)
        return 0;
    *status_code = read_le16(frame + 28U);
    return 1;
}

int net_wifi_80211_parse_assoc_response(const uint8_t *frame, size_t length,
                                         const uint8_t expected_bssid[6],
                                         uint16_t *status_code,
                                         uint16_t *association_id)
{
    if (!status_code || !association_id || length < 30U ||
        !parse_mgmt_response(frame, length, IEEE80211_SUBTYPE_ASSOC_RESPONSE,
                             expected_bssid))
        return 0;
    *status_code = read_le16(frame + 26U);
    *association_id = (uint16_t)(read_le16(frame + 28U) & 0x3FFFU);
    if (*status_code != 0U)
        *association_id = 0U;
    return 1;
}

int net_wifi_80211_snap_encapsulate(const uint8_t *ethernet_frame,
                                    size_t ethernet_length,
                                    uint8_t *llc_payload,
                                    size_t llc_capacity,
                                    size_t *llc_length)
{
    size_t payload_length;
    if (llc_length) *llc_length = 0U;
    if (!ethernet_frame || !llc_payload || !llc_length ||
        ethernet_length < 14U)
        return 0;
    payload_length = ethernet_length - 14U;
    if (payload_length > 2304U || llc_capacity < payload_length + 8U)
        return 0;

    llc_payload[0] = 0xAAU;
    llc_payload[1] = 0xAAU;
    llc_payload[2] = 0x03U;
    llc_payload[3] = 0x00U;
    llc_payload[4] = 0x00U;
    llc_payload[5] = 0x00U;
    llc_payload[6] = ethernet_frame[12];
    llc_payload[7] = ethernet_frame[13];
    for (size_t i = 0U; i < payload_length; ++i)
        llc_payload[8U + i] = ethernet_frame[14U + i];
    *llc_length = payload_length + 8U;
    return 1;
}

int net_wifi_80211_snap_decapsulate(const uint8_t destination[6],
                                    const uint8_t source[6],
                                    const uint8_t *llc_payload,
                                    size_t llc_length,
                                    uint8_t *ethernet_frame,
                                    size_t ethernet_capacity,
                                    size_t *ethernet_length)
{
    size_t payload_length;
    if (ethernet_length) *ethernet_length = 0U;
    if (!destination || !source || !llc_payload || !ethernet_frame ||
        !ethernet_length || llc_length < 8U ||
        llc_payload[0] != 0xAAU || llc_payload[1] != 0xAAU ||
        llc_payload[2] != 0x03U || llc_payload[3] != 0U ||
        llc_payload[4] != 0U || llc_payload[5] != 0U)
        return 0;
    payload_length = llc_length - 8U;
    if (payload_length > 2304U || ethernet_capacity < payload_length + 14U)
        return 0;

    for (size_t i = 0U; i < 6U; ++i) {
        ethernet_frame[i] = destination[i];
        ethernet_frame[6U + i] = source[i];
    }
    ethernet_frame[12] = llc_payload[6];
    ethernet_frame[13] = llc_payload[7];
    for (size_t i = 0U; i < payload_length; ++i)
        ethernet_frame[14U + i] = llc_payload[8U + i];
    *ethernet_length = payload_length + 14U;
    return 1;
}
