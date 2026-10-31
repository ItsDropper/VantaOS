/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "storage_driver.h"
#include "device.h"

#include <assert.h>
#include <stddef.h>

static struct pci_device test_pci;

static uint32_t pci_probe_count;

static storage_probe_result_t test_storage_probe(const struct pci_device* pci)
{
    assert(pci == &test_pci);
    ++pci_probe_count;
    return STORAGE_PROBE_BOUND;
}

static const storage_pci_id_t test_ids[] = {
    {0xFFFFU, 0xFFFFU, 0x01U, 0x08U, 0x02U}
};
static const storage_driver_t test_driver = {
    "test-nvme", test_ids, 1U, test_storage_probe
};

int pci_get_device_count(void)
{
    return 1;
}

const struct pci_device* pci_get_device(int index)
{
    return index == 0 ? &test_pci : NULL;
}

int main(void)
{
    test_pci.vendor_id = 0x1234U;
    test_pci.device_id = 0x5678U;
    test_pci.class_code = 0x01U;
    test_pci.subclass = 0x08U;
    test_pci.prog_if = 0x02U;
    test_pci.core.bus = DEVICE_BUS_PCI;
    test_pci.core.bus_data = &test_pci;
    test_pci.core.vendor_id = test_pci.vendor_id;
    test_pci.core.device_id = test_pci.device_id;
    test_pci.core.class_code = test_pci.class_code;
    test_pci.core.subclass = test_pci.subclass;
    test_pci.core.prog_if = test_pci.prog_if;
    test_pci.core.name[0] = 'n';
    test_pci.core.name[1] = 'v';
    test_pci.core.name[2] = 'm';
    test_pci.core.name[3] = 'e';
    test_pci.core.name[4] = '0';
    test_pci.core.name[5] = '\0';

    assert(device_register(&test_pci.core));
    assert(storage_driver_register(&test_driver));
    assert(!storage_driver_register(&test_driver));
    assert(storage_driver_count() == 1U);
    assert(storage_probe_pci_devices() == 1U);
    assert(pci_probe_count == 1U);
    assert(test_pci.core.state == DEVICE_STATE_BOUND);
    assert(storage_driver_bound_name(&test_pci) != NULL);
    assert(storage_driver_bound_name(&test_pci)[0] == 't');

    assert(device_unbind(&test_pci.core));
    assert(test_pci.core.state == DEVICE_STATE_REGISTERED);
    assert(storage_driver_bound_name(&test_pci) == NULL);
    assert(device_unregister(&test_pci.core));
    return 0;
}
