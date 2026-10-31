/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "storage_driver.h"
#include "device.h"
#include "spinlock.h"

typedef struct
{
    const storage_driver_t* storage;
    device_id_t ids[STORAGE_DRIVER_ID_MAX];
    device_driver_t generic;
} storage_adapter_t;

static storage_adapter_t adapters[STORAGE_DRIVER_MAX];
static uint32_t adapters_used;
static spinlock_t storage_lock = SPINLOCK_INITIALIZER;

static int valid_name(const char* name)
{
    if (!name || !name[0])
        return 0;
    for (uint32_t i = 0U; i < STORAGE_DRIVER_NAME_MAX; ++i)
        if (name[i] == '\0')
            return 1;
    return 0;
}

static device_probe_result_t storage_adapter_probe(device_t* device)
{
    storage_adapter_t* adapter;
    if (!device || !device->driver || !device->bus_data)
        return DEVICE_PROBE_FAILED;
    adapter = (storage_adapter_t*)device->driver->context;
    if (!adapter || !adapter->storage || !adapter->storage->probe)
        return DEVICE_PROBE_FAILED;
    return (device_probe_result_t)adapter->storage->probe(
        (const struct pci_device*)device->bus_data);
}

int storage_driver_register(const storage_driver_t* driver)
{
    uint64_t flags;
    storage_adapter_t* adapter;

    if (!driver || !valid_name(driver->name) || !driver->ids ||
        !driver->id_count || driver->id_count > STORAGE_DRIVER_ID_MAX ||
        !driver->probe)
        return 0;

    flags = spinlock_lock_irqsave(&storage_lock);
    for (uint32_t i = 0U; i < adapters_used; ++i)
    {
        if (adapters[i].storage == driver ||
            (adapters[i].storage &&
             adapters[i].storage->name[0] == driver->name[0] &&
             valid_name(adapters[i].storage->name)))
        {
            int same_name = 1;
            for (uint32_t j = 0U; j < STORAGE_DRIVER_NAME_MAX; ++j)
            {
                if (adapters[i].storage->name[j] != driver->name[j])
                {
                    same_name = 0;
                    break;
                }
                if (driver->name[j] == '\0')
                    break;
            }
            if (same_name)
            {
                spinlock_unlock_irqrestore(&storage_lock, flags);
                return 0;
            }
        }
    }
    if (adapters_used >= STORAGE_DRIVER_MAX)
    {
        spinlock_unlock_irqrestore(&storage_lock, flags);
        return 0;
    }

    adapter = &adapters[adapters_used];
    adapter->storage = driver;
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
    adapter->generic.probe = storage_adapter_probe;
    adapter->generic.remove = NULL;
    adapter->generic.context = adapter;

    if (!device_driver_register(&adapter->generic))
    {
        adapter->storage = NULL;
        adapter->generic = (device_driver_t){0};
        spinlock_unlock_irqrestore(&storage_lock, flags);
        return 0;
    }

    ++adapters_used;
    spinlock_unlock_irqrestore(&storage_lock, flags);
    return 1;
}

uint32_t storage_driver_count(void)
{
    uint64_t flags = spinlock_lock_irqsave(&storage_lock);
    uint32_t count = adapters_used;
    spinlock_unlock_irqrestore(&storage_lock, flags);
    return count;
}

uint32_t storage_probe_pci_devices(void)
{
    const device_driver_t* snapshot[STORAGE_DRIVER_MAX];
    uint64_t flags = spinlock_lock_irqsave(&storage_lock);
    uint32_t driver_count = adapters_used;
    for (uint32_t i = 0U; i < driver_count; ++i)
        snapshot[i] = &adapters[i].generic;
    spinlock_unlock_irqrestore(&storage_lock, flags);

    uint32_t newly_bound = 0U;
    for (int index = 0; index < pci_get_device_count(); ++index)
    {
        const struct pci_device* pci = pci_get_device(index);
        if (!pci || pci->class_code != 0x01U ||
            pci->core.state != DEVICE_STATE_REGISTERED)
            continue;

        for (uint32_t i = 0U; i < driver_count; ++i)
        {
            device_bind_result_t result =
                device_bind((device_t*)&pci->core, snapshot[i]);
            if (result == DEVICE_BIND_BOUND)
            {
                ++newly_bound;
                break;
            }
            if (result == DEVICE_BIND_FAILED ||
                result == DEVICE_BIND_INVALID)
                break;
        }
    }
    return newly_bound;
}

const char* storage_driver_bound_name(const struct pci_device* pci)
{
    const char* name = NULL;
    if (!pci || pci->core.state != DEVICE_STATE_BOUND ||
        !pci->core.driver)
        return NULL;

    uint64_t flags = spinlock_lock_irqsave(&storage_lock);
    for (uint32_t i = 0U; i < adapters_used; ++i)
    {
        if (&adapters[i].generic == pci->core.driver)
        {
            name = adapters[i].storage->name;
            break;
        }
    }
    spinlock_unlock_irqrestore(&storage_lock, flags);
    return name;
}
