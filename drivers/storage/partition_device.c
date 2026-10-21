/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "partition_device.h"
#include "partition_scan.h"
#include "serial.h"

#define PARTITION_DEVICE_MAX (BLOCK_DEVICE_MAX - 1U)

typedef struct {
    block_device_t block;
    block_device_t *parent;
    uint64_t start_lba;
    uint64_t length;
} partition_context_t;

typedef struct {
    block_device_t *disk;
    uint8_t has_table;
} disk_scan_state_t;

static partition_context_t partitions[PARTITION_DEVICE_MAX];
static disk_scan_state_t scan_state[BLOCK_DEVICE_MAX];
static size_t partition_count;
static size_t scan_state_count;

static int read_disk(void *context, uint64_t lba, uint32_t count, void *buffer)
{
    block_device_t *disk = (block_device_t *)context;
    return disk && disk->sector_size == 512U &&
           block_device_read(disk, lba, count, buffer) == BLOCK_STATUS_OK;
}

static block_status_t partition_read(block_device_t *device, uint64_t lba,
                                     uint32_t count, void *buffer)
{
    partition_context_t *part = (partition_context_t *)device->driver_data;
    if (!part || !part->parent || !buffer || !count ||
        lba >= part->length || (uint64_t)count > part->length - lba)
        return BLOCK_STATUS_OUT_OF_RANGE;
    return block_device_read(part->parent, part->start_lba + lba, count, buffer);
}

static block_status_t partition_write(block_device_t *device, uint64_t lba,
                                      uint32_t count, const void *buffer)
{
    partition_context_t *part = (partition_context_t *)device->driver_data;
    if (!part || !part->parent || !buffer || !count ||
        lba >= part->length || (uint64_t)count > part->length - lba)
        return BLOCK_STATUS_OUT_OF_RANGE;
    return block_device_write(part->parent, part->start_lba + lba, count, buffer);
}

static block_status_t partition_flush(block_device_t *device)
{
    partition_context_t *part = (partition_context_t *)device->driver_data;
    if (!part || !part->parent)
        return BLOCK_STATUS_INVALID;
    return block_device_flush(part->parent);
}

static const block_device_ops_t partition_ops = {
    partition_read, partition_write, partition_flush
};

static void copy_string(char *out, size_t capacity, const char *in)
{
    size_t i = 0U;
    if (!capacity) return;
    if (in) while (i + 1U < capacity && in[i]) {
        out[i] = in[i];
        ++i;
    }
    out[i] = '\0';
}

static void partition_name(char *out, size_t capacity,
                           const char *disk_name, size_t number)
{
    char reverse[10];
    size_t digits = 0U;
    while (number && digits < sizeof(reverse)) {
        reverse[digits++] = (char)('0' + number % 10U);
        number /= 10U;
    }
    size_t pos = 0U;
    while (disk_name[pos] && pos + 1U < capacity) {
        out[pos] = disk_name[pos];
        ++pos;
    }
    if (pos + 1U < capacity) out[pos++] = 'p';
    while (digits && pos + 1U < capacity) out[pos++] = reverse[--digits];
    out[pos] = '\0';
}

static int has_partition_entry(const uint8_t *mbr, uint64_t disk_sectors)
{
    if (mbr[510] != 0x55U || mbr[511] != 0xAAU)
        return 0;
    for (size_t i = 0U; i < 4U; ++i) {
        const uint8_t *entry = mbr + 446U + i * 16U;
        uint8_t type = entry[4U];
        uint64_t start = (uint64_t)entry[8U] |
            ((uint64_t)entry[9U] << 8U) |
            ((uint64_t)entry[10U] << 16U) |
            ((uint64_t)entry[11U] << 24U);
        uint64_t length = (uint64_t)entry[12U] |
            ((uint64_t)entry[13U] << 8U) |
            ((uint64_t)entry[14U] << 16U) |
            ((uint64_t)entry[15U] << 24U);
        if (type == 0xEEU && start == 1U && length != 0U)
            return 1;
        if (type != 0U && start < disk_sectors && length != 0U &&
            length <= disk_sectors - start)
            return 1;
    }
    return 0;
}

int partition_device_has_table(const block_device_t *disk)
{
    for (size_t i = 0U; i < scan_state_count; ++i)
        if (scan_state[i].disk == disk)
            return scan_state[i].has_table != 0U;
    return 0;
}

static void record_scan(block_device_t *disk, int has_table)
{
    if (scan_state_count >= BLOCK_DEVICE_MAX)
        return;
    scan_state[scan_state_count].disk = disk;
    scan_state[scan_state_count].has_table = has_table ? 1U : 0U;
    ++scan_state_count;
}

void partition_device_initialize(void)
{
    size_t disks_to_scan = block_device_count();
    for (size_t index = 0U; index < disks_to_scan; ++index) {
        block_device_t *disk = block_device_get((uint32_t)index);
        if (!disk || (disk->flags & BLOCK_DEVICE_PARTITION) ||
            disk->sector_size != 512U || !disk->sector_count)
            continue;

        uint8_t first_sector[512];
        int has_table = read_disk(disk, 0U, 1U, first_sector) &&
                        has_partition_entry(first_sector, disk->sector_count);
        record_scan(disk, has_table);

        partition_scan_entry_t found[PARTITION_SCAN_MAX];
        size_t found_count = 0U;
        partition_scan_status_t status = partition_scan(read_disk, disk,
            disk->sector_count, found, PARTITION_SCAN_MAX, &found_count);
        if (status != PARTITION_SCAN_OK) {
            if (has_table)
                serial_write("VANTAOS_PARTITION_FAIL\n");
            continue;
        }

        size_t registered_before = partition_count;
        for (size_t i = 0U; i < found_count &&
             partition_count < PARTITION_DEVICE_MAX; ++i) {
            partition_context_t *part = &partitions[partition_count];
            *part = (partition_context_t){0};
            part->parent = disk;
            part->start_lba = found[i].start_lba;
            part->length = found[i].sector_count;
            partition_name(part->block.name, sizeof(part->block.name),
                           disk->name, i + 1U);
            part->block.sector_count = found[i].sector_count;
            part->block.sector_size = disk->sector_size;
            part->block.flags = (disk->flags & (BLOCK_DEVICE_READ_ONLY | BLOCK_DEVICE_REMOVABLE |
                                BLOCK_DEVICE_TRANSPORT_ATA |
                                BLOCK_DEVICE_TRANSPORT_NVME)) |
                                BLOCK_DEVICE_PARTITION;
            part->block.ops = &partition_ops;
            part->block.driver_data = part;
            part->block.partition_scheme = found[i].scheme;
            part->block.partition_type = found[i].mbr_type;
            for (size_t j = 0U; j < 16U; ++j)
                part->block.partition_type_guid[j] = found[i].type_guid[j];
            copy_string(part->block.partition_label,
                        sizeof(part->block.partition_label), found[i].name);
            if (block_device_register(&part->block) == BLOCK_STATUS_OK) {
                ++partition_count;
                serial_write("VANTAOS_PARTITION_OK\n");
             else {
                serial_write("VANTAOS_PARTITION_FAIL\n");
            }
        }
    }
    serial_write("VANTAOS_PARTITION_SCAN_DONE\n");
}
