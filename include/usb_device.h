/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef VANTA_USB_DEVICE_H
#define VANTA_USB_DEVICE_H

#include <stdint.h>
#include "device.h"

#define USB_DEVICE_MAX_SLOTS 8U

typedef struct
{
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t protocol;
    uint8_t revision;
} usb_device_identity_t;

/* Called only by the serialized USB owner/poll context. */
int usb_device_register(uint32_t slot, uint32_t port, device_t* parent,
                        const usb_device_identity_t* identity);
int usb_device_register_interfaces(uint32_t slot, const uint8_t* configuration,
                                   uint16_t length);
int usb_device_unregister(uint32_t slot);
uint32_t usb_device_port(uint32_t slot);
device_t* usb_device_node(uint32_t slot);
uint32_t usb_device_interface_count(uint32_t slot);
device_t* usb_device_interface_node(uint32_t slot, uint8_t interface_number);
uint32_t usb_device_count(void);

#endif
