/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "usb_bluetooth.h"

#include <stddef.h>
#include <stdint.h>

#define USB_DESC_INTERFACE 4U
#define USB_DESC_ENDPOINT 5U
#define USB_CLASS_WIRELESS_CONTROLLER 0xE0U
#define USB_SUBCLASS_RF_CONTROLLER 0x01U
#define USB_PROTOCOL_BLUETOOTH 0x01U
#define USB_TRANSFER_BULK 2U
#define USB_TRANSFER_INTERRUPT 3U

typedef struct {
    uint8_t active;
    uint8_t interface_number;
    uint8_t event_endpoint;
    uint8_t acl_in_endpoint;
    uint8_t acl_out_endpoint;
    uint8_t event_interval;
    uint16_t event_max_packet;
    uint16_t acl_in_max_packet;
    uint16_t acl_out_max_packet;
} hci_interface_t;

static uint16_t endpoint_max_packet(const uint8_t *descriptor)
{
    return (uint16_t)descriptor[4] |
           ((uint16_t)(descriptor[5] & 0x07U) << 8);
}

static int endpoint_address_valid(uint8_t address)
{
    /* Bits 4..6 are reserved; endpoint zero is the control endpoint. */
    return (address & 0x70U) == 0U && (address & 0x0FU) != 0U;
}

int usb_bluetooth_find_hci_interface(const uint8_t *configuration,
                                     uint16_t length,
                                     usb_bluetooth_hci_candidate_t *candidate)
{
    uint32_t offset = 0;
    hci_interface_t current = {0};
    usb_bluetooth_hci_candidate_t found = {0};
    int have_candidate = 0;

    if (candidate == NULL)
        return 0;

    *candidate = (usb_bluetooth_hci_candidate_t){0};
    if (configuration == NULL || length < 9U)
        return 0;

    /*
     * The caller supplies the complete configuration descriptor returned by
     * enumeration, not an arbitrary slice. Validate its header and total
     * length before considering interface descriptors.
     */
    if (configuration[0] < 9U || configuration[1] != 2U)
        return 0;
    if (((uint16_t)configuration[2] |
         ((uint16_t)configuration[3] << 8)) != length)
        return 0;

    while (offset < length) {
        uint8_t descriptor_length;
        uint8_t descriptor_type;

        if ((uint32_t)length - offset < 2U)
            return 0;

        descriptor_length = configuration[offset];
        descriptor_type = configuration[offset + 1U];
        if (descriptor_length < 2U ||
            (uint32_t)descriptor_length > (uint32_t)length - offset)
            return 0;

        if (descriptor_type == USB_DESC_INTERFACE) {
            if (descriptor_length < 9U)
                return 0;

            if (!have_candidate && current.active &&
                current.event_endpoint != 0U &&
                current.acl_in_endpoint != 0U &&
                current.acl_out_endpoint != 0U) {
                found.interface_number = current.interface_number;
                found.event_endpoint = current.event_endpoint;
                found.acl_in_endpoint = current.acl_in_endpoint;
                found.acl_out_endpoint = current.acl_out_endpoint;
                found.event_interval = current.event_interval;
                found.event_max_packet = current.event_max_packet;
                found.acl_in_max_packet = current.acl_in_max_packet;
                found.acl_out_max_packet = current.acl_out_max_packet;
                have_candidate = 1;
            }

            current = (hci_interface_t){0};
            current.interface_number = configuration[offset + 2U];
            current.active =
                configuration[offset + 5U] == USB_CLASS_WIRELESS_CONTROLLER &&
                configuration[offset + 6U] == USB_SUBCLASS_RF_CONTROLLER &&
                configuration[offset + 7U] == USB_PROTOCOL_BLUETOOTH;
        } else if (descriptor_type == USB_DESC_ENDPOINT) {
            uint8_t address;
            uint8_t transfer_type;
            uint16_t max_packet;

            if (descriptor_length < 7U)
                return 0;
            if (!current.active)
                goto next_descriptor;

            address = configuration[offset + 2U];
            transfer_type = configuration[offset + 3U] & 0x03U;
            max_packet = endpoint_max_packet(configuration + offset);
            if (!endpoint_address_valid(address) || max_packet == 0U)
                goto next_descriptor;

            if (transfer_type == USB_TRANSFER_INTERRUPT &&
                (address & 0x80U) != 0U && current.event_endpoint == 0U) {
                current.event_endpoint = address;
                current.event_interval = configuration[offset + 6U];
                current.event_max_packet = max_packet;
            } else if (transfer_type == USB_TRANSFER_BULK &&
                       (address & 0x80U) != 0U &&
                       current.acl_in_endpoint == 0U) {
                current.acl_in_endpoint = address;
                current.acl_in_max_packet = max_packet;
            } else if (transfer_type == USB_TRANSFER_BULK &&
                       (address & 0x80U) == 0U &&
                       current.acl_out_endpoint == 0U) {
                current.acl_out_endpoint = address;
                current.acl_out_max_packet = max_packet;
            }
        }

next_descriptor:
        offset += descriptor_length;
    }

    if (!have_candidate && current.active &&
        current.event_endpoint != 0U &&
        current.acl_in_endpoint != 0U &&
        current.acl_out_endpoint != 0U) {
        found.interface_number = current.interface_number;
        found.event_endpoint = current.event_endpoint;
        found.acl_in_endpoint = current.acl_in_endpoint;
        found.acl_out_endpoint = current.acl_out_endpoint;
        found.event_interval = current.event_interval;
        found.event_max_packet = current.event_max_packet;
        found.acl_in_max_packet = current.acl_in_max_packet;
        found.acl_out_max_packet = current.acl_out_max_packet;
        have_candidate = 1;
    }

    /*
     * Validate the entire stream before publishing a match. Returning as soon
     * as a valid interface ended would accept a valid prefix followed by a
     * malformed descriptor tail.
     */
    if (!have_candidate)
        return 0;
    *candidate = found;
    return 1;
}

/* USB interrupt transfers carry one complete HCI event. */
int usb_bluetooth_hci_event_valid(const uint8_t *packet, uint32_t length)
{
    return packet != NULL && length >= 2U &&
           (uint32_t)packet[1] == length - 2U;
}

/* Return Reset status, or -1 if this is not its Command Complete event. */
int usb_bluetooth_hci_reset_status(const uint8_t *packet, uint32_t length)
{
    if (!usb_bluetooth_hci_event_valid(packet, length) ||
        packet[0] != 0x0EU || packet[1] < 4U ||
        packet[3] != 0x03U || packet[4] != 0x0CU)
        return -1;
    return (int)packet[5];
}

/* Command Complete event for HCI Reset (opcode 0x0C03, status zero). */
int usb_bluetooth_hci_reset_complete(const uint8_t *packet, uint32_t length)
{
    return usb_bluetooth_hci_reset_status(packet, length) == 0;
}

/* ACL is available to consumers only after a successful HCI Reset. */
int usb_bluetooth_transport_ready(int reset_complete, int reset_failed)
{
    return reset_complete != 0 && reset_failed == 0;
}

/* ACL packet header: LE16 handle/flags followed by an LE16 payload length. */
int usb_bluetooth_acl_packet_valid(const uint8_t *packet, uint32_t length)
{
    if (packet == NULL || length < 4U)
        return 0;
    uint32_t payload_length = (uint32_t)packet[2] |
                              ((uint32_t)packet[3] << 8);
    return payload_length == length - 4U;
}


void usb_bluetooth_acl_queue_reset(usb_bluetooth_acl_queue_t *queue)
{
    if (queue == NULL)
        return;
    queue->head = 0U;
    queue->count = 0U;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_ACL_QUEUE_DEPTH; ++i) {
        queue->lengths[i] = 0U;
        for (uint32_t j = 0U; j < USB_BLUETOOTH_ACL_MAX_PACKET; ++j)
            queue->packets[i][j] = 0U;
    }
}

int usb_bluetooth_acl_queue_push(usb_bluetooth_acl_queue_t *queue,
                                 const uint8_t *packet, uint32_t length)
{
    if (queue == NULL || packet == NULL ||
        length > USB_BLUETOOTH_ACL_MAX_PACKET ||
        !usb_bluetooth_acl_packet_valid(packet, length) ||
        queue->head >= USB_BLUETOOTH_ACL_QUEUE_DEPTH ||
        queue->count > USB_BLUETOOTH_ACL_QUEUE_DEPTH)
        return -22;
    if (queue->count == USB_BLUETOOTH_ACL_QUEUE_DEPTH)
        return -11;

    uint32_t tail = (queue->head + queue->count) %
                    USB_BLUETOOTH_ACL_QUEUE_DEPTH;
    for (uint32_t i = 0U; i < length; ++i)
        queue->packets[tail][i] = packet[i];
    queue->lengths[tail] = length;
    ++queue->count;
    return 0;
}

int usb_bluetooth_acl_queue_pop(usb_bluetooth_acl_queue_t *queue,
                                uint8_t *packet, uint32_t capacity)
{
    if (queue == NULL || packet == NULL || capacity == 0U ||
        queue->head >= USB_BLUETOOTH_ACL_QUEUE_DEPTH ||
        queue->count > USB_BLUETOOTH_ACL_QUEUE_DEPTH)
        return -22;
    if (queue->count == 0U)
        return -11;

    uint32_t length = queue->lengths[queue->head];
    if (length < 4U || length > USB_BLUETOOTH_ACL_MAX_PACKET ||
        !usb_bluetooth_acl_packet_valid(queue->packets[queue->head], length))
        return -22;
    if (length > capacity)
        return -90;

    for (uint32_t i = 0U; i < length; ++i)
        packet[i] = queue->packets[queue->head][i];
    queue->lengths[queue->head] = 0U;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_ACL_MAX_PACKET; ++i)
        queue->packets[queue->head][i] = 0U;
    queue->head = (queue->head + 1U) % USB_BLUETOOTH_ACL_QUEUE_DEPTH;
    --queue->count;
    return (int)length;
}
