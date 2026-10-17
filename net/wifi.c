#include "net_wifi.h"

#include <string.h>

static const net_wifi_driver_ops_t *wifi_ops;
static net_wifi_state_t wifi_state = NET_WIFI_DOWN;

static void wipe_secret(volatile uint8_t *secret, size_t length)
{
    while (length--)
        *secret++ = 0;
}

int net_wifi_register_driver(const net_wifi_driver_ops_t *ops)
{
    if (!ops || !ops->start || !ops->scan || !ops->connect ||
        !ops->disconnect || !ops->poll || !ops->get_state ||
        !ops->get_scan_results)
        return 0;
    if (wifi_ops && wifi_state != NET_WIFI_DOWN && wifi_state != NET_WIFI_ERROR)
        return 0;
    wifi_ops = ops;
    wifi_state = NET_WIFI_DOWN;
    return 1;
}

int net_wifi_start(void)
{
    if (!wifi_ops || (wifi_state != NET_WIFI_DOWN && wifi_state != NET_WIFI_ERROR))
        return 0;
    if (!wifi_ops->start()) {
        wifi_state = NET_WIFI_ERROR;
        return 0;
    }
    wifi_state = NET_WIFI_READY;
    return 1;
}

int net_wifi_scan(void)
{
    if (!wifi_ops || (wifi_state != NET_WIFI_READY && wifi_state != NET_WIFI_CONNECTED))
        return 0;
    if (!wifi_ops->scan())
        return 0;
    wifi_state = NET_WIFI_SCANNING;
    return 1;
}

int net_wifi_connect(const uint8_t *ssid, size_t ssid_length,
                     const uint8_t *passphrase, size_t passphrase_length,
                     net_wifi_security_t security)
{
    uint8_t secret[NET_WIFI_PASSPHRASE_MAX];
    int accepted;

    if (!wifi_ops || !ssid || ssid_length == 0U ||
        ssid_length > NET_WIFI_SSID_MAX || !passphrase ||
        passphrase_length < NET_WIFI_PASSPHRASE_MIN ||
        passphrase_length > NET_WIFI_PASSPHRASE_MAX ||
        (security != NET_WIFI_SECURITY_WPA2_PSK &&
         security != NET_WIFI_SECURITY_WPA3_SAE) ||
        (wifi_state != NET_WIFI_READY && wifi_state != NET_WIFI_CONNECTED))
        return 0;

    /* Reject embedded NULs/control characters: this API accepts passphrases,
       not raw 256-bit PSK hex or binary key material. */
    for (size_t i = 0; i < passphrase_length; ++i) {
        if (passphrase[i] < 0x20U || passphrase[i] > 0x7eU)
            return 0;
    }

    memcpy(secret, passphrase, passphrase_length);
    accepted = wifi_ops->connect(ssid, ssid_length, secret,
                                 passphrase_length, security);
    wipe_secret(secret, sizeof(secret));
    if (!accepted)
        return 0;

    wifi_state = NET_WIFI_CONNECTING;
    return 1;
}

int net_wifi_disconnect(void)
{
    if (!wifi_ops || wifi_state != NET_WIFI_CONNECTED)
        return 0;
    if (!wifi_ops->disconnect())
        return 0;
    wifi_state = NET_WIFI_DISCONNECTING;
    return 1;
}

size_t net_wifi_poll(size_t budget)
{
    size_t completed = 0;

    if (!wifi_ops || budget == 0U)
        return 0;
    while (completed < budget) {
        if (!wifi_ops->poll())
            break;
        ++completed;
    }

    /* The hardware driver is authoritative for association/authentication.
       Never report CONNECTED based on scan or command acceptance alone. */
    wifi_state = wifi_ops->get_state();
    if (wifi_state < NET_WIFI_DOWN || wifi_state > NET_WIFI_ERROR)
        wifi_state = NET_WIFI_ERROR;
    return completed;
}

net_wifi_state_t net_wifi_get_state(void)
{
    if (!wifi_ops)
        return NET_WIFI_DOWN;
    return wifi_state;
}

size_t net_wifi_get_scan_results(net_wifi_scan_result_t *results,
                                 size_t capacity)
{
    if (!wifi_ops || !results || capacity == 0U ||
        capacity > NET_WIFI_MAX_SCAN_RESULTS ||
        (wifi_state != NET_WIFI_SCANNING && wifi_state != NET_WIFI_READY &&
         wifi_state != NET_WIFI_CONNECTED))
        return 0;
    return wifi_ops->get_scan_results(results, capacity);
}
