/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef USB_BLUETOOTH_H
#define USB_BLUETOOTH_H

#include <stdint.h>

/*
 * A descriptor-level match only. It does not mean that the controller has
 * been initialized or that Bluetooth services are available.
 */
typedef struct {
    uint8_t interface_number;
    uint8_t event_endpoint;
    uint8_t acl_in_endpoint;
    uint8_t acl_out_endpoint;
    uint8_t event_interval;
    uint16_t event_max_packet;
    uint16_t acl_in_max_packet;
    uint16_t acl_out_max_packet;
} usb_bluetooth_hci_candidate_t;

/*
 * Scan a complete configuration descriptor. Returns 1 only when one
 * Bluetooth HCI interface (class E0/subclass 01/protocol 01) has an
 * interrupt-IN event endpoint and both bulk-IN and bulk-OUT ACL endpoints.
 * Malformed descriptor streams and incomplete interfaces return 0.
 */
int usb_bluetooth_find_hci_interface(const uint8_t *configuration,
                                     uint16_t length,
                                     usb_bluetooth_hci_candidate_t *candidate);

/* Strict packet-framing checks for HCI event and ACL-IN transfers. */
int usb_bluetooth_hci_event_valid(const uint8_t *packet, uint32_t length);
int usb_bluetooth_hci_reset_complete(const uint8_t *packet, uint32_t length);
int usb_bluetooth_acl_packet_valid(const uint8_t *packet, uint32_t length);

#endif
