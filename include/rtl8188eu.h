#ifndef VANTAOS_RTL8188EU_H
#define VANTAOS_RTL8188EU_H

#include <stddef.h>
#include <stdint.h>
#include "net_wifi.h"

typedef enum {
    RTL8188EU_STATUS_UNBOUND = 0,
    RTL8188EU_STATUS_USB_BOUND,
    RTL8188EU_STATUS_FIRMWARE_PENDING,
    RTL8188EU_STATUS_FIRMWARE_READY,
    RTL8188EU_STATUS_FIRMWARE_FAILED
} rtl8188eu_status_t;

/*
 * Probe a USB device descriptor/configuration for the supported RTL8188EU
 * USB IDs and configure its bulk endpoints. This is transport binding only:
 * it does not claim that firmware, RF calibration, association, or WPA2 are
 * initialized.
 */
int rtl8188eu_probe(uint32_t slot_id, uint32_t port, uint16_t vendor_id, uint16_t device_id,
                    const uint8_t *configuration, size_t length);
void rtl8188eu_poll(void);
/* Firmware readiness is not equivalent to association or IP connectivity. */
rtl8188eu_status_t rtl8188eu_get_status(void);
/* Active scan is currently fail-closed: returns 0 until verified radio
 * initialization/calibration and channel programming are implemented. */
int rtl8188eu_start_active_scan(void);
/* Begin a WPA2-PSK connection to a matching scanned SSID. This returns only
 * when the first authentication frame was submitted; use get_connection_state
 * to observe the actual authentication/association/handshake result. */
int rtl8188eu_connect(const uint8_t *ssid, size_t ssid_length,
                      const uint8_t *passphrase, size_t passphrase_length);
typedef enum {
    RTL8188EU_CONNECTION_IDLE = 0,
    RTL8188EU_CONNECTION_AUTHENTICATING,
    RTL8188EU_CONNECTION_ASSOCIATING,
    RTL8188EU_CONNECTION_WPA2_HANDSHAKE,
    RTL8188EU_CONNECTION_AUTHENTICATED,
    RTL8188EU_CONNECTION_FAILED
} rtl8188eu_connection_state_t;
rtl8188eu_connection_state_t rtl8188eu_get_connection_state(void);
/* Read the station address programmed into the adapter's MACID registers. */
int rtl8188eu_get_mac_address(uint8_t mac[6]);
/* Send one complete 802.11 MPDU (without FCS). Currently returns 0 until
 * verified MAC/PHY/RF initialization and calibration enable radio TX. */
int rtl8188eu_transmit_frame(const uint8_t *frame, size_t length);
int rtl8188eu_port_disconnected(uint32_t port);
int rtl8188eu_port_is_bound(uint32_t port);
size_t rtl8188eu_get_passive_results(net_wifi_scan_result_t *results,
                                     size_t capacity);

/* Decode a captured RTL8188E physical eFuse map into its logical map. */
int rtl8188eu_decode_efuse_map(const uint8_t *physical, size_t physical_length,
                               uint8_t *logical, size_t logical_length);

typedef struct {
    uint8_t cck_base[3];
    uint8_t ht40_base[3];
    int8_t ofdm_diff;
    int8_t ht20_diff;
} rtl8188eu_tx_power_calibration_t;

/* Decode the factory 2.4-GHz, 1T eFuse power bases and signed deltas. */
int rtl8188eu_decode_tx_power_calibration(
    const uint8_t *logical, size_t logical_length,
    rtl8188eu_tx_power_calibration_t *calibration);

/* Validate a board-specific RTL8188EU PHY profile without touching hardware. */
int rtl8188eu_validate_phy_profile(const uint8_t *profile, size_t length,
                                   uint8_t chip_cut);

#endif
