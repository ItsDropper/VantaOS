/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "usb_device.h"

#include <stddef.h>

typedef struct
{
    device_t core;
    uint32_t port;
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

int usb_device_unregister(uint32_t slot)
{
    if (slot == 0U || slot > USB_DEVICE_MAX_SLOTS ||
        !entries[slot].active)
        return 0;

    usb_device_entry_t* entry = &entries[slot];
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

uint32_t usb_device_count(void)
{
    return registered_count;
}
