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

static unsigned read_calls;

static uint8_t valid_mbr[512];

static void write_u32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8U);
    out[2] = (uint8_t)(value >> 16U);
    out[3] = (uint8_t)(value >> 24U);
}

static block_status_t valid_read(block_device_t *device, uint64_t lba,
                                 uint32_t count, void *buffer)
{
    (void)device;
    if (!buffer || !count || lba >= 128U || (uint64_t)count > 128U - lba)
        return BLOCK_STATUS_OUT_OF_RANGE;
    memset(buffer, 0, (size_t)count * 512U);
    if (lba == 0U)
        memcpy(buffer, valid_mbr, sizeof(valid_mbr));
    return BLOCK_STATUS_OK;
}

static const block_device_ops_t valid_ops = {
    valid_read, 0, 0
};



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

    /* A name collision on partition 2 must roll back partition 1. */
    block_device_t disk2;
    block_device_t conflict;
    memset(&disk2, 0, sizeof(disk2));
    memset(&conflict, 0, sizeof(conflict));
    memset(valid_mbr, 0, sizeof(valid_mbr));
    valid_mbr[510] = 0x55U;
    valid_mbr[511] = 0xAAU;
    valid_mbr[446U + 4U] = 0x0CU;
    write_u32(valid_mbr + 446U + 8U, 1U);
    write_u32(valid_mbr + 446U + 12U, 10U);
    valid_mbr[462U + 4U] = 0x0CU;
    write_u32(valid_mbr + 462U + 8U, 20U);
    write_u32(valid_mbr + 462U + 12U, 10U);

    memcpy(disk2.name, "rollback", sizeof("rollback"));
    disk2.sector_count = 128U;
    disk2.sector_size = 512U;
    disk2.ops = &valid_ops;
    disk2.driver_data = valid_mbr;
    memcpy(conflict.name, "rollbackp2", sizeof("rollbackp2"));
    conflict.sector_count = 1U;
    conflict.sector_size = 512U;
    conflict.ops = &failing_ops;

    assert(block_device_register(&conflict) == BLOCK_STATUS_OK);
    assert(block_device_register(&disk2) == BLOCK_STATUS_OK);
    uint32_t count_before_scan = block_device_count();
    assert(partition_device_scan_disk(&disk2) == 0);
    assert(block_device_count() == count_before_scan);
    assert(block_device_find("rollbackp1") == 0);
    assert(block_device_find("rollbackp2") == &conflict);
    assert(partition_device_has_table(&disk2));
    assert(partition_device_scan_disk(&disk2) == 0);
    assert(block_device_count() == count_before_scan);
    assert(block_device_unregister(&disk2) == BLOCK_STATUS_OK);
    assert(block_device_unregister(&conflict) == BLOCK_STATUS_OK);
    return 0;
}
