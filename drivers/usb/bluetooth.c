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
    if ((uint16_t)configuration[2] |
        ((uint16_t)configuration[3] << 8) != length)
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
