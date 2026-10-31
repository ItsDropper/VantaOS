/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "block_device.h"
#include "device.h"
#include "spinlock.h"

static block_device_t* devices[BLOCK_DEVICE_MAX];
static uint32_t device_count_value;
static spinlock_t registry_lock = SPINLOCK_INITIALIZER;

static int valid_sector_size(uint32_t size)
{
    return size >= 512U && size <= 65536U &&
           (size & (size - 1U)) == 0U;
}

static int valid_name(const char* name)
{
    if (!name || !name[0])
        return 0;
    for (uint32_t i = 0U; i < BLOCK_DEVICE_NAME_MAX; ++i)
        if (name[i] == '\0')
            return 1;
    return 0;
}

static int same_name(const char* left, const char* right)
{
    for (uint32_t i = 0U; i < BLOCK_DEVICE_NAME_MAX; ++i)
    {
        if (left[i] != right[i])
            return 0;
        if (left[i] == '\0')
            return 1;
    }
    return 0;
}

static int find_device_locked(const block_device_t* device)
{
    for (uint32_t i = 0U; i < device_count_value; ++i)
        if (devices[i] == device)
            return (int)i;
    return -1;
}

static void prepare_core_device(block_device_t* device, const char* name)
{
    uint32_t i = 0U;
    while (i + 1U < DEVICE_NAME_MAX && name[i])
    {
        device->core.name[i] = name[i];
        ++i;
    }
    device->core.name[i] = '\0';
    device->core.bus = DEVICE_BUS_VIRTUAL;
    device->core.parent = device->parent_device;
    device->core.bus_data = device;
    device->core.driver = NULL;
    device->core.state = DEVICE_STATE_UNREGISTERED;
}

block_status_t block_device_register(block_device_t* device)
{
    if (!device || !valid_name(device->name) ||
        !valid_sector_size(device->sector_size) ||
        !device->sector_count || !device->ops || !device->ops->read)
        return BLOCK_STATUS_INVALID;
    if (device->core.state != DEVICE_STATE_UNREGISTERED ||
        device->core.driver)
        return BLOCK_STATUS_BUSY;

    uint64_t flags = spinlock_lock_irqsave(&registry_lock);
    for (uint32_t i = 0U; i < device_count_value; ++i)
    {
        if (devices[i] == device ||
            same_name(devices[i]->name, device->name))
        {
            spinlock_unlock_irqrestore(&registry_lock, flags);
            return BLOCK_STATUS_BUSY;
        }
    }
    if (device_count_value >= BLOCK_DEVICE_MAX)
    {
        spinlock_unlock_irqrestore(&registry_lock, flags);
        return BLOCK_STATUS_NO_MEMORY;
    }

    prepare_core_device(device, device->name);
    if (!device_register(&device->core))
    {
        spinlock_unlock_irqrestore(&registry_lock, flags);
        return BLOCK_STATUS_BUSY;
    }
    device->active_operations = 0U;
    device->online = 1U;
    devices[device_count_value++] = device;
    spinlock_unlock_irqrestore(&registry_lock, flags);
    return BLOCK_STATUS_OK;
}

static void make_disk_name(char* name, uint32_t number)
{
    char digits[10];
    uint32_t count = 0U;
    name[0] = 'd';
    name[1] = 'i';
    name[2] = 's';
    name[3] = 'k';
    do
    {
        digits[count++] = (char)('0' + number % 10U);
        number /= 10U;
    } while (number && count < sizeof(digits));

    uint32_t position = 4U;
    while (count)
        name[position++] = digits[--count];
    name[position] = '\0';
}

block_status_t block_device_register_disk(block_device_t* device)
{
    if (!device || !valid_sector_size(device->sector_size) ||
        !device->sector_count || !device->ops || !device->ops->read)
        return BLOCK_STATUS_INVALID;
    if (device->core.state != DEVICE_STATE_UNREGISTERED ||
        device->core.driver)
        return BLOCK_STATUS_BUSY;

    uint64_t flags = spinlock_lock_irqsave(&registry_lock);
    if (find_device_locked(device) >= 0)
    {
        spinlock_unlock_irqrestore(&registry_lock, flags);
        return BLOCK_STATUS_BUSY;
    }
    if (device_count_value >= BLOCK_DEVICE_MAX)
    {
        spinlock_unlock_irqrestore(&registry_lock, flags);
        return BLOCK_STATUS_NO_MEMORY;
    }

    char candidate[BLOCK_DEVICE_NAME_MAX];
    for (uint32_t number = 0U; number < BLOCK_DEVICE_MAX; ++number)
    {
        make_disk_name(candidate, number);
        int collision = 0;
        for (uint32_t i = 0U; i < device_count_value; ++i)
        {
            if (same_name(devices[i]->name, candidate))
            {
                collision = 1;
                break;
            }
        }
        if (collision)
            continue;

        prepare_core_device(device, candidate);
        if (!device_register(&device->core))
        {
            spinlock_unlock_irqrestore(&registry_lock, flags);
            return BLOCK_STATUS_BUSY;
        }
        uint32_t i = 0U;
        do
        {
            device->name[i] = candidate[i];
        } while (candidate[i++] && i < BLOCK_DEVICE_NAME_MAX);
        device->active_operations = 0U;
        device->online = 1U;
        devices[device_count_value++] = device;
        spinlock_unlock_irqrestore(&registry_lock, flags);
        return BLOCK_STATUS_OK;
    }

    spinlock_unlock_irqrestore(&registry_lock, flags);
    return BLOCK_STATUS_NO_MEMORY;
}

block_status_t block_device_unregister(block_device_t* device)
{
    if (!device)
        return BLOCK_STATUS_INVALID;

    uint64_t flags = spinlock_lock_irqsave(&registry_lock);
    int index = find_device_locked(device);
    if (index < 0)
    {
        spinlock_unlock_irqrestore(&registry_lock, flags);
        return BLOCK_STATUS_NOT_READY;
    }

    device->online = 0U;
    if (__atomic_load_n(&device->active_operations, __ATOMIC_ACQUIRE) != 0U)
    {
        __atomic_store_n(&device->online, 1U, __ATOMIC_RELEASE);
        spinlock_unlock_irqrestore(&registry_lock, flags);
        return BLOCK_STATUS_BUSY;
    }
    /*
     * The generic tree rejects removal while partitions or other child
     * devices still reference this block device as their parent.
     */
    if (!device_unregister(&device->core))
    {
        __atomic_store_n(&device->online, 1U, __ATOMIC_RELEASE);
        spinlock_unlock_irqrestore(&registry_lock, flags);
        return BLOCK_STATUS_BUSY;
    }

    for (uint32_t i = (uint32_t)index + 1U; i < device_count_value; ++i)
        devices[i - 1U] = devices[i];
    devices[--device_count_value] = NULL;
    spinlock_unlock_irqrestore(&registry_lock, flags);
    return BLOCK_STATUS_OK;
}

uint32_t block_device_count(void)
{
    uint64_t flags = spinlock_lock_irqsave(&registry_lock);
    uint32_t count = device_count_value;
    spinlock_unlock_irqrestore(&registry_lock, flags);
    return count;
}

block_device_t* block_device_get(uint32_t index)
{
    uint64_t flags = spinlock_lock_irqsave(&registry_lock);
    block_device_t* device =
        index < device_count_value ? devices[index] : NULL;
    spinlock_unlock_irqrestore(&registry_lock, flags);
    return device;
}

block_device_t* block_device_find(const char* name)
{
    if (!valid_name(name))
        return NULL;
    uint64_t flags = spinlock_lock_irqsave(&registry_lock);
    block_device_t* found = NULL;
    for (uint32_t i = 0U; i < device_count_value; ++i)
    {
        if (same_name(devices[i]->name, name))
        {
            found = devices[i];
            break;
        }
    }
    spinlock_unlock_irqrestore(&registry_lock, flags);
    return found;
}

static block_status_t begin_io(block_device_t* device, uint64_t lba,
                               uint32_t count, const void* buffer)
{
    if (!device || !buffer || !count)
        return BLOCK_STATUS_INVALID;

    uint64_t flags = spinlock_lock_irqsave(&registry_lock);
    if (find_device_locked(device) < 0 ||
        !__atomic_load_n(&device->online, __ATOMIC_ACQUIRE))
    {
        spinlock_unlock_irqrestore(&registry_lock, flags);
        return BLOCK_STATUS_NOT_READY;
    }
    if (lba >= device->sector_count ||
        (uint64_t)count > device->sector_count - lba)
    {
        spinlock_unlock_irqrestore(&registry_lock, flags);
        return BLOCK_STATUS_OUT_OF_RANGE;
    }
    __atomic_add_fetch(&device->active_operations, 1U, __ATOMIC_ACQ_REL);
    spinlock_unlock_irqrestore(&registry_lock, flags);
    return BLOCK_STATUS_OK;
}

static void end_io(block_device_t* device)
{
    __atomic_sub_fetch(&device->active_operations, 1U, __ATOMIC_RELEASE);
}

block_status_t block_device_read(block_device_t* device, uint64_t lba,
                                 uint32_t count, void* buffer)
{
    block_status_t status = begin_io(device, lba, count, buffer);
    if (status != BLOCK_STATUS_OK)
        return status;
    status = device->ops->read(device, lba, count, buffer);
    end_io(device);
    return status;
}

block_status_t block_device_write(block_device_t* device, uint64_t lba,
                                  uint32_t count, const void* buffer)
{
    block_status_t status = begin_io(device, lba, count, buffer);
    if (status != BLOCK_STATUS_OK)
        return status;
    if (device->flags & BLOCK_DEVICE_READ_ONLY)
        status = BLOCK_STATUS_READ_ONLY;
    else if (!device->ops->write)
        status = BLOCK_STATUS_UNSUPPORTED;
    else
        status = device->ops->write(device, lba, count, buffer);
    end_io(device);
    return status;
}

block_status_t block_device_flush(block_device_t* device)
{
    if (!device)
        return BLOCK_STATUS_INVALID;
    uint64_t flags = spinlock_lock_irqsave(&registry_lock);
    if (find_device_locked(device) < 0 ||
        !__atomic_load_n(&device->online, __ATOMIC_ACQUIRE))
    {
        spinlock_unlock_irqrestore(&registry_lock, flags);
        return BLOCK_STATUS_NOT_READY;
    }
    __atomic_add_fetch(&device->active_operations, 1U, __ATOMIC_ACQ_REL);
    spinlock_unlock_irqrestore(&registry_lock, flags);

    block_status_t status =
        (!device->ops || !device->ops->flush) ? BLOCK_STATUS_UNSUPPORTED :
        device->ops->flush(device);
    end_io(device);
    return status;
}
