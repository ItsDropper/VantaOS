#ifndef VANTAOS_NET_WIFI_H
#define VANTAOS_NET_WIFI_H

#include <stddef.h>
#include <stdint.h>

/*
 * Chipset-independent Wi-Fi control plane.
 *
 * A registered chipset driver owns firmware, radio/DMA/interrupt handling,
 * 802.11 management/data frames, and WPA2/WPA3 cryptography. This API never
 * treats a PCI match or a successful scan as an authenticated connection.
 * Driver operations must be bounded, nonblocking, and callable from poll
 * context unless explicitly documented otherwise.
 */
#define NET_WIFI_SSID_MAX 32U
#define NET_WIFI_BSSID_SIZE 6U
#define NET_WIFI_MAX_SCAN_RESULTS 32U
#define NET_WIFI_PASSPHRASE_MIN 8U
#define NET_WIFI_PASSPHRASE_MAX 63U

typedef enum {
    NET_WIFI_DOWN = 0,
    NET_WIFI_READY,
    NET_WIFI_SCANNING,
    NET_WIFI_CONNECTING,
    NET_WIFI_CONNECTED,
    NET_WIFI_DISCONNECTING,
    NET_WIFI_ERROR
} net_wifi_state_t;

typedef enum {
    NET_WIFI_SECURITY_UNKNOWN = 0,
    NET_WIFI_SECURITY_OPEN,
    NET_WIFI_SECURITY_WPA2_PSK,
    NET_WIFI_SECURITY_WPA3_SAE
} net_wifi_security_t;

typedef struct {
    uint8_t ssid[NET_WIFI_SSID_MAX];
    uint8_t ssid_length;
    uint8_t bssid[NET_WIFI_BSSID_SIZE];
    int8_t rssi_dbm;
    uint8_t channel;
    net_wifi_security_t security;
} net_wifi_scan_result_t;

typedef struct {
    int (*start)(void);
    int (*scan)(void);
    int (*connect)(const uint8_t *ssid, size_t ssid_length,
                   const uint8_t *passphrase, size_t passphrase_length,
                   net_wifi_security_t security);
    int (*disconnect)(void);
    /* Poll must make bounded progress and must not sleep or wait for I/O. */
    int (*poll)(void);
    net_wifi_state_t (*get_state)(void);
    size_t (*get_scan_results)(net_wifi_scan_result_t *results,
                               size_t capacity);
} net_wifi_driver_ops_t;

/* One radio is supported until multi-radio ownership is implemented. */
int net_wifi_register_driver(const net_wifi_driver_ops_t *ops);
int net_wifi_start(void);
int net_wifi_scan(void);
/* Open networks are intentionally rejected; WPA2-PSK/WPA3-SAE only. */
int net_wifi_connect(const uint8_t *ssid, size_t ssid_length,
                     const uint8_t *passphrase, size_t passphrase_length,
                     net_wifi_security_t security);
int net_wifi_disconnect(void);
size_t net_wifi_poll(size_t budget);
net_wifi_state_t net_wifi_get_state(void);
size_t net_wifi_get_scan_results(net_wifi_scan_result_t *results,
                                 size_t capacity);

#endif
