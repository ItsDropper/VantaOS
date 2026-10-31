/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "net.h"
#include "device.h"

#include <assert.h>

static struct pci_device test_pci;
static uint32_t probe_count;
static uint32_t poll_count;

static net_pci_probe_result_t test_probe(const struct pci_device* pci)
{
    assert(pci == &test_pci);
    ++probe_count;
    return NET_PCI_PROBE_BOUND;
}

static int test_poll(void)
{
    ++poll_count;
    return 3;
}

static const net_pci_id_t test_ids[] = {
    {0xFFFFU, 0xFFFFU, 0x02U, 0x00U, 0x00U}
};
static const net_pci_driver_t test_driver = {
    "test-net", test_ids, 1U, test_probe, test_poll
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
    test_pci.class_code = 0x02U;
    test_pci.subclass = 0x00U;
    test_pci.prog_if = 0x00U;
    test_pci.core.bus = DEVICE_BUS_PCI;
    test_pci.core.bus_data = &test_pci;
    test_pci.core.class_code = test_pci.class_code;
    test_pci.core.name[0] = 'n';
    test_pci.core.name[1] = 'e';
    test_pci.core.name[2] = 't';
    test_pci.core.name[3] = '0';
    test_pci.core.name[4] = '\0';

    assert(device_register(&test_pci.core));
    assert(net_pci_driver_register(&test_driver));
    assert(!net_pci_driver_register(&test_driver));
    assert(net_probe_pci_devices() == 1U);
    assert(probe_count == 1U);
    assert(test_pci.core.state == DEVICE_STATE_BOUND);
    assert(net_pci_bound_driver(&test_pci) != NULL);
    assert(net_pci_bound_driver(&test_pci)[0] == 't');
    assert(net_pci_poll_devices() == 3U);
    assert(poll_count == 1U);

    /* No hardware teardown callback exists, so unbind must fail closed. */
    assert(!device_unbind(&test_pci.core));
    assert(test_pci.core.state == DEVICE_STATE_BOUND);
    assert(net_pci_bound_driver(&test_pci) != NULL);
    assert(net_pci_poll_devices() == 3U);
    assert(poll_count == 2U);
    return 0;
}
