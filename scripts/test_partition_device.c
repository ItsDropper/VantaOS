/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "block_device.h"
#include "partition_device.h"

#include <assert.h>
#include <string.h>

static block_status_t failing_read(block_device_t *device, uint64_t lba,
                                   uint32_t count, void *buffer)
{
    (void)device;
    (void)lba;
    (void)count;
    (void)buffer;
    ++read_calls;
    return BLOCK_STATUS_IO_ERROR;
}

static unsigned read_calls;

static const block_device_ops_t failing_ops = {
    failing_read, 0, 0
};

void serial_write(const char *text)
{
    (void)text;
}

int main(void)
{
    block_device_t disk;
    memset(&disk, 0, sizeof(disk));
    memcpy(disk.name, "failing-disk", sizeof("failing-disk"));
    disk.sector_count = 128U;
    disk.sector_size = 512U;
    disk.ops = &failing_ops;

    assert(block_device_register(&disk) == BLOCK_STATUS_OK);
    assert(!partition_device_has_table(&disk));

    /* A failed first scan must not become a false success on repeat. */
    assert(partition_device_scan_disk(&disk) == 0);
    unsigned calls_after_first_scan = read_calls;
    assert(calls_after_first_scan > 0U);
    assert(partition_device_scan_disk(&disk) == 0);
    assert(read_calls == calls_after_first_scan);
    assert(!partition_device_has_table(&disk));

    assert(block_device_unregister(&disk) == BLOCK_STATUS_OK);
    return 0;
}
