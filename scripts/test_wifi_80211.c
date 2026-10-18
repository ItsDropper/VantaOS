#include "net_wifi_80211.h"

#include <stdio.h>
#include <string.h>

#define FRAME_CAPACITY 160U
#define HEADER_FIXED 36U

static void put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static size_t make_frame(uint8_t *f, uint8_t subtype,
                         net_wifi_security_t security)
{
    size_t n = HEADER_FIXED;
    memset(f, 0, FRAME_CAPACITY);
    f[0] = subtype;
    f[4] = 0x02; f[5] = 0x11; f[6] = 0x22;
    f[7] = 0x33; f[8] = 0x44; f[9] = 0x55;
    memcpy(f + 16, f + 4, 6);

    f[n++] = 0U; f[n++] = 4U;
    memcpy(f + n, "test", 4U); n += 4U;
    f[n++] = 3U; f[n++] = 1U; f[n++] = 6U;

    if (security == NET_WIFI_SECURITY_WPA2_PSK ||
        security == NET_WIFI_SECURITY_WPA3_SAE) {
        uint8_t rsn[20];
        size_t r = 0U;
        put_le16(rsn + r, 1U); r += 2U;
        rsn[r++] = 0x00; rsn[r++] = 0x0F; rsn[r++] = 0xAC; rsn[r++] = 4U;
        put_le16(rsn + r, 1U); r += 2U;
        rsn[r++] = 0x00; rsn[r++] = 0x0F; rsn[r++] = 0xAC; rsn[r++] = 4U;
        put_le16(rsn + r, 1U); r += 2U;
        rsn[r++] = 0x00; rsn[r++] = 0x0F; rsn[r++] = 0xAC;
        rsn[r++] = security == NET_WIFI_SECURITY_WPA3_SAE ? 8U : 2U;
        put_le16(rsn + r, security == NET_WIFI_SECURITY_WPA3_SAE ? 0x00C0U : 0U);
        r += 2U;
        f[n++] = 48U; f[n++] = (uint8_t)r;
        memcpy(f + n, rsn, r); n += r;
    }
    return n;
}

int main(void)
{
    uint8_t frame[FRAME_CAPACITY];
    net_wifi_scan_result_t result;
    size_t n = make_frame(frame, 0x80U, NET_WIFI_SECURITY_WPA2_PSK);

    memset(&result, 0xA5, sizeof(result));
    if (!net_wifi_80211_parse_scan_frame(frame, n, -42, &result) ||
        result.ssid_length != 4U || memcmp(result.ssid, "test", 4U) != 0 ||
        result.channel != 6U || result.rssi_dbm != -42 ||
        result.security != NET_WIFI_SECURITY_WPA2_PSK ||
        memcmp(result.bssid, frame + 16U, 6U) != 0)
        return 1;

    n = make_frame(frame, 0x50U, NET_WIFI_SECURITY_WPA3_SAE);
    if (!net_wifi_80211_parse_scan_frame(frame, n, -60, &result) ||
        result.security != NET_WIFI_SECURITY_WPA3_SAE)
        return 2;

    n = make_frame(frame, 0x80U, NET_WIFI_SECURITY_UNKNOWN);
    if (!net_wifi_80211_parse_scan_frame(frame, n, -70, &result) ||
        result.security != NET_WIFI_SECURITY_UNKNOWN)
        return 3;
    if (net_wifi_80211_parse_scan_frame(frame, 10U, -40, &result))
        return 4;
    frame[0] = 0x08U;
    if (net_wifi_80211_parse_scan_frame(frame, n, -40, &result))
        return 5;

    n = make_frame(frame, 0x80U, NET_WIFI_SECURITY_WPA2_PSK);
    frame[HEADER_FIXED + 1U] = 0xFFU;
    if (net_wifi_80211_parse_scan_frame(frame, n, -40, &result))
        return 6;
    n = make_frame(frame, 0x80U, NET_WIFI_SECURITY_WPA2_PSK);
    frame[16] |= 1U;
    if (net_wifi_80211_parse_scan_frame(frame, n, -40, &result))
        return 7;

    n = make_frame(frame, 0x80U, NET_WIFI_SECURITY_WPA3_SAE);
    frame[n - 2U] = 0U;
    frame[n - 1U] = 0U;
    if (!net_wifi_80211_parse_scan_frame(frame, n, -40, &result) ||
        result.security != NET_WIFI_SECURITY_UNKNOWN)
        return 8;

    {
        const uint8_t ap[6] = {0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
        const uint8_t sta[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
        const uint8_t ssid[] = {'t', 'e', 's', 't'};
        const uint8_t rsn[] = {
            48U, 20U, 1U, 0U, 0U, 15U, 172U, 4U,
            1U, 0U, 0U, 15U, 172U, 4U,
            1U, 0U, 0U, 15U, 172U, 2U, 0U, 0U
        };
        size_t frame_length = 0U;
        uint16_t status = 0xFFFFU, aid = 0xFFFFU;
        memset(frame, 0xA5, sizeof(frame));
        if (!net_wifi_80211_build_open_auth_request(frame, sizeof(frame),
                ap, sta, ap, 7U, &frame_length) || frame_length != 30U ||
            frame[0] != 0xB0U || memcmp(frame + 4U, ap, 6U) != 0 ||
            memcmp(frame + 10U, sta, 6U) != 0 ||
            memcmp(frame + 16U, ap, 6U) != 0 ||
            frame[24] != 0U || frame[26] != 1U)
            return 9;
        if (net_wifi_80211_build_open_auth_request(frame, 29U, ap, sta, ap,
                0U, &frame_length))
            return 10;
        if (!net_wifi_80211_build_assoc_request(frame, sizeof(frame), ap, sta,
                ap, 8U, ssid, sizeof(ssid), rsn, sizeof(rsn), &frame_length) ||
            frame[0] != 0U || memcmp(frame + 4U, ap, 6U) != 0 ||
            frame[28] != 0U || frame[29] != sizeof(ssid) ||
            memcmp(frame + 30U, ssid, sizeof(ssid)) != 0)
            return 11;
        if (net_wifi_80211_build_assoc_request(frame, 32U, ap, sta, ap, 0U,
                ssid, sizeof(ssid), rsn, sizeof(rsn), &frame_length))
            return 12;

        memset(frame, 0, sizeof(frame));
        frame[0] = 0xB0U;
        memcpy(frame + 4U, sta, 6U);
        memcpy(frame + 10U, ap, 6U);
        memcpy(frame + 16U, ap, 6U);
        put_le16(frame + 24U, 0U);
        put_le16(frame + 26U, 2U);
        put_le16(frame + 28U, 0U);
        if (!net_wifi_80211_parse_auth_response(frame, 30U, ap, &status) ||
            status != 0U)
            return 13;
        frame[28] = 1U;
        if (!net_wifi_80211_parse_auth_response(frame, 30U, ap, &status) ||
            status != 1U)
            return 14;
        frame[10] ^= 1U;
        if (net_wifi_80211_parse_auth_response(frame, 30U, ap, &status))
            return 15;

        memset(frame, 0, sizeof(frame));
        frame[0] = 0x10U;
        memcpy(frame + 4U, sta, 6U);
        memcpy(frame + 10U, ap, 6U);
        memcpy(frame + 16U, ap, 6U);
        put_le16(frame + 26U, 0U);
        put_le16(frame + 28U, 0xC123U);
        if (!net_wifi_80211_parse_assoc_response(frame, 30U, ap, &status, &aid) ||
            status != 0U || aid != 0x0123U)
            return 16;
        put_le16(frame + 26U, 17U);
        if (!net_wifi_80211_parse_assoc_response(frame, 30U, ap, &status, &aid) ||
            status != 17U || aid != 0U)
            return 17;
        if (net_wifi_80211_parse_assoc_response(frame, 29U, ap, &status, &aid))
            return 18;
    }

    puts("802.11 scan/auth/association frame tests passed.");
    return 0;
}
