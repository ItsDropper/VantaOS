/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "usb_device.h"

#include <assert.h>
#include <stddef.h>

static uint32_t hid_probe_count;
static uint32_t hid_remove_count;

static device_probe_result_t hid_interface_probe(device_t* device)
{
    assert(device != NULL);
    assert(device->bus == DEVICE_BUS_USB);
    assert(device->class_code == 3U);
    ++hid_probe_count;
    return DEVICE_PROBE_BOUND;
}

static void hid_interface_remove(device_t* device)
{
    assert(device != NULL);
    ++hid_remove_count;
}

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

    const uint8_t malformed_configuration[11] = {
        9U, 2U, 11U, 0U, 1U, 1U, 0U, 0x80U, 50U,
        1U, 4U
    };
    assert(!usb_device_register_interfaces(
        1U, malformed_configuration, sizeof(malformed_configuration)));
    assert(usb_device_interface_count(1U) == 0U);
    assert(device_count() == 2U);

    const uint8_t configuration[36] = {
        9U, 2U, 36U, 0U, 2U, 1U, 0U, 0x80U, 50U,
        9U, 4U, 0U, 0U, 0U, 3U, 1U, 1U, 0U,
        9U, 4U, 0U, 1U, 1U, 3U, 1U, 0U, 0U,
        9U, 4U, 1U, 0U, 0U, 8U, 6U, 0x50U, 0U
    };
    assert(usb_device_register_interfaces(1U, configuration,
                                          sizeof(configuration)));
    assert(usb_device_interface_count(1U) == 2U);
    device_t* hid_interface = usb_device_interface_node(1U, 0U);
    device_t* storage_interface = usb_device_interface_node(1U, 1U);
    assert(hid_interface != NULL);
    assert(storage_interface != NULL);
    assert(hid_interface->parent == usb_device_node(1U));
    assert(hid_interface->class_code == 3U);
    assert(hid_interface->subclass == 1U);
    assert(hid_interface->prog_if == 1U);
    assert(storage_interface->class_code == 8U);
    assert(storage_interface->subclass == 6U);
    assert(storage_interface->prog_if == 0x50U);
    assert(device_find(DEVICE_BUS_USB, "usb-slot1-if0") == hid_interface);
    assert(device_find(DEVICE_BUS_USB, "usb-slot1-if1") == storage_interface);

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
    assert(!device_unregister(hid_interface));
    assert(!device_unregister(storage_interface));

    const device_id_t hid_ids[] = {{
        DEVICE_ID_ANY16, DEVICE_ID_ANY16, 3U, 1U, 1U
    }};
    const device_driver_t hid_driver = {
        .name = "usb-hid-interface-test",
        .bus = DEVICE_BUS_USB,
        .ids = hid_ids,
        .id_count = 1U,
        .probe = hid_interface_probe,
        .remove = hid_interface_remove
    };
    assert(device_driver_register(&hid_driver));
    assert(device_bind(hid_interface, &hid_driver) == DEVICE_BIND_BOUND);
    assert(hid_probe_count == 1U);
    /* A bound interface prevents parent teardown until its driver detaches. */
    assert(!usb_device_unregister(1U));
    assert(usb_device_interface_count(1U) == 1U);
    assert(usb_device_interface_node(1U, 0U) == hid_interface);
    assert(usb_device_interface_node(1U, 1U) == NULL);
    assert(device_unbind(hid_interface));
    assert(hid_remove_count == 1U);
    assert(device_driver_unregister(&hid_driver));

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
