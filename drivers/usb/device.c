/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "usb_device.h"

#include <stddef.h>

#define USB_DEVICE_INTERFACE_MAX 16U

typedef struct
{
    device_t core;
    uint8_t interface_number;
    uint8_t active;
} usb_interface_entry_t;

typedef struct
{
    device_t core;
    usb_interface_entry_t interfaces[USB_DEVICE_INTERFACE_MAX];
    uint32_t port;
    uint32_t interface_count;
    uint8_t active;
} usb_device_entry_t;

static usb_device_entry_t entries[USB_DEVICE_MAX_SLOTS + 1U];
static uint32_t registered_count;

static int make_name(char* name, uint32_t slot)
{
    static const char prefix[] = "usb-slot";
    uint32_t position = 0U;
    for (uint32_t i = 0U; i < sizeof(prefix) - 1U; ++i)
        name[position++] = prefix[i];

    if (slot >= 10U)
        name[position++] = (char)('0' + (slot / 10U) % 10U);
    name[position++] = (char)('0' + slot % 10U);
    name[position] = '\0';
    return position < DEVICE_NAME_MAX;
}

int usb_device_register(uint32_t slot, uint32_t port, device_t* parent,
                        const usb_device_identity_t* identity)
{
    if (slot == 0U || slot > USB_DEVICE_MAX_SLOTS || port == 0U ||
        !parent || !identity || entries[slot].active ||
        entries[slot].core.state != DEVICE_STATE_UNREGISTERED)
        return 0;

    usb_device_entry_t* entry = &entries[slot];
    entry->core = (device_t){0};
    if (!make_name(entry->core.name, slot))
        return 0;
    entry->core.bus = DEVICE_BUS_USB;
    entry->core.parent = parent;
    entry->core.vendor_id = identity->vendor_id;
    entry->core.device_id = identity->product_id;
    entry->core.class_code = identity->class_code;
    entry->core.subclass = identity->subclass;
    entry->core.prog_if = identity->protocol;
    entry->core.revision = identity->revision;
    entry->core.bus_data = entry;
    entry->core.owner = entry;

    if (!device_register(&entry->core))
        return 0;

    entry->port = port;
    entry->active = 1U;
    ++registered_count;
    return 1;
}

static int make_interface_name(char* name, uint32_t slot,
                               uint8_t interface_number)
{
    if (!make_name(name, slot))
        return 0;
    uint32_t position = 0U;
    while (name[position])
        ++position;
    static const char suffix[] = "-if";
    for (uint32_t i = 0U; i < sizeof(suffix) - 1U; ++i)
        name[position++] = suffix[i];
    if (interface_number >= 100U)
        name[position++] = (char)('0' + interface_number / 100U);
    if (interface_number >= 10U)
        name[position++] = (char)('0' + (interface_number / 10U) % 10U);
    name[position++] = (char)('0' + interface_number % 10U);
    name[position] = '\0';
    return position < DEVICE_NAME_MAX;
}

int usb_device_register_interfaces(uint32_t slot,
                                   const uint8_t* configuration,
                                   uint16_t length)
{
    if (slot == 0U || slot > USB_DEVICE_MAX_SLOTS ||
        !entries[slot].active || !configuration || length < 9U ||
        configuration[0] < 9U || configuration[1] != 2U ||
        ((uint16_t)configuration[2] |
         ((uint16_t)configuration[3] << 8)) != length)
        return 0;

    usb_device_entry_t* parent = &entries[slot];
    if (parent->interface_count != 0U)
        return 0;

    uint8_t seen[32] = {0};
    uint32_t count = 0U;
    uint16_t offset = 0U;
    while (offset < length)
    {
        uint8_t descriptor_length = configuration[offset];
        if (descriptor_length < 2U ||
            descriptor_length > (uint16_t)(length - offset))
            goto rollback;

        if (configuration[offset + 1U] == 4U)
        {
            if (descriptor_length < 9U)
                goto rollback;
            uint8_t number = configuration[offset + 2U];
            uint8_t alternate = configuration[offset + 3U];
            if (alternate == 0U)
            {
                uint32_t byte = number / 8U;
                uint8_t mask = (uint8_t)(1U << (number % 8U));
                if (seen[byte] & mask ||
                    count >= USB_DEVICE_INTERFACE_MAX)
                    goto rollback;
                seen[byte] |= mask;

                usb_interface_entry_t* interface =
                    &parent->interfaces[count];
                interface->core = (device_t){0};
                if (!make_interface_name(interface->core.name, slot, number))
                    goto rollback;
                interface->core.bus = DEVICE_BUS_USB;
                interface->core.parent = &parent->core;
                interface->core.vendor_id = parent->core.vendor_id;
                interface->core.device_id = parent->core.device_id;
                interface->core.class_code = configuration[offset + 5U];
                interface->core.subclass = configuration[offset + 6U];
                interface->core.prog_if = configuration[offset + 7U];
                interface->core.revision = parent->core.revision;
                interface->core.bus_data = interface;
                interface->core.owner = interface;
                interface->interface_number = number;

                if (!device_register(&interface->core))
                    goto rollback;
                interface->active = 1U;
                ++count;
                parent->interface_count = count;
            }
        }
        offset = (uint16_t)(offset + descriptor_length);
    }

    if (count != configuration[4])
        goto rollback;
    return 1;

rollback:
    while (parent->interface_count != 0U)
    {
        usb_interface_entry_t* interface =
            &parent->interfaces[parent->interface_count - 1U];
        if (!device_unregister_owned(&interface->core, interface))
            break;
        interface->active = 0U;
        --parent->interface_count;
    }
    return 0;
}

int usb_device_unregister(uint32_t slot)
{
    if (slot == 0U || slot > USB_DEVICE_MAX_SLOTS ||
        !entries[slot].active)
        return 0;

    usb_device_entry_t* entry = &entries[slot];
    while (entry->interface_count != 0U)
    {
        usb_interface_entry_t* interface =
            &entry->interfaces[entry->interface_count - 1U];
        if (!device_unregister_owned(&interface->core, interface))
            return 0;
        interface->active = 0U;
        --entry->interface_count;
    }
    if (!device_unregister_owned(&entry->core, entry))
        return 0;

    entry->active = 0U;
    entry->port = 0U;
    if (registered_count != 0U)
        --registered_count;
    return 1;
}

uint32_t usb_device_port(uint32_t slot)
{
    return slot != 0U && slot <= USB_DEVICE_MAX_SLOTS &&
           entries[slot].active ? entries[slot].port : 0U;
}

device_t* usb_device_node(uint32_t slot)
{
    return slot != 0U && slot <= USB_DEVICE_MAX_SLOTS &&
           entries[slot].active ? &entries[slot].core : NULL;
}

uint32_t usb_device_interface_count(uint32_t slot)
{
    return slot != 0U && slot <= USB_DEVICE_MAX_SLOTS &&
           entries[slot].active ? entries[slot].interface_count : 0U;
}

device_t* usb_device_interface_node(uint32_t slot, uint8_t interface_number)
{
    if (slot == 0U || slot > USB_DEVICE_MAX_SLOTS ||
        !entries[slot].active)
        return NULL;
    usb_device_entry_t* entry = &entries[slot];
    for (uint32_t i = 0U; i < entry->interface_count; ++i)
        if (entry->interfaces[i].active &&
            entry->interfaces[i].interface_number == interface_number)
            return &entry->interfaces[i].core;
    return NULL;
}

uint32_t usb_device_count(void)
{
    return registered_count;
}
