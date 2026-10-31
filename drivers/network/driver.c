/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "net.h"
#include "device.h"
#include "spinlock.h"

#include <stddef.h>

typedef struct
{
    const net_pci_driver_t* network;
    device_id_t ids[NET_PCI_ID_MAX];
    device_driver_t generic;
} net_driver_adapter_t;

typedef struct
{
    const struct pci_device* device;
    const net_pci_driver_t* driver;
} net_binding_t;

static net_driver_adapter_t adapters[NET_PCI_DRIVER_MAX];
static uint32_t adapter_count;
static net_binding_t bindings[NET_PCI_BINDING_MAX];
static uint32_t binding_count;
static uint32_t binding_reservations;
static spinlock_t driver_lock = SPINLOCK_INITIALIZER;

static int valid_driver(const net_pci_driver_t* driver)
{
    if (!driver || !driver->name || !driver->name[0] ||
        !driver->ids || !driver->id_count ||
        driver->id_count > NET_PCI_ID_MAX || !driver->probe)
        return 0;
    for (uint32_t i = 0U; i < 32U; ++i)
        if (driver->name[i] == '\0')
            return 1;
    return 0;
}


static device_probe_result_t net_adapter_probe(device_t* device)
{
    net_driver_adapter_t* adapter;
    net_pci_probe_result_t result;
    if (!device || !device->driver || !device->bus_data)
        return DEVICE_PROBE_FAILED;
    adapter = (net_driver_adapter_t*)device->driver->context;
    if (!adapter || !adapter->network || !adapter->network->probe)
        return DEVICE_PROBE_FAILED;

    result = adapter->network->probe(
        (const struct pci_device*)device->bus_data);
    if (result == NET_PCI_PROBE_BOUND)
        return DEVICE_PROBE_BOUND;
    if (result == NET_PCI_PROBE_FAILED)
        return DEVICE_PROBE_FAILED;
    return DEVICE_PROBE_NO_MATCH;
}

int net_pci_driver_register(const net_pci_driver_t* driver)
{
    uint64_t flags;
    net_driver_adapter_t* adapter;

    if (!valid_driver(driver))
        return 0;

    flags = spinlock_lock_irqsave(&driver_lock);
    for (uint32_t i = 0U; i < adapter_count; ++i)
    {
        if (adapters[i].network == driver)
        {
            spinlock_unlock_irqrestore(&driver_lock, flags);
            return 0;
        }
    }
    if (adapter_count >= NET_PCI_DRIVER_MAX)
    {
        spinlock_unlock_irqrestore(&driver_lock, flags);
        return 0;
    }

    adapter = &adapters[adapter_count];
    adapter->network = driver;
    for (uint32_t i = 0U; i < driver->id_count; ++i)
    {
        adapter->ids[i].vendor_id = driver->ids[i].vendor_id;
        adapter->ids[i].device_id = driver->ids[i].device_id;
        adapter->ids[i].class_code = driver->ids[i].class_code;
        adapter->ids[i].subclass = driver->ids[i].subclass;
        adapter->ids[i].prog_if = driver->ids[i].prog_if;
    }
    adapter->generic.name = driver->name;
    adapter->generic.bus = DEVICE_BUS_PCI;
    adapter->generic.ids = adapter->ids;
    adapter->generic.id_count = driver->id_count;
    adapter->generic.probe = net_adapter_probe;
    /* Hardware teardown is not implemented; generic unbind stays disabled. */
    adapter->generic.remove = NULL;
    adapter->generic.context = adapter;

    if (!device_driver_register(&adapter->generic))
    {
        adapter->network = NULL;
        adapter->generic = (device_driver_t){0};
        spinlock_unlock_irqrestore(&driver_lock, flags);
        return 0;
    }

    ++adapter_count;
    spinlock_unlock_irqrestore(&driver_lock, flags);
    return 1;
}

static int bound(const struct pci_device* device)
{
    for (uint32_t i = 0U; i < binding_count; ++i)
        if (bindings[i].device == device)
            return 1;
    return 0;
}

uint32_t net_probe_pci_devices(void)
{
    const device_driver_t* snapshot[NET_PCI_DRIVER_MAX];
    uint64_t flags = spinlock_lock_irqsave(&driver_lock);
    uint32_t count = adapter_count;
    for (uint32_t i = 0U; i < count; ++i)
        snapshot[i] = &adapters[i].generic;
    spinlock_unlock_irqrestore(&driver_lock, flags);

    uint32_t added = 0U;
    for (int index = 0; index < pci_get_device_count(); ++index)
    {
        const struct pci_device* pci = pci_get_device(index);
        if (!pci || pci->class_code != 0x02U ||
            pci->core.state != DEVICE_STATE_REGISTERED)
            continue;

        flags = spinlock_lock_irqsave(&driver_lock);
        int claimed = bound(pci);
        int room = binding_count + binding_reservations < NET_PCI_BINDING_MAX;
        spinlock_unlock_irqrestore(&driver_lock, flags);
        if (claimed || !room)
            continue;

        for (uint32_t i = 0U; i < count; ++i)
        {
            flags = spinlock_lock_irqsave(&driver_lock);
            if (binding_count + binding_reservations >= NET_PCI_BINDING_MAX)
            {
                spinlock_unlock_irqrestore(&driver_lock, flags);
                break;
            }
            ++binding_reservations;
            spinlock_unlock_irqrestore(&driver_lock, flags);

            device_bind_result_t result =
                device_bind((device_t*)&pci->core, snapshot[i]);
            flags = spinlock_lock_irqsave(&driver_lock);
            if (binding_reservations != 0U)
                --binding_reservations;
            if (result == DEVICE_BIND_BOUND && !bound(pci) &&
                binding_count < NET_PCI_BINDING_MAX)
            {
                bindings[binding_count].device = pci;
                /* Context points to the adapter, not the public descriptor. */
                bindings[binding_count].driver =
                    ((const net_driver_adapter_t*)snapshot[i]->context)->network;
                ++binding_count;
                ++added;
            }
            spinlock_unlock_irqrestore(&driver_lock, flags);

            if (result == DEVICE_BIND_BOUND ||
                result == DEVICE_BIND_FAILED ||
                result == DEVICE_BIND_INVALID)
                break;
        }
    }
    return added;
}

const char* net_pci_bound_driver(const struct pci_device* device)
{
    const char* name = NULL;
    if (!device)
        return NULL;
    uint64_t flags = spinlock_lock_irqsave(&driver_lock);
    for (uint32_t i = 0U; i < binding_count; ++i)
    {
        if (bindings[i].device == device)
        {
            name = bindings[i].driver->name;
            break;
        }
    }
    spinlock_unlock_irqrestore(&driver_lock, flags);
    return name;
}

size_t net_pci_poll_devices(void)
{
    const net_pci_driver_t* snapshot[NET_PCI_BINDING_MAX];
    uint32_t count = 0U;
    uint64_t flags = spinlock_lock_irqsave(&driver_lock);
    for (uint32_t i = 0U; i < binding_count; ++i)
        if (bindings[i].driver->poll)
            snapshot[count++] = bindings[i].driver;
    spinlock_unlock_irqrestore(&driver_lock, flags);

    size_t work = 0U;
    for (uint32_t i = 0U; i < count; ++i)
    {
        int result = snapshot[i]->poll();
        if (result > 0)
            work += (size_t)result;
    }
    return work;
}
