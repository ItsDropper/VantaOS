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
/* Read the station address programmed into the adapter's MACID registers. */
int rtl8188eu_get_mac_address(uint8_t mac[6]);
/* Send one complete 802.11 MPDU (without FCS). Currently returns 0 until
 * verified MAC/PHY/RF initialization and calibration enable radio TX. */
int rtl8188eu_transmit_frame(const uint8_t *frame, size_t length);
int rtl8188eu_port_disconnected(uint32_t port);
int rtl8188eu_port_is_bound(uint32_t port);
size_t rtl8188eu_get_passive_results(net_wifi_scan_result_t *results,
                                     size_t capacity);

#endif
