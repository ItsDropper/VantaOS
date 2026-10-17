#include "net_wifi.h"

#include <stdio.h>
#include <string.h>

static net_wifi_state_t driver_state = NET_WIFI_DOWN;
static unsigned connect_calls;
static unsigned scan_calls;
static unsigned disconnect_calls;

static int driver_start(void)
{
    driver_state = NET_WIFI_READY;
    return 1;
}
static int driver_scan(void)
{
    ++scan_calls;
    driver_state = NET_WIFI_SCANNING;
    return 1;
}
static int driver_connect(const uint8_t *ssid, size_t ssid_length,
                          const uint8_t *passphrase, size_t passphrase_length,
                          net_wifi_security_t security)
{
    static const uint8_t expected_ssid[] = {'t','e','s','t'};
    static const uint8_t expected_password[] = {'p','a','s','s','w','o','r','d'};
    if (!ssid || ssid_length != sizeof(expected_ssid) ||
        memcmp(ssid, expected_ssid, sizeof(expected_ssid)) != 0 ||
        !passphrase || passphrase_length != sizeof(expected_password) ||
        memcmp(passphrase, expected_password, sizeof(expected_password)) != 0 ||
        (security != NET_WIFI_SECURITY_WPA2_PSK &&
         security != NET_WIFI_SECURITY_WPA3_SAE))
        return 0;
    ++connect_calls;
    driver_state = NET_WIFI_CONNECTING;
    return 1;
}
static int driver_disconnect(void)
{
    ++disconnect_calls;
    driver_state = NET_WIFI_DISCONNECTING;
    return 1;
}
static int driver_poll(void)
{
    if (driver_state == NET_WIFI_CONNECTING)
        driver_state = NET_WIFI_CONNECTED;
    else if (driver_state == NET_WIFI_DISCONNECTING)
        driver_state = NET_WIFI_READY;
    else
        return 0;
    return 1;
}
static net_wifi_state_t driver_get_state(void) { return driver_state; }
static size_t driver_scan_results(net_wifi_scan_result_t *results, size_t capacity)
{
    if (!results || !capacity)
        return 0;
    memset(&results[0], 0, sizeof(results[0]));
    results[0].ssid[0] = 't';
    results[0].ssid_length = 1;
    results[0].security = NET_WIFI_SECURITY_WPA2_PSK;
    return 1;
}

int main(void)
{
    static const net_wifi_driver_ops_t ops = {
        driver_start, driver_scan, driver_connect, driver_disconnect,
        driver_poll, driver_get_state, driver_scan_results
    };
    static const uint8_t ssid[] = {'t','e','s','t'};
    static const uint8_t password[] = {'p','a','s','s','w','o','r','d'};
    static const uint8_t short_password[] = {'s','h','o','r','t'};
    net_wifi_scan_result_t result;

    if (net_wifi_get_state() != NET_WIFI_DOWN ||
        net_wifi_start() != 0 ||
        net_wifi_register_driver(&ops) != 1 ||
        net_wifi_register_driver(NULL) != 0 ||
        net_wifi_start() != 1 ||
        net_wifi_get_state() != NET_WIFI_READY)
        return 1;

    if (net_wifi_connect(ssid, sizeof(ssid), password, sizeof(password),
                         NET_WIFI_SECURITY_OPEN) != 0 ||
        net_wifi_connect(ssid, sizeof(ssid), short_password, sizeof(short_password),
                         NET_WIFI_SECURITY_WPA2_PSK) != 0 ||
        net_wifi_connect(ssid, 0, password, sizeof(password),
                         NET_WIFI_SECURITY_WPA2_PSK) != 0 ||
        net_wifi_connect(ssid, sizeof(ssid), password, sizeof(password),
                         NET_WIFI_SECURITY_WPA2_PSK) != 1 ||
        connect_calls != 1 || net_wifi_get_state() != NET_WIFI_CONNECTING)
        return 2;

    if (net_wifi_poll(1) != 1 ||
        net_wifi_get_state() != NET_WIFI_CONNECTED ||
        net_wifi_disconnect() != 1 ||
        disconnect_calls != 1 ||
        net_wifi_poll(1) != 1 ||
        net_wifi_get_state() != NET_WIFI_READY)
        return 3;

    if (net_wifi_scan() != 1 || scan_calls != 1 ||
        net_wifi_get_scan_results(&result, 1) != 1 ||
        result.ssid_length != 1 || result.security != NET_WIFI_SECURITY_WPA2_PSK ||
        net_wifi_get_scan_results(&result, NET_WIFI_MAX_SCAN_RESULTS + 1U) != 0)
        return 4;

    puts("Wi-Fi control API validation tests passed.");
    return 0;
}
