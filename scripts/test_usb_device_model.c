/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "usb_device.h"

#include <assert.h>

int main(void)
{
    device_t controller = {
        .name = "pci0000:00:14.0",
        .bus = DEVICE_BUS_PCI
    };
    const usb_device_identity_t identity = {
        .vendor_id = 0x046DU,
        .product_id = 0xC31CU,
        .class_code = 0U,
        .subclass = 0U,
        .protocol = 0U,
        .revision = 0x12U
    };

    assert(device_register(&controller));
    assert(!usb_device_register(0U, 1U, &controller, &identity));
    assert(!usb_device_register(1U, 0U, &controller, &identity));
    assert(!usb_device_register(1U, 1U, NULL, &identity));
    assert(usb_device_count() == 0U);

    assert(usb_device_register(1U, 3U, &controller, &identity));
    assert(usb_device_count() == 1U);
    assert(usb_device_port(1U) == 3U);
    device_t* node = usb_device_node(1U);
    assert(node != NULL);
    assert(node->state == DEVICE_STATE_REGISTERED);
    assert(node->bus == DEVICE_BUS_USB);
    assert(node->parent == &controller);
    assert(node->vendor_id == identity.vendor_id);
    assert(node->device_id == identity.product_id);
    assert(node->class_code == identity.class_code);
    assert(node->subclass == identity.subclass);
    assert(node->prog_if == identity.protocol);
    assert(node->revision == identity.revision);
    assert(node->owner != NULL);
    assert(device_find(DEVICE_BUS_USB, "usb-slot1") == node);
    assert(!usb_device_register(1U, 3U, &controller, &identity));
    assert(!device_unregister(node));
    assert(!device_unregister_owned(node, &controller));
    assert(!device_unregister(&controller));

    assert(usb_device_unregister(1U));
    assert(usb_device_count() == 0U);
    assert(usb_device_port(1U) == 0U);
    assert(usb_device_node(1U) == NULL);
    assert(!usb_device_unregister(1U));
    assert(device_count() == 1U);
    assert(device_unregister(&controller));
    assert(device_count() == 0U);
    return 0;
}
