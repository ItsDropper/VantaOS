/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "fat32.h"
#include "fat32_lfn.h"
#include "block_device.h"
#include "partition_device.h"
#include "serial.h"
#ifdef VANTAOS_SELF_TEST
#define FAT32_RENAME_DIAG(message) serial_write(message "\n")
#else
#define FAT32_RENAME_DIAG(message) ((void)0)
#endif

#define FAT32_MAX_CLUSTER 0x0FFFFFF7U
#define FAT32_EOC_MIN 0x0FFFFFF8U
#define FAT32_BAD_CLUSTER 0xFFFFFFFEU
#define FAT32_INVALID_LBA 0xFFFFFFFFU

static int mounted;
static block_device_t* active_device;
static uint32_t partition_lba;
static uint32_t fat_lba;
static uint32_t data_lba;
static uint32_t sectors_per_fat;
static uint32_t sectors_per_cluster;
static uint32_t total_sectors;
static uint32_t cluster_count;
static uint32_t root_cluster;
static uint16_t bytes_per_sector;
static uint8_t fat_count;

static uint8_t sector_buffer[512];
static uint8_t boot_sector_buffer[512];

static uint16_t fat16(const uint8_t* p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t fat32(const uint8_t* p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int read_sector(uint32_t lba, uint8_t* buffer)
{
    if (!active_device || !buffer || active_device->sector_size != 512U ||
        (uint64_t)lba >= active_device->sector_count)
        return 0;
    return block_device_read(active_device, lba, 1U, buffer) == BLOCK_STATUS_OK;
}

static int valid_boot_sector(const uint8_t* b)
{
    if (b[510] != 0x55 || b[511] != 0xAA)
        return 0;

    /*
     * Do not depend on the optional filesystem-type text at offset 82.
     * FAT32 identifies itself through the BPB geometry and FAT32-specific
     * fields; some formatters leave the text field blank or use different
     * wording.
     */
    uint16_t bytes = fat16(&b[11]);
    uint8_t sectors = b[13];
    uint16_t reserved = fat16(&b[14]);
    uint8_t fats = b[16];
    uint32_t sectors_per_fat = fat32(&b[36]);
    uint32_t total32 = fat32(&b[32]);
    uint16_t total16 = fat16(&b[19]);
    uint32_t total = total32 ? total32 : total16;

    return bytes == 512 &&
           sectors != 0 &&
           (sectors & (sectors - 1U)) == 0 &&
           reserved != 0 &&
           fats != 0 &&
           sectors_per_fat != 0 &&
           total != 0;
}

static int mount_partition(uint32_t lba, uint32_t partition_sectors)
{
    mounted = 0;

    if (!active_device || active_device->sector_size != 512U ||
        (uint64_t)lba >= active_device->sector_count ||
        (partition_sectors &&
         ((uint64_t)partition_sectors > active_device->sector_count - lba)) ||
        !read_sector(lba, boot_sector_buffer))
        return 0;

    const uint8_t* boot = boot_sector_buffer;
    if (!valid_boot_sector(boot))
        return 0;

    uint16_t bps = fat16(&boot[11]);
    uint32_t spc = boot[13];
    uint32_t reserved = fat16(&boot[14]);
    uint32_t fats = boot[16];
    uint32_t spf = fat32(&boot[36]);
    uint32_t root = fat32(&boot[44]);
    uint32_t total = fat32(&boot[32]);
    if (total == 0)
        total = fat16(&boot[19]);

    if (bps != 512 || spc == 0 || (spc & (spc - 1U)) != 0 ||
        reserved == 0 || fats == 0 || fats > 2 || spf == 0 ||
        total == 0 || root < 2 || root >= FAT32_MAX_CLUSTER ||
        (partition_sectors && total > partition_sectors) ||
        (uint64_t)total > active_device->sector_count - lba ||
        (uint64_t)lba + total > (uint64_t)UINT32_MAX + 1U)
        return 0;

    /* Wide intermediates prevent corrupt BPB values wrapping geometry. */
    uint64_t fat_area = (uint64_t)fats * spf;
    uint64_t first_data = (uint64_t)reserved + fat_area;
    if (first_data >= total)
        return 0;

    uint32_t data_sectors = total - (uint32_t)first_data;
    uint32_t clusters = data_sectors / spc;
    if (clusters == 0 || clusters > FAT32_MAX_CLUSTER - 2U ||
        root > clusters + 1U)
        return 0;

    /* Every data cluster must have a corresponding FAT entry. */
    if (((uint64_t)clusters + 2U) * 4U > (uint64_t)spf * 512U)
        return 0;

    partition_lba = lba;
    fat_lba = lba + reserved;
    data_lba = lba + (uint32_t)first_data;
    bytes_per_sector = bps;
    sectors_per_cluster = spc;
    fat_count = (uint8_t)fats;
    sectors_per_fat = spf;
    total_sectors = total;
    cluster_count = clusters;
    root_cluster = root;
    mounted = 1;
    return 1;
}


static uint32_t cluster_lba(uint32_t cluster)
{
    if (!mounted || cluster < 2 || cluster > cluster_count + 1U)
        return FAT32_INVALID_LBA;

    uint64_t lba = (uint64_t)data_lba +
        (uint64_t)(cluster - 2U) * sectors_per_cluster;
    if (lba + sectors_per_cluster > (uint64_t)partition_lba + total_sectors ||
        lba + sectors_per_cluster > active_device->sector_count ||
        lba + sectors_per_cluster > (uint64_t)UINT32_MAX + 1U)
        return FAT32_INVALID_LBA;

    return (uint32_t)lba;
}

static uint32_t next_cluster(uint32_t cluster)
{
    if (!mounted || cluster < 2 || cluster > cluster_count + 1U)
        return FAT32_BAD_CLUSTER;

    uint64_t fat_offset = (uint64_t)cluster * 4U;
    if (fat_offset + 4U > (uint64_t)sectors_per_fat * 512U)
        return FAT32_BAD_CLUSTER;

    uint32_t lba = fat_lba + (uint32_t)(fat_offset / 512U);
    uint32_t offset = (uint32_t)(fat_offset % 512U);
    if (!read_sector(lba, sector_buffer))
        return FAT32_BAD_CLUSTER;

    uint32_t next = fat32(&sector_buffer[offset]) & 0x0FFFFFFFU;
    if (next >= FAT32_EOC_MIN)
        return FAT32_MAX_CLUSTER;
    if (next == 0x0FFFFFF7U || next < 2 || next > cluster_count + 1U)
        return FAT32_BAD_CLUSTER;
    return next;
}

static void short_name(
    const uint8_t* entry,
    char* name
)
{
    unsigned int pos = 0;

    for (unsigned int i = 0; i < 8; i++)
    {
        if (entry[i] == ' ')
            break;

        name[pos++] = (char)entry[i];
    }

    int has_extension = 0;

    for (unsigned int i = 8; i < 11; i++)
    {
        if (entry[i] != ' ')
        {
            has_extension = 1;
            break;
        }
    }

    if (has_extension)
    {
        name[pos++] = '.';

        for (unsigned int i = 8; i < 11; i++)
        {
            if (entry[i] == ' ')
                break;

            name[pos++] = (char)entry[i];
        }
    }

    name[pos] = 0;
}


static int fat32_write_sector(uint32_t lba, const uint8_t* data)
{
    if (!active_device || !data || active_device->sector_size != 512U ||
        (uint64_t)lba >= active_device->sector_count)
        return 0;
    if (block_device_write(active_device, lba, 1U, data) != BLOCK_STATUS_OK)
        return 0;
    /* Keep each metadata/data sector durable before a caller reports success.
       This is conservative until FAT32 gains an ordered transaction layer. */
    return block_device_flush(active_device) == BLOCK_STATUS_OK;
}

static int fat32_raw_entry(uint32_t cluster, uint32_t* value)
{
    if (cluster < 2 || cluster > cluster_count + 1U || !value)
        return 0;
    uint32_t offset = cluster * 4U;
    if ((uint64_t)offset + 4U > (uint64_t)sectors_per_fat * 512U)
        return 0;

    uint32_t expected = 0;
    for (uint32_t copy = 0; copy < fat_count; copy++)
    {
        uint32_t lba = fat_lba + copy * sectors_per_fat + offset / 512U;
        if (!read_sector(lba, sector_buffer))
            return 0;
        uint32_t entry = fat32(&sector_buffer[offset % 512U]) & 0x0FFFFFFFU;
        /* Never allocate through a mirrored FAT that disagrees about ownership. */
        if (copy != 0 && entry != expected)
            return 0;
        expected = entry;
    }

    *value = expected;
    return 1;
}

static int fat32_set_raw_entry(uint32_t cluster, uint32_t value)
{
    if (cluster < 2 || cluster > cluster_count + 1U || value > FAT32_EOC_MIN)
        return 0;
    uint32_t offset = cluster * 4U;
    for (uint32_t copy = 0; copy < fat_count; copy++)
    {
        uint32_t lba = fat_lba + copy * sectors_per_fat + offset / 512U;
        uint32_t at = offset % 512U;
        if (!read_sector(lba, sector_buffer))
            return 0;
        uint32_t old = fat32(&sector_buffer[at]);
        uint32_t v = (old & 0xF0000000U) | value;
        sector_buffer[at] = (uint8_t)v;
        sector_buffer[at + 1] = (uint8_t)(v >> 8);
        sector_buffer[at + 2] = (uint8_t)(v >> 16);
        sector_buffer[at + 3] = (uint8_t)(v >> 24);
        if (!fat32_write_sector(lba, sector_buffer))
            return 0;
    }
    return 1;
}

static uint32_t fat32_allocate_one(void)
{
    static const uint8_t zero[512] = {0};
    for (uint32_t cluster = 2; cluster <= cluster_count + 1U; cluster++)
    {
        uint32_t value;
        if (!fat32_raw_entry(cluster, &value))
            return 0;
        if (value != 0)
            continue;
        if (!fat32_set_raw_entry(cluster, FAT32_EOC_MIN))
            return 0;
        uint32_t base = cluster_lba(cluster);
        if (base == FAT32_INVALID_LBA)
        {
            (void)fat32_set_raw_entry(cluster, 0);
            return 0;
        }
        for (uint32_t s = 0; s < sectors_per_cluster; s++)
            if (!fat32_write_sector(base + s, zero))
            {
                (void)fat32_set_raw_entry(cluster, 0);
                return 0;
            }
        return cluster;
    }
    return 0;
}

static void fat32_release_chain(uint32_t cluster)
{
    for (uint32_t i = 0; i < cluster_count &&
         cluster >= 2 && cluster <= cluster_count + 1U; i++)
    {
        uint32_t next;
        if (!fat32_raw_entry(cluster, &next) ||
            !fat32_set_raw_entry(cluster, 0))
            return;
        if (next >= FAT32_EOC_MIN || next < 2 ||
            next > cluster_count + 1U || next == 0x0FFFFFF7U)
            return;
        cluster = next;
    }
}

static int fat32_make_short_name(const char* input, uint8_t out[11])
{
    unsigned int base = 0, ext = 0;
    int dot = 0;
    for (unsigned int i = 0; i < 11; i++)
        out[i] = ' ';
    if (!input || !*input)
        return 0;

    for (unsigned int i = 0; input[i]; i++)
    {
        char ch = input[i];
        if (ch == '.')
        {
            if (dot || !base)
                return 0;
            dot = 1;
            continue;
        }
        if (ch >= 'a' && ch <= 'z')
            ch = (char)(ch - 'a' + 'A');

        /* Conservative ASCII subset; reject unsupported names rather than alias them. */
        if (!((ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') ||
              ch == '_' || ch == '-' || ch == '~'))
            return 0;

        if (!dot)
        {
            if (base == 8)
                return 0;
            out[base++] = (uint8_t)ch;
        }
        else
        {
            if (ext == 3)
                return 0;
            out[8U + ext++] = (uint8_t)ch;
        }
    }
    return base != 0;
}

void fat32_initialize(void)
{
    mounted = 0;
    active_device = 0;

    /* Never pick a disk by registration order. Probe every writable,
       512-byte-sector device and mount only when exactly one contains a
       valid FAT32 volume. Multiple valid candidates are intentionally
       ambiguous until an explicit boot-volume policy exists. */
    block_device_t* selected = 0;
    uint32_t count = block_device_count();
    for (uint32_t i = 0; i < count; ++i)
    {
        block_device_t* candidate = block_device_get(i);
        if (!candidate || candidate->sector_size != 512U ||
            candidate->sector_count == 0U ||
            (candidate->flags & BLOCK_DEVICE_READ_ONLY) != 0U)
            continue;

        if ((candidate->flags & BLOCK_DEVICE_PARTITION) == 0U &&
            partition_device_has_table(candidate))
            continue;

        active_device = candidate;
        mounted = 0;
        if (!mount_partition(0U, 0U))
            continue;

        if (selected)
        {
            mounted = 0;
            active_device = 0;
            serial_write("VANTAOS_FAT32_AMBIGUOUS_DEVICES\n");
            return;
        }
        selected = candidate;
    }

    if (!selected)
    {
        mounted = 0;
        active_device = 0;
        serial_write("VANTAOS_FAT32_NO_USABLE_VOLUME\n");
        return;
    }

    active_device = selected;
    mounted = 0;
    if (!mount_partition(0U, 0U))
    {
        mounted = 0;
        active_device = 0;
        serial_write("VANTAOS_FAT32_SELECTED_VOLUME_MOUNT_FAIL\n");
        return;
    }
#ifdef VANTAOS_SELF_TEST
    if (active_device->flags & BLOCK_DEVICE_TRANSPORT_NVME)
        serial_write("VANTAOS_FAT32_BACKEND_NVME_OK\\n");
    else if (active_device->flags & BLOCK_DEVICE_TRANSPORT_ATA)
        serial_write("VANTAOS_FAT32_BACKEND_ATA_OK\\n");
    serial_write("VANTAOS_FAT32_BLOCK_DEVICE_MOUNT_OK\\n");
#endif
}

int fat32_is_mounted(void)
{
    return mounted;
}

uint32_t fat32_root_cluster(void)
{
    return mounted ? root_cluster : 0;
}

int fat32_list_directory(
    uint32_t cluster,
    fat32_dirent_t* entries,
    unsigned int capacity
)
{
    if (!mounted || !entries || capacity == 0 ||
        cluster < 2 || cluster > cluster_count + 1U)
        return -1;

    unsigned int count = 0;
    fat32_lfn_state_t lfn;
    fat32_lfn_reset(&lfn);

    for (uint32_t steps = 0;
         steps < cluster_count && cluster >= 2 && cluster <= cluster_count + 1U;
         steps++)
    {
        uint32_t base = cluster_lba(cluster);
        if (base == FAT32_INVALID_LBA)
            return -1;
        for (uint32_t sector = 0; sector < sectors_per_cluster; sector++)
        {
            if (!read_sector(base + sector, sector_buffer))
                return -1;

            for (unsigned int offset = 0; offset < 512; offset += 32)
            {
                const uint8_t* entry = &sector_buffer[offset];
                if (entry[0] == 0x00)
                    return (int)count;

                if (entry[0] == 0xE5)
                {
                    fat32_lfn_reset(&lfn);
                    continue;
                }

                if (entry[11] == 0x0F)
                {
                    fat32_lfn_consume(entry, &lfn);
                    continue;
                }

                if ((entry[11] & 0x08U) != 0)
                {
                    fat32_lfn_reset(&lfn);
                    continue;
                }

                if (count >= capacity)
                    return (int)count;

                char short_buffer[256];
                short_name(entry, short_buffer);
                int use_long_name =
                    lfn.active &&
                    fat32_lfn_is_complete(&lfn) &&
                    lfn.expected_checksum == fat32_lfn_checksum(entry) &&
                    fat32_lfn_decode(&lfn, entries[count].name);

                if (!use_long_name)
                {
                    unsigned int i = 0;
                    while (short_buffer[i] && i < 255U)
                    {
                        entries[count].name[i] = short_buffer[i];
                        i++;
                    }
                    entries[count].name[i] = 0;
                }

                fat32_lfn_reset(&lfn);

                uint32_t high = (uint32_t)fat16(&entry[20]);
                uint32_t low = (uint32_t)fat16(&entry[26]);
                entries[count].first_cluster = (high << 16) | low;
                entries[count].size = fat32(&entry[28]);
                entries[count].directory = (entry[11] & 0x10U) != 0;

                if (entries[count].name[0] != 0 &&
                    entries[count].name[0] != '.')
                    count++;
            }
        }
        cluster = next_cluster(cluster);
        if (cluster == FAT32_BAD_CLUSTER)
            return -1;
        if (cluster == FAT32_MAX_CLUSTER)
            return (int)count;
    }

    /* A still-valid cluster after the volume-derived step limit is a cycle. */
    if (cluster >= 2 && cluster <= cluster_count + 1U)
        return -1;
    return (int)count;
}

int fat32_read_file(
    uint32_t first_cluster,
    uint32_t size,
    char* buffer,
    unsigned int capacity
)
{
    if (!mounted || !buffer || capacity == 0)
        return -1;

    if (size > capacity - 1)
        size = capacity - 1;

    if (size == 0)
    {
        buffer[0] = 0;
        return 0;
    }

    if (first_cluster < 2)
        return -1;

    uint32_t remaining = size;
    unsigned int written = 0;
    uint32_t cluster = first_cluster;

    for (uint32_t steps = 0;
         remaining && steps < cluster_count &&
         cluster >= 2 && cluster <= cluster_count + 1U;
         steps++)
    {
        uint32_t base = cluster_lba(cluster);
        if (base == FAT32_INVALID_LBA)
            return -1;

        for (uint32_t sector = 0;
             sector < sectors_per_cluster && remaining;
             sector++)
        {
            if (!read_sector(base + sector, sector_buffer))
                return -1;

            uint32_t amount =
                remaining > 512 ? 512 : remaining;

            for (uint32_t i = 0; i < amount; i++)
                buffer[written++] = (char)sector_buffer[i];

            remaining -= amount;
        }

        if (!remaining)
            break;

        cluster = next_cluster(cluster);
        if (remaining &&
            (cluster == FAT32_MAX_CLUSTER || cluster == FAT32_BAD_CLUSTER))
            return -1;
    }

    if (remaining)
        return -1;

    buffer[written] = 0;
    return (int)written;
}


/* Read a byte range without allocating or staging the complete file. */
int fat32_read_file_range(uint32_t first_cluster, uint32_t size,
                          uint32_t offset, char* buffer, uint32_t length)
{
    if (!mounted || (length && !buffer) || length > 0x7FFFFFFFU)
        return -1;
    if (offset >= size || length == 0U)
        return 0;
    if (first_cluster < 2U)
        return -1;

    uint32_t available = size - offset;
    if (length > available)
        length = available;

    uint64_t cluster_bytes = (uint64_t)sectors_per_cluster * 512U;
    uint32_t target_index = (uint32_t)((uint64_t)offset / cluster_bytes);
    uint32_t cluster = first_cluster;
    for (uint32_t index = 0; index < target_index; index++)
    {
        cluster = next_cluster(cluster);
        if (cluster == FAT32_MAX_CLUSTER || cluster == FAT32_BAD_CLUSTER)
            return -1;
    }

    uint32_t copied = 0U;
    uint32_t in_cluster = (uint32_t)((uint64_t)offset % cluster_bytes);
    uint32_t steps = target_index;
    while (copied < length && steps < cluster_count)
    {
        uint32_t base = cluster_lba(cluster);
        if (base == FAT32_INVALID_LBA)
            return -1;

        while (in_cluster < sectors_per_cluster * 512U && copied < length)
        {
            uint32_t sector_index = in_cluster / 512U;
            uint32_t in_sector = in_cluster % 512U;
            if (!read_sector(base + sector_index, sector_buffer))
                return -1;

            uint32_t amount = 512U - in_sector;
            if (amount > length - copied)
                amount = length - copied;
            for (uint32_t i = 0; i < amount; i++)
                buffer[copied + i] = (char)sector_buffer[in_sector + i];
            copied += amount;
            in_cluster += amount;
        }

        if (copied == length)
            break;
        cluster = next_cluster(cluster);
        if (cluster == FAT32_MAX_CLUSTER || cluster == FAT32_BAD_CLUSTER)
            return -1;
        in_cluster = 0U;
        steps++;
    }
    return copied == length ? (int)copied : -1;
}



typedef struct
{
    int found;
    int free_found;
    uint32_t entry_lba, entry_offset, free_lba, free_offset, last_cluster;
    uint32_t first_cluster, size;
    uint8_t attributes;
} fat32_slot_t;

static int fat32_find_entry(uint32_t dir, const uint8_t* wanted, fat32_slot_t* out)
{
    if (!out || dir < 2 || dir > cluster_count + 1U)
        return 0;

    out->found = 0;
    out->free_found = 0;
    uint32_t cluster = dir;

    for (uint32_t steps = 0; steps < cluster_count; steps++)
    {
        uint32_t base = cluster_lba(cluster);
        if (base == FAT32_INVALID_LBA)
            return 0;
        out->last_cluster = cluster;

        for (uint32_t sector = 0; sector < sectors_per_cluster; sector++)
        {
            uint32_t lba = base + sector;
            if (!read_sector(lba, sector_buffer))
                return 0;

            for (uint32_t offset = 0; offset < 512; offset += 32)
            {
                const uint8_t* entry = &sector_buffer[offset];
                if (entry[0] == 0 || entry[0] == 0xE5)
                {
                    if (!out->free_found)
                    {
                        out->free_found = 1;
                        out->free_lba = lba;
                        out->free_offset = offset;
                    }
                    if (entry[0] == 0)
                        return 1;
                    continue;
                }

                if (!wanted || entry[11] == 0x0F || (entry[11] & 0x08U))
                    continue;

                unsigned int i;
                for (i = 0; i < 11 && entry[i] == wanted[i]; i++) {}
                if (i == 11)
                {
                    out->found = 1;
                    out->entry_lba = lba;
                    out->entry_offset = offset;
                    out->attributes = entry[11];
                    out->first_cluster =
                        ((uint32_t)fat16(&entry[20]) << 16) | fat16(&entry[26]);
                    out->size = fat32(&entry[28]);
                    return 1;
                }
            }
        }

        uint32_t next;
        if (!fat32_raw_entry(cluster, &next))
            return 0;
        if (next >= FAT32_EOC_MIN)
            return 1;
        if (next == 0x0FFFFFF7U || next < 2 || next > cluster_count + 1U)
            return 0;
        cluster = next;
    }
    return 0;
}

static char fat32_fold_name(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static int fat32_names_equal(const char* left, const char* right)
{
    unsigned int i = 0;
    if (!left || !right)
        return 0;
    while (left[i] && right[i])
    {
        if (fat32_fold_name(left[i]) != fat32_fold_name(right[i]))
            return 0;
        i++;
    }
    return left[i] == right[i];
}

static void fat32_slot_from_entry(fat32_slot_t* out, uint32_t lba,
                                  uint32_t offset, const uint8_t* entry)
{
    out->found = 1;
    out->entry_lba = lba;
    out->entry_offset = offset;
    out->attributes = entry[11];
    out->first_cluster = ((uint32_t)fat16(&entry[20]) << 16) |
                         fat16(&entry[26]);
    out->size = fat32(&entry[28]);
}

static int fat32_find_component(uint32_t directory, const char* name,
                                fat32_slot_t* out, uint8_t alias[11])
{
    if (!name || !out || !alias ||
        directory < 2U || directory > cluster_count + 1U)
        return 0;
    uint8_t wanted[11];
    int has_short = fat32_make_short_name(name, wanted);
    uint32_t cluster = directory;
    fat32_lfn_state_t lfn;
    fat32_lfn_reset(&lfn);
    out->found = 0;
    out->free_found = 0;
    for (uint32_t steps = 0; steps < cluster_count; steps++)
    {
        uint32_t base = cluster_lba(cluster);
        if (base == FAT32_INVALID_LBA)
            return 0;
        out->last_cluster = cluster;
        for (uint32_t sector = 0; sector < sectors_per_cluster; sector++)
        {
            uint32_t lba = base + sector;
            if (!read_sector(lba, sector_buffer))
                return 0;
            for (uint32_t offset = 0; offset < 512U; offset += 32U)
            {
                const uint8_t* entry = &sector_buffer[offset];
                if (entry[0] == 0U)
                    return 0;
                if (entry[0] == 0xE5U)
                {
                    fat32_lfn_reset(&lfn);
                    continue;
                }
                if (entry[11] == 0x0FU)
                {
                    (void)fat32_lfn_consume(entry, &lfn);
                    continue;
                }
                if ((entry[11] & 0x08U) != 0U)
                {
                    fat32_lfn_reset(&lfn);
                    continue;
                }
                char long_name[256];
                int has_long = lfn.active && fat32_lfn_is_complete(&lfn) &&
                    lfn.expected_checksum == fat32_lfn_checksum(entry) &&
                    fat32_lfn_decode(&lfn, long_name);
                char display_name[256];
                short_name(entry, display_name);
                int match_long = has_long && fat32_names_equal(long_name, name);
                int match_short = has_short &&
                    fat32_names_equal(display_name, name);
                if (match_long || match_short)
                {
                    fat32_slot_from_entry(out, lba, offset, entry);
                    for (unsigned int i = 0; i < 11U; i++)
                        alias[i] = entry[i];
                    return 1;
                }
                fat32_lfn_reset(&lfn);
            }
        }
        uint32_t next;
        if (!fat32_raw_entry(cluster, &next))
            return 0;
        if (next >= FAT32_EOC_MIN)
            return 0;
        if (next == FAT32_BAD_CLUSTER || next < 2U ||
            next > cluster_count + 1U)
            return 0;
        cluster = next;
        fat32_lfn_reset(&lfn);
    }
    return 0;
}

static int fat32_make_alias(uint32_t directory, const char* name,
                            uint8_t alias[11])
{
    char base[7] = {0};
    char ext[4] = {0};
    unsigned int base_len = 0U, ext_len = 0U;
    const char* dot = 0;
    unsigned int length = 0U;
    while (name[length])
    {
        if (length >= 255U)
            return 0;
        if (name[length] == '.')
            dot = name + length;
        length++;
    }
    if (length == 0U || name[0] == '.' ||
        (length == 1U && name[0] == '.') ||
        (length == 2U && name[0] == '.' && name[1] == '.'))
        return 0;
    unsigned int raw_base = dot ? (unsigned int)(dot - name) : length;
    for (unsigned int i = 0; i < raw_base && base_len < 6U; i++)
    {
        char ch = name[i];
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
        if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
            ch == '_' || ch == '-')
            base[base_len++] = ch;
    }
    if (base_len == 0U)
    {
        base[0] = 'F'; base[1] = 'I'; base[2] = 'L'; base[3] = 'E';
        base_len = 4U;
    }
    if (dot)
    {
        const char* p = dot + 1;
        while (*p && ext_len < 3U)
        {
            char ch = *p++;
            if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
            if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'))
                ext[ext_len++] = ch;
        }
    }
    for (unsigned int attempt = 1U; attempt < 1000U; attempt++)
    {
        for (unsigned int i = 0; i < 11U; i++) alias[i] = ' ';
        unsigned int digits = attempt < 10U ? 1U :
                              attempt < 100U ? 2U : 3U;
        unsigned int prefix = 7U - digits;
        if (prefix > base_len) prefix = base_len;
        for (unsigned int i = 0; i < prefix; i++) alias[i] = (uint8_t)base[i];
        alias[prefix++] = '~';
        unsigned int value = attempt;
        for (unsigned int i = 0; i < digits; i++)
        {
            unsigned int pos = prefix + digits - i - 1U;
            alias[pos] = (uint8_t)('0' + value % 10U);
            value /= 10U;
        }
        for (unsigned int i = 0; i < ext_len; i++) alias[8U + i] = (uint8_t)ext[i];
        fat32_slot_t existing;
        if (!fat32_find_entry(directory, alias, &existing) || !existing.found)
            return 1;
    }
    return 0;
}

static int fat32_parent_and_leaf(const char* path, uint32_t* parent, uint8_t leaf[11])
{
    if (!path || path[0] != '/' || !parent || !leaf)
        return 0;
    const char* cursor = path;
    while (*cursor == '/') cursor++;
    uint32_t directory = root_cluster;
    for (;;)
    {
        char component[256];
        unsigned int length = 0U;
        while (cursor[length] && cursor[length] != '/')
        {
            if (length >= sizeof(component) - 1U)
                return 0;
            component[length] = cursor[length];
            length++;
        }
        if (length == 0U)
            return 0;
        component[length] = 0;
        cursor += length;
        while (*cursor == '/') cursor++;

        uint8_t short_alias[11];
        int valid_short = fat32_make_short_name(component, short_alias);
        if (!*cursor)
        {
            if (valid_short)
            {
                fat32_slot_t short_entry;
                if (fat32_find_entry(directory, short_alias, &short_entry) &&
                    short_entry.found)
                {
                    for (unsigned int i = 0; i < 11U; i++)
                        leaf[i] = short_alias[i];
                    *parent = directory;
                    return 1;
                }
            }
            fat32_slot_t existing;
            if (fat32_find_component(directory, component, &existing, leaf))
            {
                *parent = directory;
                return 1;
            }
            if (valid_short)
            {
                for (unsigned int i = 0; i < 11U; i++)
                    leaf[i] = short_alias[i];
                *parent = directory;
                return 1;
            }
            if (fat32_make_alias(directory, component, leaf))
            {
                *parent = directory;
                return 1;
            }
            return 0;
        }

        fat32_slot_t found;
        uint8_t found_alias[11];
        if (valid_short &&
            fat32_find_entry(directory, short_alias, &found) && found.found)
        {
            if (!(found.attributes & 0x10U) ||
                found.first_cluster < 2U ||
                found.first_cluster > cluster_count + 1U)
                return 0;
            directory = found.first_cluster;
            continue;
        }
        if (!fat32_find_component(directory, component, &found, found_alias) ||
            !(found.attributes & 0x10U) ||
            found.first_cluster < 2U ||
            found.first_cluster > cluster_count + 1U)
            return 0;
        directory = found.first_cluster;
    }
}

static int fat32_write_entry(uint32_t lba, uint32_t offset,
                             const uint8_t name[11], uint8_t attributes,
                             uint32_t cluster, uint32_t size)
{
    if (offset > 480U || (offset & 31U))
        return 0;
    if (!read_sector(lba, sector_buffer))
        return 0;

    uint8_t* entry = &sector_buffer[offset];
    for (unsigned int i = 0; i < 11; i++)
        entry[i] = name[i];
    entry[11] = attributes;
    entry[12] = entry[13] = 0;
    for (unsigned int i = 14; i < 20; i++)
        entry[i] = 0;
    entry[20] = (uint8_t)(cluster >> 16);
    entry[21] = (uint8_t)(cluster >> 24);
    entry[26] = (uint8_t)cluster;
    entry[27] = (uint8_t)(cluster >> 8);
    entry[28] = (uint8_t)size;
    entry[29] = (uint8_t)(size >> 8);
    entry[30] = (uint8_t)(size >> 16);
    entry[31] = (uint8_t)(size >> 24);
    return fat32_write_sector(lba, sector_buffer);
}

static int fat32_reserve_named_slots(uint32_t directory, unsigned int needed,
                                       uint32_t lbas[21], uint32_t offsets[21])
{
    if (needed == 0U || needed > 21U)
        return 0;
    uint32_t cluster = directory;
    uint32_t last_cluster = directory;
    uint32_t run = 0U;
    int end_seen = 0;
    for (uint32_t steps = 0; steps < cluster_count; steps++)
    {
        uint32_t base = cluster_lba(cluster);
        if (base == FAT32_INVALID_LBA)
            return 0;
        last_cluster = cluster;
        for (uint32_t sector = 0; sector < sectors_per_cluster; sector++)
        {
            uint32_t lba = base + sector;
            if (!read_sector(lba, sector_buffer))
                return 0;
            for (uint32_t offset = 0; offset < 512U; offset += 32U)
            {
                const uint8_t* entry = &sector_buffer[offset];
                if (entry[0] == 0U)
                    end_seen = 1;
                int free_slot = end_seen || entry[0] == 0xE5U;
                if (free_slot)
                {
                    if (run < needed)
                    {
                        if (run == 0U)
                        {
                            lbas[0] = lba;
                            offsets[0] = offset;
                        }
                        else
                        {
                            lbas[run] = lba;
                            offsets[run] = offset;
                        }
                    }
                    run++;
                    if (run >= needed)
                        return 1;
                }
                else
                    run = 0U;
            }
        }
        uint32_t next;
        if (!fat32_raw_entry(cluster, &next))
            return 0;
        if (next >= FAT32_EOC_MIN)
        {
            uint32_t allocated = fat32_allocate_one();
            if (!allocated || !fat32_set_raw_entry(last_cluster, allocated))
            {
                if (allocated) fat32_release_chain(allocated);
                return 0;
            }
            cluster = allocated;
            end_seen = 1;
        }
        else if (next == FAT32_BAD_CLUSTER || next < 2U ||
                 next > cluster_count + 1U)
            return 0;
        else
            cluster = next;
    }
    return 0;
}

static int fat32_leaf_name(const char* path, char name[256])
{
    if (!path || !name)
        return 0;
    unsigned int length = 0U;
    while (path[length]) length++;
    while (length > 0U && path[length - 1U] == '/') length--;
    unsigned int start = length;
    while (start > 0U && path[start - 1U] != '/') start--;
    unsigned int leaf_length = length - start;
    if (leaf_length == 0U || leaf_length > 255U)
        return 0;
    for (unsigned int i = 0; i < leaf_length; i++)
    {
        unsigned char ch = (unsigned char)path[start + i];
        if (ch < 32U || ch >= 127U || ch == ':' || ch == '*' ||
            ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|')
            return 0;
        name[i] = (char)ch;
    }
    name[leaf_length] = 0;
    return 1;
}

static int fat32_name_matches_alias(const char* name, const uint8_t alias[11])
{
    char display[13];
    unsigned int used = 0U;
    unsigned int base_end = 8U;
    while (base_end > 0U && alias[base_end - 1U] == ' ') base_end--;
    for (unsigned int i = 0; i < base_end; i++) display[used++] = (char)alias[i];
    unsigned int ext_end = 11U;
    while (ext_end > 8U && alias[ext_end - 1U] == ' ') ext_end--;
    if (ext_end > 8U)
    {
        display[used++] = '.';
        for (unsigned int i = 8U; i < ext_end; i++) display[used++] = (char)alias[i];
    }
    display[used] = 0;
    return fat32_names_equal(name, display);
}

static int fat32_write_lfn_entry(uint32_t lba, uint32_t offset,
                                 const uint16_t chars[260], unsigned int ordinal,
                                 unsigned int total, uint8_t checksum)
{
    static const uint8_t char_offsets[13] = {
        1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30
    };
    if (offset > 480U || (offset & 31U) || ordinal == 0U ||
        ordinal > total || total > 20U || !read_sector(lba, sector_buffer))
        return 0;
    uint8_t* entry = &sector_buffer[offset];
    for (unsigned int i = 0; i < 32U; i++) entry[i] = 0U;
    entry[0] = (uint8_t)(ordinal | (ordinal == total ? 0x40U : 0U));
    entry[11] = 0x0FU;
    entry[12] = 0U;
    entry[13] = checksum;
    entry[26] = entry[27] = 0U;
    for (unsigned int i = 0; i < 13U; i++)
    {
        uint16_t ch = chars[(ordinal - 1U) * 13U + i];
        entry[char_offsets[i]] = (uint8_t)ch;
        entry[char_offsets[i] + 1U] = (uint8_t)(ch >> 8);
    }
    return fat32_write_sector(lba, sector_buffer);
}

static int fat32_write_named_entry(uint32_t parent, const char* name,
                                   const uint8_t alias[11], uint8_t attributes,
                                   uint32_t first_cluster, uint32_t size)
{
    unsigned int length = 0U;
    while (name && name[length])
    {
        if (length >= 255U || (unsigned char)name[length] >= 127U)
            return 0;
        length++;
    }
    if (!name || length == 0U)
        return 0;
    int needs_lfn = !fat32_name_matches_alias(name, alias);
    unsigned int lfn_count = needs_lfn ? (length + 12U) / 13U : 0U;
    unsigned int slot_count = lfn_count + 1U;
    uint32_t lbas[21], offsets[21];
    if (!fat32_reserve_named_slots(parent, slot_count, lbas, offsets))
        return 0;

    if (needs_lfn)
    {
        uint16_t chars[260];
        for (unsigned int i = 0; i < 260U; i++) chars[i] = 0xFFFFU;
        for (unsigned int i = 0; i < length; i++) chars[i] = (uint8_t)name[i];
        if (length < lfn_count * 13U) chars[length] = 0U;
        uint8_t checksum = fat32_lfn_checksum(alias);
        for (unsigned int physical = 0; physical < lfn_count; physical++)
        {
            unsigned int ordinal = lfn_count - physical;
            if (!fat32_write_lfn_entry(lbas[physical], offsets[physical],
                                       chars, ordinal, lfn_count, checksum))
                return 0;
        }
    }
    return fat32_write_entry(lbas[slot_count - 1U], offsets[slot_count - 1U],
                            alias, attributes, first_cluster, size);
}

static int fat32_mark_slot_deleted(uint32_t lba, uint32_t offset)
{
    if (offset > 480U || (offset & 31U) || !read_sector(lba, sector_buffer))
        return 0;
    sector_buffer[offset] = 0xE5U;
    return fat32_write_sector(lba, sector_buffer);
}

static int fat32_mark_entry_deleted(uint32_t directory, const fat32_slot_t* target)
{
    if (!target || !target->found)
        return 0;
    uint32_t cluster = directory;
    uint32_t lfn_lbas[20], lfn_offsets[20];
    unsigned int lfn_count = 0U;
    fat32_lfn_state_t lfn;
    fat32_lfn_reset(&lfn);
    for (uint32_t steps = 0; steps < cluster_count; steps++)
    {
        uint32_t base = cluster_lba(cluster);
        if (base == FAT32_INVALID_LBA) return 0;
        for (uint32_t sector = 0; sector < sectors_per_cluster; sector++)
        {
            uint32_t lba = base + sector;
            if (!read_sector(lba, sector_buffer)) return 0;
            for (uint32_t offset = 0; offset < 512U; offset += 32U)
            {
                const uint8_t* entry = &sector_buffer[offset];
                if (entry[0] == 0U) return 0;
                if (entry[0] == 0xE5U)
                {
                    lfn_count = 0U;
                    fat32_lfn_reset(&lfn);
                    continue;
                }
                if (entry[11] == 0x0FU)
                {
                    if (lfn_count < 20U)
                    {
                        lfn_lbas[lfn_count] = lba;
                        lfn_offsets[lfn_count] = offset;
                        lfn_count++;
                    }
                    (void)fat32_lfn_consume(entry, &lfn);
                    continue;
                }
                if (lba == target->entry_lba && offset == target->entry_offset)
                {
                    for (unsigned int i = 0; i < lfn_count; i++)
                        if (!fat32_mark_slot_deleted(lfn_lbas[i], lfn_offsets[i]))
                            return 0;
                    return fat32_mark_slot_deleted(lba, offset);
                }
                lfn_count = 0U;
                fat32_lfn_reset(&lfn);
            }
        }
        uint32_t next;
        if (!fat32_raw_entry(cluster, &next)) return 0;
        if (next >= FAT32_EOC_MIN) return 0;
        if (next == FAT32_BAD_CLUSTER || next < 2U ||
            next > cluster_count + 1U) return 0;
        cluster = next;
        lfn_count = 0U;
        fat32_lfn_reset(&lfn);
    }
    return 0;
}

int fat32_create_file(const char* path)
{
    uint32_t parent;
    uint8_t leaf[11];
    char name[256];
    fat32_slot_t entry;
    if (!fat32_parent_and_leaf(path, &parent, leaf) ||
        !fat32_leaf_name(path, name) ||
        !fat32_find_entry(parent, leaf, &entry) || entry.found)
        return -1;
    return fat32_write_named_entry(parent, name, leaf, 0x20U, 0U, 0U) ? 0 : -1;
}

int fat32_create_directory(const char* path)
{
    uint32_t parent;
    uint8_t leaf[11];
    char name[256];
    fat32_slot_t entry;
    if (!mounted || !fat32_parent_and_leaf(path, &parent, leaf) ||
        !fat32_leaf_name(path, name) ||
        !fat32_find_entry(parent, leaf, &entry) || entry.found)
        return -1;

    uint32_t directory_cluster = fat32_allocate_one();
    if (!directory_cluster)
        return -1;
    if (!fat32_write_named_entry(parent, name, leaf, 0x10U,
                                 directory_cluster, 0U))
    {
        fat32_release_chain(directory_cluster);
        return -1;
    }

    uint32_t base = cluster_lba(directory_cluster);
    if (base == FAT32_INVALID_LBA || !read_sector(base, sector_buffer))
        return -1;
    for (unsigned int i = 0; i < 32U; i++) sector_buffer[i] = 0U;
    sector_buffer[0] = '.';
    for (unsigned int i = 1; i < 11U; i++) sector_buffer[i] = ' ';
    sector_buffer[11] = 0x10U;
    sector_buffer[26] = (uint8_t)directory_cluster;
    sector_buffer[27] = (uint8_t)(directory_cluster >> 8);
    sector_buffer[20] = (uint8_t)(directory_cluster >> 16);
    sector_buffer[21] = (uint8_t)(directory_cluster >> 24);
    for (unsigned int i = 32U; i < 64U; i++) sector_buffer[i] = 0U;
    sector_buffer[32] = '.';
    sector_buffer[33] = '.';
    for (unsigned int i = 34U; i < 43U; i++) sector_buffer[i] = ' ';
    sector_buffer[43] = 0x10U;
    sector_buffer[58] = (uint8_t)parent;
    sector_buffer[59] = (uint8_t)(parent >> 8);
    sector_buffer[52] = (uint8_t)(parent >> 16);
    sector_buffer[53] = (uint8_t)(parent >> 24);
    if (!fat32_write_sector(base, sector_buffer))
        return -1;
    return 0;
}

int fat32_delete_path(const char* path)
{
    uint32_t parent;
    uint8_t leaf[11];
    fat32_slot_t entry;
    if (!mounted || !fat32_parent_and_leaf(path, &parent, leaf) ||
        !fat32_find_entry(parent, leaf, &entry) || !entry.found ||
        (entry.attributes & 0x01U) != 0U ||
        (entry.attributes & 0x08U) != 0U)
        return -1;

    if (entry.attributes & 0x10U)
    {
        fat32_dirent_t children[2];
        int count = fat32_list_directory(entry.first_cluster, children, 2U);
        if (count != 0)
            return -1;
    }

    if (!fat32_mark_entry_deleted(parent, &entry))
        return -1;
    if (entry.first_cluster >= 2U)
        fat32_release_chain(entry.first_cluster);
    return 0;
}

int fat32_rename_path(const char* old_path, const char* new_path)
{
    uint32_t old_parent, new_parent;
    uint8_t old_leaf[11], new_leaf[11];
    char new_name[256];
    fat32_slot_t source, destination;
    if (!mounted)
    {
        FAT32_RENAME_DIAG("VANTAOS_FAT32_RENAME_STAGE_1");
        return -1;
    }
    if (!fat32_parent_and_leaf(old_path, &old_parent, old_leaf))
    {
        FAT32_RENAME_DIAG("VANTAOS_FAT32_RENAME_STAGE_2");
        return -1;
    }
    if (!fat32_parent_and_leaf(new_path, &new_parent, new_leaf) ||
        !fat32_leaf_name(new_path, new_name))
    {
        FAT32_RENAME_DIAG("VANTAOS_FAT32_RENAME_STAGE_3");
        return -1;
    }
    if (old_parent != new_parent)
    {
        FAT32_RENAME_DIAG("VANTAOS_FAT32_RENAME_STAGE_4");
        return -1;
    }
    if (!fat32_find_entry(old_parent, old_leaf, &source) || !source.found)
    {
        FAT32_RENAME_DIAG("VANTAOS_FAT32_RENAME_STAGE_5");
        return -1;
    }
    if ((source.attributes & (0x01U | 0x08U)) != 0U)
    {
        FAT32_RENAME_DIAG("VANTAOS_FAT32_RENAME_STAGE_6");
        return -1;
    }
    if (!fat32_find_entry(new_parent, new_leaf, &destination) || destination.found)
    {
        FAT32_RENAME_DIAG("VANTAOS_FAT32_RENAME_STAGE_7");
        return -1;
    }
    if (!fat32_write_named_entry(new_parent, new_name, new_leaf,
                                 source.attributes, source.first_cluster,
                                 source.size))
    {
        FAT32_RENAME_DIAG("VANTAOS_FAT32_RENAME_STAGE_8");
        return -1;
    }
    if (!fat32_mark_entry_deleted(old_parent, &source))
    {
        FAT32_RENAME_DIAG("VANTAOS_FAT32_RENAME_STAGE_9");
        return -1;
    }
    return 0;
}

static int fat32_update_file_entry(uint32_t lba, uint32_t offset,
                                   uint32_t cluster, uint32_t size)
{
    if (offset > 480U || (offset & 31U))
        return 0;
    if (!read_sector(lba, sector_buffer))
        return 0;
    uint8_t* entry = &sector_buffer[offset];
    entry[20] = (uint8_t)(cluster >> 16);
    entry[21] = (uint8_t)(cluster >> 24);
    entry[26] = (uint8_t)cluster;
    entry[27] = (uint8_t)(cluster >> 8);
    entry[28] = (uint8_t)size;
    entry[29] = (uint8_t)(size >> 8);
    entry[30] = (uint8_t)(size >> 16);
    entry[31] = (uint8_t)(size >> 24);
    return fat32_write_sector(lba, sector_buffer);
}

static int fat32_next_for_write(uint32_t cluster, uint32_t* next)
{
    uint32_t value;
    if (!next || !fat32_raw_entry(cluster, &value))
        return 0;
    if (value >= FAT32_EOC_MIN)
    {
        uint32_t allocated = fat32_allocate_one();
        if (!allocated)
            return 0;
        if (!fat32_set_raw_entry(cluster, allocated))
        {
            fat32_release_chain(allocated);
            return 0;
        }
        *next = allocated;
        return 1;
    }
    if (value < 2U || value > cluster_count + 1U ||
        value == FAT32_BAD_CLUSTER)
        return 0;
    *next = value;
    return 1;
}

/*
 * Write one bounded chunk at an explicit byte offset. Storage allocation is
 * incremental: this routine only walks/allocates the clusters touched by the
 * chunk and never stages the whole file in RAM.
 */
int fat32_write_file_range(const char* path, uint32_t offset,
                           const char* data, uint32_t length)
{
    if (!mounted || !path || (length && !data) ||
        length > 0x7FFFFFFFU)
        return -1;
    if (length == 0U)
        return 0;

    uint64_t end = (uint64_t)offset + length;
    uint64_t cluster_bytes = (uint64_t)sectors_per_cluster * 512U;
    uint64_t volume_bytes = (uint64_t)cluster_count * cluster_bytes;
    if (end > UINT32_MAX || end > volume_bytes)
        return -1;

    uint32_t parent;
    uint8_t leaf[11];
    fat32_slot_t entry;
    if (!fat32_parent_and_leaf(path, &parent, leaf) ||
        !fat32_find_entry(parent, leaf, &entry) || !entry.found ||
        (entry.attributes & 0x01U) != 0U ||
        offset > entry.size ||
        (entry.size != 0U && entry.first_cluster < 2U))
        return -1;

    uint32_t first_cluster = entry.first_cluster;
    int newly_allocated_first = 0;
    if (first_cluster < 2U)
    {
        first_cluster = fat32_allocate_one();
        if (!first_cluster)
            return -1;
        newly_allocated_first = 1;
    }

    uint32_t current_cluster = first_cluster;
    uint32_t current_index = 0U;
    uint32_t written = 0U;

    while (written < length)
    {
        uint64_t absolute = (uint64_t)offset + written;
        uint32_t target_index = (uint32_t)(absolute / cluster_bytes);
        while (current_index < target_index)
        {
            uint32_t next;
            if (!fat32_next_for_write(current_cluster, &next))
            {
                if (newly_allocated_first)
                    fat32_release_chain(first_cluster);
                return -1;
            }
            current_cluster = next;
            current_index++;
        }

        uint32_t in_cluster = (uint32_t)(absolute % cluster_bytes);
        uint32_t sector_index = in_cluster / 512U;
        uint32_t in_sector = in_cluster % 512U;
        uint32_t lba = cluster_lba(current_cluster);
        if (lba == FAT32_INVALID_LBA ||
            sector_index >= sectors_per_cluster ||
            !read_sector(lba + sector_index, sector_buffer))
        {
            if (newly_allocated_first)
                fat32_release_chain(first_cluster);
            return -1;
        }

        uint32_t amount = 512U - in_sector;
        if (amount > length - written)
            amount = length - written;
        for (uint32_t i = 0; i < amount; i++)
            sector_buffer[in_sector + i] = (uint8_t)data[written + i];
        if (!fat32_write_sector(lba + sector_index, sector_buffer))
        {
            if (newly_allocated_first)
                fat32_release_chain(first_cluster);
            return -1;
        }
        written += amount;
    }

    uint32_t final_size = entry.size;
    if (end > final_size)
        final_size = (uint32_t)end;
    if (!fat32_update_file_entry(entry.entry_lba, entry.entry_offset,
                                 first_cluster, final_size))
    {
        /* The directory-sector write may have reached the device. Do not
           release the new chain after an ambiguous commit result. */
        return -1;
    }
    return (int)length;
}

int fat32_write_file(const char* path, const char* data, uint32_t size)
{
    if (!mounted || !path || (size && !data))
        return -1;

    uint64_t cluster_bytes = (uint64_t)sectors_per_cluster * 512U;
    uint64_t volume_bytes = (uint64_t)cluster_count * cluster_bytes;
    if (size > volume_bytes)
        return -1;

    uint32_t parent;
    uint8_t leaf[11];
    fat32_slot_t entry;
    if (!fat32_parent_and_leaf(path, &parent, leaf) ||
        !fat32_find_entry(parent, leaf, &entry) ||
        (entry.found && (entry.attributes & 0x01U)))
        return -1;

    if (!entry.found)
    {
        if (fat32_create_file(path) < 0 ||
            !fat32_find_entry(parent, leaf, &entry) || !entry.found)
            return -1;
    }

    uint32_t old_cluster = entry.first_cluster;
    if (!fat32_update_file_entry(entry.entry_lba, entry.entry_offset, 0U, 0U))
        return -1;
    if (old_cluster >= 2U)
        fat32_release_chain(old_cluster);

    uint32_t offset = 0U;
    while (offset < size)
    {
        uint32_t amount = size - offset;
        if (amount > 4096U)
            amount = 4096U;
        int result = fat32_write_file_range(path, offset, data + offset, amount);
        if (result != (int)amount)
            return -1;
        offset += amount;
    }
    return 0;
}
