/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "block_device.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static block_device_t device;
static block_device_t duplicate;
static unsigned read_calls;
static unsigned write_calls;
static unsigned flush_calls;
static block_status_t nested_unregister_status;
static uint8_t storage[8U * 512U];

static block_status_t mock_read(block_device_t *d, uint64_t lba,
                                uint32_t count, void *buffer)
{
    assert(d == &device);
    ++read_calls;
    memcpy(buffer, storage + (size_t)lba * d->sector_size,
           (size_t)count * d->sector_size);
    nested_unregister_status = block_device_unregister(d);
    return BLOCK_STATUS_OK;
}

static block_status_t mock_write(block_device_t *d, uint64_t lba,
                                 uint32_t count, const void *buffer)
{
    assert(d == &device);
    ++write_calls;
    memcpy(storage + (size_t)lba * d->sector_size, buffer,
           (size_t)count * d->sector_size);
    return BLOCK_STATUS_OK;
}

static block_status_t mock_flush(block_device_t *d)
{
    assert(d == &device);
    ++flush_calls;
    return BLOCK_STATUS_OK;
}

static const block_device_ops_t full_ops = {
    mock_read, mock_write, mock_flush
};
static const block_device_ops_t read_only_ops = {
    mock_read, 0, mock_flush
};

static void initialize_device(block_device_t *d, const char *name,
                              const block_device_ops_t *ops)
{
    memset(d, 0, sizeof(*d));
    size_t i = 0U;
    while (i + 1U < sizeof(d->name) && name[i]) {
        d->name[i] = name[i];
        ++i;
    }
    d->name[i] = '\0';
    d->sector_count = 8U;
    d->sector_size = 512U;
    d->ops = ops;
}

int main(void)
{
    uint8_t input[512U];
    uint8_t output[512U];
    memset(input, 0xA5, sizeof(input));
    memset(output, 0, sizeof(output));
    memset(storage, 0, sizeof(storage));

    initialize_device(&device, "unit-disk", &full_ops);
    initialize_device(&duplicate, "unit-disk", &full_ops);

    assert(block_device_register(0) == BLOCK_STATUS_INVALID);
    assert(block_device_register(&device) == BLOCK_STATUS_OK);
    assert(device.online == 1U);
    assert(device.active_operations == 0U);
    assert(block_device_count() == 1U);
    assert(block_device_find("unit-disk") == &device);
    assert(block_device_get(0U) == &device);
    assert(block_device_get(1U) == 0);
    assert(block_device_register(&device) == BLOCK_STATUS_BUSY);
    assert(block_device_register(&duplicate) == BLOCK_STATUS_BUSY);

    assert(block_device_read(&device, 0U, 1U, output) == BLOCK_STATUS_OK);
    assert(read_calls == 1U);
    assert(nested_unregister_status == BLOCK_STATUS_BUSY);
    assert(device.online == 1U);
    assert(device.active_operations == 0U);
    assert(block_device_write(&device, 2U, 1U, input) == BLOCK_STATUS_OK);
    assert(write_calls == 1U);
    memset(output, 0, sizeof(output));
    assert(block_device_read(&device, 2U, 1U, output) == BLOCK_STATUS_OK);
    assert(memcmp(input, output, sizeof(input)) == 0);

    assert(block_device_read(&device, 8U, 1U, output) == BLOCK_STATUS_OUT_OF_RANGE);
    assert(block_device_read(&device, 7U, 2U, output) == BLOCK_STATUS_OUT_OF_RANGE);
    assert(block_device_read(&device, 0U, 0U, output) == BLOCK_STATUS_INVALID);
    assert(block_device_read(&device, 0U, 1U, 0) == BLOCK_STATUS_INVALID);
    assert(block_device_write(&device, 8U, 1U, input) == BLOCK_STATUS_OUT_OF_RANGE);
    assert(block_device_flush(&device) == BLOCK_STATUS_OK);
    assert(flush_calls == 1U);

    assert(block_device_unregister(&device) == BLOCK_STATUS_OK);
    assert(block_device_count() == 0U);
    assert(block_device_find("unit-disk") == 0);
    assert(block_device_read(&device, 0U, 1U, output) == BLOCK_STATUS_NOT_READY);
    assert(block_device_unregister(&device) == BLOCK_STATUS_NOT_READY);

    initialize_device(&device, "read-only", &read_only_ops);
    device.flags = BLOCK_DEVICE_READ_ONLY;
    assert(block_device_register(&device) == BLOCK_STATUS_OK);
    assert(block_device_write(&device, 0U, 1U, input) == BLOCK_STATUS_READ_ONLY);
    assert(write_calls == 1U);
    assert(block_device_unregister(&device) == BLOCK_STATUS_OK);

    initialize_device(&device, "bad-sector-size", &full_ops);
    device.sector_size = 768U;
    assert(block_device_register(&device) == BLOCK_STATUS_INVALID);
    initialize_device(&device, "zero-capacity", &full_ops);
    device.sector_count = 0U;
    assert(block_device_register(&device) == BLOCK_STATUS_INVALID);

    return 0;
}
