/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef USB_BLUETOOTH_H
#define USB_BLUETOOTH_H

#include <stdint.h>

#define USB_BLUETOOTH_ACL_MAX_PACKET 4096U
#define USB_BLUETOOTH_ACL_QUEUE_DEPTH 2U

typedef struct {
    uint32_t head;
    uint32_t count;
    uint32_t lengths[USB_BLUETOOTH_ACL_QUEUE_DEPTH];
    uint8_t packets[USB_BLUETOOTH_ACL_QUEUE_DEPTH][USB_BLUETOOTH_ACL_MAX_PACKET];
} usb_bluetooth_acl_queue_t;

/*
 * A descriptor-level match only. It does not mean that the controller has
 * been initialized or that Bluetooth services are available.
 */
typedef struct {
    uint16_t handle;
    uint8_t packet_boundary;
    uint8_t broadcast;
    const uint8_t *payload;
    uint32_t payload_length;
} usb_bluetooth_acl_view_t;

typedef struct {
    uint16_t channel_id;
    const uint8_t *payload;
    uint16_t payload_length;
} usb_bluetooth_l2cap_view_t;

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

/* Parse one complete HCI ACL packet without copying its payload. */
int usb_bluetooth_acl_parse(const uint8_t *packet, uint32_t length,
                            usb_bluetooth_acl_view_t *view);
/* Parse one complete, unfragmented L2CAP PDU carried in an ACL packet. */
int usb_bluetooth_l2cap_parse(const uint8_t *packet, uint32_t length,
                              usb_bluetooth_l2cap_view_t *view);
/* Validate a complete L2CAP signaling command list on CID 0x0001. */
int usb_bluetooth_l2cap_signaling_valid(const uint8_t *payload,
                                        uint32_t length);
/* Strict packet-framing checks for HCI event and ACL-IN transfers. */
int usb_bluetooth_hci_event_valid(const uint8_t *packet, uint32_t length);
int usb_bluetooth_hci_reset_status(const uint8_t *packet, uint32_t length);
int usb_bluetooth_hci_reset_complete(const uint8_t *packet, uint32_t length);
int usb_bluetooth_acl_packet_valid(const uint8_t *packet, uint32_t length);
/* True only after a successful HCI Reset and before any reset failure. */
int usb_bluetooth_transport_ready(int reset_complete, int reset_failed);
/* Apply reset completion; returns 1 new success, -1 new failure, 0 otherwise. */
int usb_bluetooth_update_reset_state(const uint8_t *packet, uint32_t length,
                                     uint8_t *reset_complete,
                                     uint8_t *reset_failed);
void usb_bluetooth_acl_queue_reset(usb_bluetooth_acl_queue_t *queue);
int usb_bluetooth_acl_queue_push(usb_bluetooth_acl_queue_t *queue,
                                 const uint8_t *packet, uint32_t length);
int usb_bluetooth_acl_queue_pop(usb_bluetooth_acl_queue_t *queue,
                                uint8_t *packet, uint32_t capacity);

/* Send one complete framed ACL packet through a bound device port.
 * Returns 1 only if xHCI reports a full transfer; caller-context limits are
 * documented in docs/USB_BLUETOOTH_BRINGUP.md. */
int usb_bluetooth_acl_send(uint32_t port, const uint8_t *packet,
                           uint32_t length);
/* Receive one queued raw ACL packet; -11 empty, -90 too small (not consumed),
 * -9 when no bound device exists. Owner-CPU context only. */
int usb_bluetooth_acl_receive(uint32_t port, uint8_t *packet,
                              uint32_t capacity);

#endif
