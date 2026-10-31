/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "device.h"

#include <assert.h>
#include <string.h>

static uint32_t probe_count;
static uint32_t remove_count;
static uint32_t usb_probe_count;
static uint32_t usb_remove_count;

static device_probe_result_t display_probe(device_t* device)
{
    assert(device != NULL);
    ++probe_count;
    return DEVICE_PROBE_BOUND;
}

static void display_remove(device_t* device)
{
    assert(device != NULL);
    ++remove_count;
}

static device_probe_result_t failing_probe(device_t* device)
{
    assert(device != NULL);
    ++probe_count;
    return DEVICE_PROBE_FAILED;
}

static device_probe_result_t usb_probe(device_t* device)
{
    assert(device != NULL);
    ++usb_probe_count;
    return DEVICE_PROBE_BOUND;
}

static void usb_remove(device_t* device)
{
    assert(device != NULL);
    ++usb_remove_count;
}

int main(void)
{
    device_t root = {
        .name = "pci0000:00:01.0",
        .bus = DEVICE_BUS_PCI,
        .vendor_id = 0x8086U,
        .device_id = 0x2448U,
        .class_code = 0x06U,
        .subclass = 0x04U
    };
    device_t display = {
        .name = "pci0000:01:04.0",
        .bus = DEVICE_BUS_PCI,
        .parent = &root,
        .vendor_id = 0x10DEU,
        .device_id = 0x2503U,
        .class_code = 0x03U
    };
    device_t usb = {
        .name = "usb0000:00:01.0",
        .bus = DEVICE_BUS_USB,
        .vendor_id = 0x1234U,
        .device_id = 0x5678U
    };
    device_t duplicate = {
        .name = "pci0000:00:01.0",
        .bus = DEVICE_BUS_PCI
    };
    const device_id_t display_ids[] = {{
        .vendor_id = 0x10DEU,
        .device_id = DEVICE_ID_ANY16,
        .class_code = 0x03U,
        .subclass = DEVICE_ID_ANY8,
        .prog_if = DEVICE_ID_ANY8
    }};
    const device_id_t failing_ids[] = {{
        .vendor_id = 0x8086U,
        .device_id = DEVICE_ID_ANY16,
        .class_code = DEVICE_ID_ANY8,
        .subclass = DEVICE_ID_ANY8,
        .prog_if = DEVICE_ID_ANY8
    }};
    const device_driver_t display_driver = {
        .name = "test-display",
        .bus = DEVICE_BUS_PCI,
        .ids = display_ids,
        .id_count = 1U,
        .probe = display_probe,
        .remove = display_remove
    };
    const device_driver_t failing_driver = {
        .name = "test-failing",
        .bus = DEVICE_BUS_PCI,
        .ids = failing_ids,
        .id_count = 1U,
        .probe = failing_probe
    };
    const device_id_t usb_ids[] = {{
        .vendor_id = 0x1234U,
        .device_id = 0x5678U,
        .class_code = DEVICE_ID_ANY8,
        .subclass = DEVICE_ID_ANY8,
        .prog_if = DEVICE_ID_ANY8
    }};
    const device_driver_t usb_driver = {
        .name = "test-usb",
        .bus = DEVICE_BUS_USB,
        .ids = usb_ids,
        .id_count = 1U,
        .probe = usb_probe,
        .remove = usb_remove
    };

    assert(!device_register(&display)); /* Parent must be registered first. */
    assert(device_register(&root));
    assert(!device_register(&duplicate));
    assert(device_register(&display));
    assert(device_register(&usb));
    assert(device_count() == 3U);
    assert(device_get(0U) == &root);
    assert(device_find(DEVICE_BUS_PCI, "pci0000:01:04.0") == &display);
    assert(device_find(DEVICE_BUS_USB, "pci0000:01:04.0") == NULL);

    assert(device_driver_register(&display_driver));
    assert(device_driver_register(&usb_driver));
    assert(device_probe_all() == 2U);
    assert(probe_count == 1U);
    assert(usb_probe_count == 1U);
    assert(display.state == DEVICE_STATE_BOUND);
    assert(display.driver == &display_driver);
    assert(usb.state == DEVICE_STATE_BOUND);
    assert(usb.driver == &usb_driver);
    assert(root.state == DEVICE_STATE_REGISTERED);

    assert(!device_unregister(&root)); /* A registered child still exists. */
    assert(!device_driver_unregister(&display_driver));
    assert(device_unbind(&display));
    assert(remove_count == 1U);
    assert(display.state == DEVICE_STATE_REGISTERED);
    assert(display.driver == NULL);
    assert(device_unbind(&usb));
    assert(usb_remove_count == 1U);
    assert(usb.state == DEVICE_STATE_REGISTERED);
    assert(usb.driver == NULL);

    assert(device_acquire(&usb));
    assert(!device_unregister(&usb));
    device_release(&usb);
    assert(device_unregister(&usb));
    assert(device_unregister(&display));
    assert(device_unregister(&root));
    assert(device_driver_unregister(&display_driver));
    assert(device_driver_unregister(&usb_driver));

    assert(device_driver_register(&failing_driver));
    assert(device_register(&root));
    assert(device_bind(&root, &failing_driver) == DEVICE_BIND_FAILED);
    assert(root.state == DEVICE_STATE_FAILED);
    assert(root.driver == NULL);
    assert(probe_count == 2U);
    assert(device_unregister(&root));
    assert(device_driver_unregister(&failing_driver));
    return 0;
}
