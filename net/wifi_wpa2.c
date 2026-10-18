#include "wifi_wpa2.h"
#include "wifi_ccmp.h"

#include <stddef.h>
#include <stdint.h>

#define EAPOL_HEADER_SIZE 4U
#define EAPOL_TYPE_KEY 3U
#define EAPOL_KEY_DESCRIPTOR_RSN 2U
#define EAPOL_KEY_FIXED_SIZE 99U
#define EAPOL_MIC_OFFSET 81U
#define EAPOL_KEY_DATA_LENGTH_OFFSET 97U
#define EAPOL_KEY_DATA_OFFSET 99U
#define KEY_INFO_VERSION_MASK 0x0007U
#define KEY_INFO_PAIRWISE 0x0008U
#define KEY_INFO_INSTALL 0x0040U
#define KEY_INFO_ACK 0x0080U
#define KEY_INFO_MIC 0x0100U
#define KEY_INFO_SECURE 0x0200U
#define KEY_INFO_ENCRYPTED_DATA 0x1000U
#define KEY_INFO_ALLOWED (KEY_INFO_VERSION_MASK | KEY_INFO_PAIRWISE | KEY_INFO_INSTALL | KEY_INFO_ACK | KEY_INFO_MIC | KEY_INFO_SECURE | KEY_INFO_ENCRYPTED_DATA)
#define EAPOL_MESSAGE_MAX WIFI_WPA2_EAPOL_MAX

static void secure_zero(void *memory, size_t length)
{
    volatile uint8_t *p = (volatile uint8_t *)memory;
    while (length-- != 0U) *p++ = 0U;
}

static uint16_t get_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void put_be16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static int mac_equal(const uint8_t *a, const uint8_t *b)
{
    uint8_t different = 0U;
    for (size_t i = 0U; i < 6U; ++i) different |= (uint8_t)(a[i] ^ b[i]);
    return different == 0U;
}

static int rsn_ie_is_wpa2_psk_ccmp(const uint8_t *ie, size_t length)
{
    /* Accept only a minimal RSN IE selecting CCMP-128 + PSK, without PMF.
       The supplicant does not implement SAE, TKIP, or management-frame
       protection, so silently accepting those suites would be unsafe. */
    static const uint8_t ccmp[4] = {0x00U, 0x0fU, 0xacU, 0x04U};
    static const uint8_t psk[4] = {0x00U, 0x0fU, 0xacU, 0x02U};
    if (!ie || length != 22U || ie[0] != 48U || ie[1] != 20U ||
        ie[2] != 1U || ie[3] != 0U)
        return 0;
    for (size_t i = 0U; i < 4U; ++i)
        if (ie[4U + i] != ccmp[i] || ie[10U + i] != ccmp[i] ||
            ie[16U + i] != psk[i])
            return 0;
    return ie[8] == 1U && ie[9] == 0U && ie[14] == 1U &&
           ie[15] == 0U && ie[20] == 0U && ie[21] == 0U;
}

static int replay_greater(const uint8_t a[8], const uint8_t b[8])
{
    for (size_t i = 0U; i < 8U; ++i) {
        if (a[i] > b[i]) return 1;
        if (a[i] < b[i]) return 0;
    }
    return 0;
}

static void fail_session(wifi_wpa2_supplicant_t *s)
{
    secure_zero(s->pmk, sizeof(s->pmk));
    secure_zero(s->ptk, sizeof(s->ptk));
    secure_zero(s->anonce, sizeof(s->anonce));
    secure_zero(s->snonce, sizeof(s->snonce));
    s->have_ptk = 0U;
    s->state = WIFI_WPA2_FAILED;
}

static int calculate_mic(const uint8_t kck[16], const uint8_t *frame,
                         size_t length, uint8_t mic[16])
{
    uint8_t copy[EAPOL_MESSAGE_MAX], digest[20];
    int ok;
    if (!frame || length < EAPOL_KEY_FIXED_SIZE ||
        length > sizeof(copy) || EAPOL_MIC_OFFSET + 16U > length)
        return 0;
    for (size_t i = 0U; i < length; ++i) copy[i] = frame[i];
    for (size_t i = 0U; i < 16U; ++i) copy[EAPOL_MIC_OFFSET + i] = 0U;
    ok = wifi_hmac_sha1(kck, 16U, copy, length, digest);
    if (ok) for (size_t i = 0U; i < 16U; ++i) mic[i] = digest[i];
    secure_zero(copy, sizeof(copy));
    secure_zero(digest, sizeof(digest));
    return ok;
}

static int verify_mic(const uint8_t kck[16], const uint8_t *frame, size_t length)
{
    uint8_t expected[16], different = 0U;
    int ok = calculate_mic(kck, frame, length, expected);
    if (ok) {
        for (size_t i = 0U; i < 16U; ++i)
            different |= (uint8_t)(expected[i] ^ frame[EAPOL_MIC_OFFSET + i]);
    }
    secure_zero(expected, sizeof(expected));
    return ok && different == 0U;
}

static int send_key_message(wifi_wpa2_supplicant_t *s, uint16_t key_info,
                            const uint8_t nonce[32], const uint8_t *key_data,
                            size_t key_data_length)
{
    uint8_t frame[EAPOL_MESSAGE_MAX], mic[16];
    size_t total = EAPOL_KEY_FIXED_SIZE + key_data_length;
    int sent = 0;
    if (!s || !s->ops.send_eapol || key_data_length > WIFI_WPA2_RSN_IE_MAX ||
        total > sizeof(frame) || key_data_length > 0xffffU)
        return 0;

    for (size_t i = 0U; i < total; ++i) frame[i] = 0U;
    frame[0] = 2U; /* EAPOL version 2 */
    frame[1] = EAPOL_TYPE_KEY;
    put_be16(frame + 2U, (uint16_t)(total - EAPOL_HEADER_SIZE));
    frame[4] = EAPOL_KEY_DESCRIPTOR_RSN;
    put_be16(frame + 5U, key_info);
    put_be16(frame + 7U, 16U); /* CCMP-128 temporal key */
    for (size_t i = 0U; i < 8U; ++i) frame[9U + i] = s->replay_counter[i];
    if (nonce) for (size_t i = 0U; i < 32U; ++i) frame[17U + i] = nonce[i];
    put_be16(frame + EAPOL_KEY_DATA_LENGTH_OFFSET, (uint16_t)key_data_length);
    if (key_data_length != 0U)
        for (size_t i = 0U; i < key_data_length; ++i)
            frame[EAPOL_KEY_DATA_OFFSET + i] = key_data[i];

    if (!calculate_mic(s->ptk, frame, total, mic)) goto done;
    for (size_t i = 0U; i < sizeof(mic); ++i) frame[EAPOL_MIC_OFFSET + i] = mic[i];
    sent = s->ops.send_eapol(s->bssid, frame, total, s->ops.context);
done:
    secure_zero(frame, sizeof(frame));
    secure_zero(mic, sizeof(mic));
    return sent;
}

int wifi_wpa2_begin(wifi_wpa2_supplicant_t *s, const wifi_wpa2_ops_t *ops,
                    const uint8_t *ssid, size_t ssid_length,
                    const uint8_t *passphrase, size_t passphrase_length,
                    const uint8_t local_mac[6], const uint8_t bssid[6],
                    const uint8_t *rsn_ie, size_t rsn_ie_length)
{
    if (!s || !ops || !ops->random_bytes || !ops->send_eapol ||
        !ops->install_ccmp_keys || !ssid || ssid_length == 0U ||
        ssid_length > 32U || !passphrase || passphrase_length < 8U ||
        passphrase_length > 63U || !local_mac || !bssid || !rsn_ie ||
        rsn_ie_length > WIFI_WPA2_RSN_IE_MAX ||
        !rsn_ie_is_wpa2_psk_ccmp(rsn_ie, rsn_ie_length) ||
        (local_mac[0] & 1U) != 0U || (bssid[0] & 1U) != 0U)
        return 0;

    for (size_t i = 0U; i < passphrase_length; ++i) {
        if (passphrase[i] < 0x20U || passphrase[i] > 0x7eU) return 0;
    }
    wifi_wpa2_reset(s);
    s->ops = *ops;
    for (size_t i = 0U; i < 6U; ++i) {
        s->local_mac[i] = local_mac[i];
        s->bssid[i] = bssid[i];
    }
    for (size_t i = 0U; i < rsn_ie_length; ++i) s->rsn_ie[i] = rsn_ie[i];
    s->rsn_ie_length = rsn_ie_length;
    if (!wifi_pbkdf2_sha1(passphrase, passphrase_length, ssid, ssid_length,
                          WIFI_PBKDF2_SHA1_ITERATIONS, s->pmk, sizeof(s->pmk))) {
        fail_session(s);
        return 0;
    }
    s->state = WIFI_WPA2_WAIT_M1;
    return 1;
}

static int parse_gtk_kde(const uint8_t *data, size_t length,
                         uint8_t gtk[WIFI_WPA2_GTK_MAX], size_t *gtk_length,
                         uint8_t *gtk_id)
{
    size_t offset = 0U;
    *gtk_length = 0U;
    *gtk_id = 0U;
    while (offset + 2U <= length) {
        uint8_t id = data[offset], ie_length = data[offset + 1U];
        size_t total = (size_t)ie_length + 2U;
        if (total > length - offset) return 0;
        if (id == 221U && ie_length >= 6U &&
            data[offset + 2U] == 0x00U && data[offset + 3U] == 0x0fU &&
            data[offset + 4U] == 0xacU && data[offset + 5U] == 1U) {
            if (ie_length != 22U && ie_length != 38U) return 0;
            *gtk_id = (uint8_t)(data[offset + 6U] & 0x03U);
            *gtk_length = (size_t)ie_length - 6U;
            for (size_t i = 0U; i < *gtk_length; ++i)
                gtk[i] = data[offset + 8U + i];
            return 1;
        }
        /* WPA key-data padding uses a single 0xdd byte at the end. */
        if (id == 0xddU && ie_length == 0U && offset + 2U == length) break;
        offset += total;
    }
    return 0;
}

int wifi_wpa2_receive_eapol(wifi_wpa2_supplicant_t *s,
                            const uint8_t *source_mac,
                            const uint8_t *payload, size_t length)
{
    uint8_t gtk[WIFI_WPA2_GTK_MAX], decrypted[WIFI_AES128_WRAP_MAX_CIPHER];
    size_t key_data_length, decrypted_length = 0U, gtk_length = 0U;
    uint8_t gtk_id = 0U;
    uint16_t info;
    int result = 0;

    if (!s || !source_mac || !payload || s->state == WIFI_WPA2_IDLE ||
        s->state == WIFI_WPA2_FAILED || length < EAPOL_HEADER_SIZE ||
        length > EAPOL_MESSAGE_MAX || !mac_equal(source_mac, s->bssid))
        return 0;
    if (payload[1] != EAPOL_TYPE_KEY || get_be16(payload + 2U) != length - 4U)
        return 0;
    if (length < EAPOL_KEY_FIXED_SIZE || payload[4] != EAPOL_KEY_DESCRIPTOR_RSN) {
        fail_session(s);
        return 0;
    }
    info = get_be16(payload + 5U);
    if ((info & KEY_INFO_VERSION_MASK) != 2U ||
        (info & (uint16_t)~KEY_INFO_ALLOWED) != 0U ||
        (info & KEY_INFO_PAIRWISE) == 0U) {
        fail_session(s);
        return 0;
    }
    key_data_length = get_be16(payload + EAPOL_KEY_DATA_LENGTH_OFFSET);
    if (key_data_length != length - EAPOL_KEY_DATA_OFFSET) {
        fail_session(s);
        return 0;
    }

    if (s->state == WIFI_WPA2_WAIT_M1) {
        if ((info & (KEY_INFO_ACK | KEY_INFO_MIC | KEY_INFO_INSTALL |
                     KEY_INFO_SECURE | KEY_INFO_ENCRYPTED_DATA)) != KEY_INFO_ACK ||
            key_data_length != 0U) {
            fail_session(s);
            return 0;
        }
        for (size_t i = 0U; i < 8U; ++i) s->replay_counter[i] = payload[9U + i];
        for (size_t i = 0U; i < 32U; ++i) s->anonce[i] = payload[17U + i];
        if (!s->ops.random_bytes(s->snonce, sizeof(s->snonce), s->ops.context)) {
            fail_session(s);
            return 0;
        }
        if (!wifi_wpa2_prf512(s->pmk, s->bssid, s->local_mac, s->anonce,
                              s->snonce, s->ptk)) {
            fail_session(s);
            return 0;
        }
        s->have_ptk = 1U;
        if (!send_key_message(s, 0x010aU, s->snonce, s->rsn_ie,
                              s->rsn_ie_length)) {
            fail_session(s);
            return 0;
        }
        s->state = WIFI_WPA2_WAIT_M3;
        return 1;
    }

    if (s->state != WIFI_WPA2_WAIT_M3 ||
        (info & (KEY_INFO_ACK | KEY_INFO_MIC | KEY_INFO_INSTALL |
                 KEY_INFO_SECURE | KEY_INFO_ENCRYPTED_DATA)) !=
        (KEY_INFO_ACK | KEY_INFO_MIC | KEY_INFO_INSTALL | KEY_INFO_SECURE |
         KEY_INFO_ENCRYPTED_DATA) ||
        !replay_greater(payload + 9U, s->replay_counter) ||
        !mac_equal(payload + 17U, s->anonce) ||
        !verify_mic(s->ptk, payload, length) || key_data_length < 24U ||
        key_data_length > sizeof(decrypted) || (key_data_length & 7U) != 0U) {
        fail_session(s);
        return 0;
    }

    if (!wifi_aes128_key_unwrap(s->ptk + 16U, payload + EAPOL_KEY_DATA_OFFSET,
                                key_data_length, decrypted, sizeof(decrypted),
                                &decrypted_length) ||
        !parse_gtk_kde(decrypted, decrypted_length, gtk, &gtk_length, &gtk_id) ||
        (gtk_length != 16U && gtk_length != 32U)) {
        fail_session(s);
        secure_zero(decrypted, sizeof(decrypted));
        secure_zero(gtk, sizeof(gtk));
        return 0;
    }

    /* The callback must implement an atomic key-table update. Only CCMP-128
       is currently supported, so reject a 256-bit GTK rather than truncate. */
    if (gtk_length != 16U ||
        !s->ops.install_ccmp_keys(s->bssid, s->local_mac, s->ptk + 32U,
                                  gtk, gtk_length, gtk_id, s->ops.context)) {
        fail_session(s);
        secure_zero(decrypted, sizeof(decrypted));
        secure_zero(gtk, sizeof(gtk));
        return 0;
    }
    for (size_t i = 0U; i < 8U; ++i) s->replay_counter[i] = payload[9U + i];
    if (!send_key_message(s, 0x030aU, 0, 0, 0U)) {
        fail_session(s);
        secure_zero(decrypted, sizeof(decrypted));
        secure_zero(gtk, sizeof(gtk));
        return 0;
    }
    s->state = WIFI_WPA2_AUTHENTICATED;
    secure_zero(s->pmk, sizeof(s->pmk));
    secure_zero(s->snonce, sizeof(s->snonce));
    secure_zero(decrypted, sizeof(decrypted));
    secure_zero(gtk, sizeof(gtk));
    result = 1;
    return result;
}

void wifi_wpa2_reset(wifi_wpa2_supplicant_t *s)
{
    if (!s) return;
    secure_zero(s, sizeof(*s));
    s->state = WIFI_WPA2_IDLE;
}

wifi_wpa2_state_t wifi_wpa2_get_state(const wifi_wpa2_supplicant_t *s)
{
    return s ? s->state : WIFI_WPA2_FAILED;
}
