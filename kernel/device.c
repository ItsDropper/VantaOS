/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "device.h"
#include "spinlock.h"

#define DEVICE_NAME_SCAN_LIMIT DEVICE_NAME_MAX

typedef struct
{
    device_t* device;
    uint32_t references;
    uint64_t sequence;
} device_entry_t;

typedef struct
{
    const device_driver_t* driver;
    uint32_t references;
    uint32_t active_probes;
    uint64_t sequence;
} driver_entry_t;

static device_entry_t devices[DEVICE_MAX];
static driver_entry_t drivers[DEVICE_DRIVER_MAX];
static uint32_t devices_used;
static uint32_t drivers_used;
static uint64_t next_device_sequence;
static uint64_t next_driver_sequence;
static spinlock_t device_lock = SPINLOCK_INITIALIZER;

static int valid_name(const char* name)
{
    if (!name || !name[0])
        return 0;
    for (uint32_t i = 0U; i < DEVICE_NAME_SCAN_LIMIT; ++i)
        if (name[i] == '\0')
            return 1;
    return 0;
}

static int same_name(const char* left, const char* right)
{
    for (uint32_t i = 0U; i < DEVICE_NAME_SCAN_LIMIT; ++i)
    {
        if (left[i] != right[i])
            return 0;
        if (left[i] == '\0')
            return 1;
    }
    return 0;
}

static int find_device_locked(const device_t* device)
{
    for (uint32_t i = 0U; i < devices_used; ++i)
        if (devices[i].device == device)
            return (int)i;
    return -1;
}

static int find_driver_locked(const device_driver_t* driver)
{
    for (uint32_t i = 0U; i < drivers_used; ++i)
        if (drivers[i].driver == driver)
            return (int)i;
    return -1;
}

static int device_id_matches(const device_id_t* id, const device_t* device)
{
    return (id->vendor_id == DEVICE_ID_ANY16 ||
            id->vendor_id == device->vendor_id) &&
           (id->device_id == DEVICE_ID_ANY16 ||
            id->device_id == device->device_id) &&
           (id->class_code == DEVICE_ID_ANY8 ||
            id->class_code == device->class_code) &&
           (id->subclass == DEVICE_ID_ANY8 ||
            id->subclass == device->subclass) &&
           (id->prog_if == DEVICE_ID_ANY8 ||
            id->prog_if == device->prog_if);
}

static int driver_matches(const device_driver_t* driver,
                          const device_t* device)
{
    for (uint32_t i = 0U; i < driver->id_count; ++i)
        if (device_id_matches(&driver->ids[i], device))
            return 1;
    return 0;
}

int device_register(device_t* device)
{
    uint64_t flags;
    if (!device || !valid_name(device->name) ||
        device->bus <= DEVICE_BUS_NONE || device->bus > DEVICE_BUS_VIRTUAL ||
        device->state != DEVICE_STATE_UNREGISTERED || device->driver)
        return 0;

    flags = spinlock_lock_irqsave(&device_lock);
    if (devices_used >= DEVICE_MAX ||
        next_device_sequence == UINT64_MAX ||
        (device->parent && find_device_locked(device->parent) < 0))
    {
        spinlock_unlock_irqrestore(&device_lock, flags);
        return 0;
    }
    for (uint32_t i = 0U; i < devices_used; ++i)
    {
        if (devices[i].device == device ||
            (devices[i].device->bus == device->bus &&
             same_name(devices[i].device->name, device->name)))
        {
            spinlock_unlock_irqrestore(&device_lock, flags);
            return 0;
        }
    }

    devices[devices_used].device = device;
    devices[devices_used].references = 0U;
    devices[devices_used].sequence = ++next_device_sequence;
    ++devices_used;
    device->state = DEVICE_STATE_REGISTERED;
    spinlock_unlock_irqrestore(&device_lock, flags);
    return 1;
}

int device_unregister(device_t* device)
{
    uint64_t flags;
    int index;
    if (!device)
        return 0;

    flags = spinlock_lock_irqsave(&device_lock);
    index = find_device_locked(device);
    if (index < 0 || devices[index].references != 0U ||
        (device->state != DEVICE_STATE_REGISTERED &&
         device->state != DEVICE_STATE_FAILED) ||
        device->driver)
    {
        spinlock_unlock_irqrestore(&device_lock, flags);
        return 0;
    }
    for (uint32_t i = 0U; i < devices_used; ++i)
    {
        if (devices[i].device->parent == device)
        {
            spinlock_unlock_irqrestore(&device_lock, flags);
            return 0;
        }
    }

    for (uint32_t i = (uint32_t)index + 1U; i < devices_used; ++i)
        devices[i - 1U] = devices[i];
    devices[--devices_used].device = 0;
    devices[devices_used].references = 0U;
    device->state = DEVICE_STATE_UNREGISTERED;
    spinlock_unlock_irqrestore(&device_lock, flags);
    return 1;
}

int device_acquire(device_t* device)
{
    uint64_t flags;
    int index;
    if (!device)
        return 0;
    flags = spinlock_lock_irqsave(&device_lock);
    index = find_device_locked(device);
    if (index < 0 || device->state == DEVICE_STATE_REMOVING ||
        devices[index].references == UINT32_MAX)
    {
        spinlock_unlock_irqrestore(&device_lock, flags);
        return 0;
    }
    ++devices[index].references;
    spinlock_unlock_irqrestore(&device_lock, flags);
    return 1;
}

void device_release(device_t* device)
{
    uint64_t flags;
    int index;
    if (!device)
        return;
    flags = spinlock_lock_irqsave(&device_lock);
    index = find_device_locked(device);
    if (index >= 0 && devices[index].references != 0U)
        --devices[index].references;
    spinlock_unlock_irqrestore(&device_lock, flags);
}

int device_driver_register(const device_driver_t* driver)
{
    uint64_t flags;
    if (!driver || !valid_name(driver->name) ||
        driver->bus <= DEVICE_BUS_NONE || driver->bus > DEVICE_BUS_VIRTUAL ||
        !driver->ids || !driver->id_count || !driver->probe)
        return 0;

    flags = spinlock_lock_irqsave(&device_lock);
    if (drivers_used >= DEVICE_DRIVER_MAX ||
        next_driver_sequence == UINT64_MAX)
    {
        spinlock_unlock_irqrestore(&device_lock, flags);
        return 0;
    }
    for (uint32_t i = 0U; i < drivers_used; ++i)
    {
        if (drivers[i].driver == driver ||
            (drivers[i].driver->bus == driver->bus &&
             same_name(drivers[i].driver->name, driver->name)))
        {
            spinlock_unlock_irqrestore(&device_lock, flags);
            return 0;
        }
    }
    drivers[drivers_used].driver = driver;
    drivers[drivers_used].references = 0U;
    drivers[drivers_used].active_probes = 0U;
    drivers[drivers_used].sequence = ++next_driver_sequence;
    ++drivers_used;
    spinlock_unlock_irqrestore(&device_lock, flags);
    return 1;
}

int device_driver_unregister(const device_driver_t* driver)
{
    uint64_t flags;
    int index;
    if (!driver)
        return 0;
    flags = spinlock_lock_irqsave(&device_lock);
    index = find_driver_locked(driver);
    if (index < 0 || drivers[index].references != 0U ||
        drivers[index].active_probes != 0U)
    {
        spinlock_unlock_irqrestore(&device_lock, flags);
        return 0;
    }
    for (uint32_t i = 0U; i < devices_used; ++i)
    {
        if (devices[i].device->driver == driver)
        {
            spinlock_unlock_irqrestore(&device_lock, flags);
            return 0;
        }
    }
    for (uint32_t i = (uint32_t)index + 1U; i < drivers_used; ++i)
        drivers[i - 1U] = drivers[i];
    drivers[--drivers_used].driver = 0;
    drivers[drivers_used].references = 0U;
    drivers[drivers_used].active_probes = 0U;
    spinlock_unlock_irqrestore(&device_lock, flags);
    return 1;
}

device_bind_result_t device_bind(device_t* device,
                                 const device_driver_t* driver)
{
    uint64_t flags;
    int device_index;
    int driver_index;
    device_probe_result_t result;

    if (!device || !driver)
        return DEVICE_BIND_INVALID;
    flags = spinlock_lock_irqsave(&device_lock);
    device_index = find_device_locked(device);
    driver_index = find_driver_locked(driver);
    if (device_index < 0 || driver_index < 0 ||
        device->state != DEVICE_STATE_REGISTERED ||
        devices[device_index].references == UINT32_MAX ||
        drivers[driver_index].active_probes == UINT32_MAX)
    {
        spinlock_unlock_irqrestore(&device_lock, flags);
        return DEVICE_BIND_INVALID;
    }
    if (device->bus != driver->bus)
    {
        spinlock_unlock_irqrestore(&device_lock, flags);
        return DEVICE_BIND_NO_MATCH;
    }

    ++devices[device_index].references;
    ++drivers[driver_index].active_probes;
    device->driver = driver;
    device->state = DEVICE_STATE_PROBING;
    spinlock_unlock_irqrestore(&device_lock, flags);

    if (!driver_matches(driver, device))
        result = DEVICE_PROBE_NO_MATCH;
    else
        result = driver->probe(device);
    if (result != DEVICE_PROBE_BOUND &&
        result != DEVICE_PROBE_NO_MATCH &&
        result != DEVICE_PROBE_FAILED)
        result = DEVICE_PROBE_FAILED;

    flags = spinlock_lock_irqsave(&device_lock);
    device_index = find_device_locked(device);
    driver_index = find_driver_locked(driver);
    if (device_index >= 0 && driver_index >= 0)
    {
        /* A probe callback may not mutate registry-owned lifecycle state. */
        if (device->state != DEVICE_STATE_PROBING ||
            device->driver != driver)
            result = DEVICE_PROBE_FAILED;
        if (result == DEVICE_PROBE_BOUND)
            device->state = DEVICE_STATE_BOUND;
        else
        {
            device->driver = 0;
            device->state = result == DEVICE_PROBE_FAILED ?
                DEVICE_STATE_FAILED : DEVICE_STATE_REGISTERED;
        }
        if (devices[device_index].references != 0U)
            --devices[device_index].references;
        if (drivers[driver_index].active_probes != 0U)
            --drivers[driver_index].active_probes;
    }
    spinlock_unlock_irqrestore(&device_lock, flags);

    if (result == DEVICE_PROBE_BOUND)
        return DEVICE_BIND_BOUND;
    if (result == DEVICE_PROBE_FAILED)
        return DEVICE_BIND_FAILED;
    return DEVICE_BIND_NO_MATCH;
}

int device_unbind(device_t* device)
{
    uint64_t flags;
    int device_index;
    int driver_index;
    const device_driver_t* driver;

    if (!device)
        return 0;
    flags = spinlock_lock_irqsave(&device_lock);
    device_index = find_device_locked(device);
    if (device_index < 0 || device->state != DEVICE_STATE_BOUND ||
        !device->driver || devices[device_index].references == UINT32_MAX)
    {
        spinlock_unlock_irqrestore(&device_lock, flags);
        return 0;
    }
    driver = device->driver;
    driver_index = find_driver_locked(driver);
    if (driver_index < 0 || drivers[driver_index].references == UINT32_MAX)
    {
        spinlock_unlock_irqrestore(&device_lock, flags);
        return 0;
    }
    ++devices[device_index].references;
    ++drivers[driver_index].references;
    device->state = DEVICE_STATE_REMOVING;
    spinlock_unlock_irqrestore(&device_lock, flags);

    if (driver->remove)
        driver->remove(device);

    flags = spinlock_lock_irqsave(&device_lock);
    device_index = find_device_locked(device);
    driver_index = find_driver_locked(driver);
    if (device_index >= 0 && driver_index >= 0)
    {
        device->driver = 0;
        device->state = DEVICE_STATE_REGISTERED;
        if (devices[device_index].references != 0U)
            --devices[device_index].references;
        if (drivers[driver_index].references != 0U)
            --drivers[driver_index].references;
    }
    spinlock_unlock_irqrestore(&device_lock, flags);
    return 1;
}

uint32_t device_probe_all(void)
{
    uint32_t bound = 0U;
    uint64_t device_cursor = 0U;

    /*
     * Sequence-based cursors keep iteration stable when unrelated devices or
     * drivers are concurrently removed from the compact registry arrays.
     * Each object is pinned before dropping the lock for callbacks.
     */
    for (;;)
    {
        device_t* device = NULL;
        uint64_t device_sequence = UINT64_MAX;
        uint64_t flags = spinlock_lock_irqsave(&device_lock);
        int device_index = -1;
        for (uint32_t i = 0U; i < devices_used; ++i)
        {
            if (devices[i].sequence > device_cursor &&
                devices[i].sequence < device_sequence &&
                devices[i].device->state == DEVICE_STATE_REGISTERED &&
                devices[i].references != UINT32_MAX)
            {
                device_index = (int)i;
                device_sequence = devices[i].sequence;
            }
        }
        if (device_index >= 0)
        {
            device = devices[device_index].device;
            ++devices[device_index].references;
            device_cursor = device_sequence;
        }
        spinlock_unlock_irqrestore(&device_lock, flags);
        if (!device)
            break;

        uint64_t driver_cursor = 0U;
        for (;;)
        {
            const device_driver_t* driver = NULL;
            uint64_t driver_sequence = UINT64_MAX;
            flags = spinlock_lock_irqsave(&device_lock);
            int driver_index = -1;
            for (uint32_t i = 0U; i < drivers_used; ++i)
            {
                if (drivers[i].sequence > driver_cursor &&
                    drivers[i].sequence < driver_sequence &&
                    drivers[i].references != UINT32_MAX)
                {
                    driver_index = (int)i;
                    driver_sequence = drivers[i].sequence;
                }
            }
            if (driver_index >= 0)
            {
                driver = drivers[driver_index].driver;
                ++drivers[driver_index].references;
                driver_cursor = driver_sequence;
            }
            spinlock_unlock_irqrestore(&device_lock, flags);
            if (!driver)
                break;

            device_bind_result_t result = device_bind(device, driver);
            flags = spinlock_lock_irqsave(&device_lock);
            int current_driver = find_driver_locked(driver);
            if (current_driver >= 0 &&
                drivers[current_driver].references != 0U)
                --drivers[current_driver].references;
            spinlock_unlock_irqrestore(&device_lock, flags);

            if (result == DEVICE_BIND_BOUND)
            {
                ++bound;
                break;
            }
            if (result == DEVICE_BIND_FAILED ||
                result == DEVICE_BIND_INVALID)
                break;
        }

        device_release(device);
    }
    return bound;
}

uint32_t device_count(void)
{
    uint64_t flags = spinlock_lock_irqsave(&device_lock);
    uint32_t count = devices_used;
    spinlock_unlock_irqrestore(&device_lock, flags);
    return count;
}

device_t* device_get(uint32_t index)
{
    uint64_t flags = spinlock_lock_irqsave(&device_lock);
    device_t* device = index < devices_used ? devices[index].device : 0;
    spinlock_unlock_irqrestore(&device_lock, flags);
    return device;
}

device_t* device_find(device_bus_t bus, const char* name)
{
    uint64_t flags;
    device_t* found = 0;
    if (!valid_name(name))
        return 0;
    flags = spinlock_lock_irqsave(&device_lock);
    for (uint32_t i = 0U; i < devices_used; ++i)
    {
        if (devices[i].device->bus == bus &&
            same_name(devices[i].device->name, name))
        {
            found = devices[i].device;
            break;
        }
    }
    spinlock_unlock_irqrestore(&device_lock, flags);
    return found;
}
