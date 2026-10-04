#include "fat32.h"
#include "ata.h"

#define FAT32_PARTITION_OFFSET 446
#define FAT32_MAX_CLUSTER 0x0FFFFFF7U

static int mounted;
static uint32_t partition_lba;
static uint32_t fat_lba;
static uint32_t data_lba;
static uint32_t sectors_per_fat;
static uint32_t sectors_per_cluster;
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
    return ata_read_sectors(lba, 1, buffer);
}

static int valid_boot_sector(const uint8_t* b)
{
    if (b[510] != 0x55 || b[511] != 0xAA)
        return 0;

    if (b[82] != 'F' || b[83] != 'A' ||
        b[84] != 'T' || b[85] != '3' ||
        b[86] != '2')
        return 0;

    return 1;
}

static int mount_partition(uint32_t lba)
{
    if (!read_sector(lba, boot_sector_buffer))
        return 0;

    const uint8_t* boot = boot_sector_buffer;

    if (!valid_boot_sector(boot))
        return 0;

    bytes_per_sector = fat16(&boot[11]);
    sectors_per_cluster = boot[13];
    uint16_t reserved = fat16(&boot[14]);
    fat_count = boot[16];
    sectors_per_fat = fat32(&boot[36]);
    root_cluster = fat32(&boot[44]);

    if (bytes_per_sector != 512 ||
        sectors_per_cluster == 0 ||
        reserved == 0 ||
        fat_count == 0 ||
        sectors_per_fat == 0 ||
        root_cluster < 2)
        return 0;

    partition_lba = lba;
    fat_lba = lba + reserved;
    data_lba =
        fat_lba +
        (uint32_t)fat_count * sectors_per_fat;

    mounted = 1;
    return 1;
}

static int find_partition(void)
{
    if (!read_sector(0, sector_buffer))
        return 0;

    if (sector_buffer[510] != 0x55 ||
        sector_buffer[511] != 0xAA)
        return 0;

    for (unsigned int i = 0; i < 4; i++)
    {
        unsigned int offset =
            FAT32_PARTITION_OFFSET + i * 16;

        uint8_t type = sector_buffer[offset + 4];

        if (type != 0x0B && type != 0x0C)
            continue;

        uint32_t start =
            fat32(&sector_buffer[offset + 8]);

        if (mount_partition(start))
            return 1;
    }

    return 0;
}

static uint32_t cluster_lba(uint32_t cluster)
{
    return data_lba +
        (cluster - 2) * sectors_per_cluster;
}

static uint32_t next_cluster(uint32_t cluster)
{
    uint32_t fat_offset = cluster * 4;
    uint32_t lba =
        fat_lba + fat_offset / 512;
    uint32_t offset = fat_offset % 512;

    if (!read_sector(lba, sector_buffer))
        return FAT32_MAX_CLUSTER;

    return fat32(&sector_buffer[offset]) & 0x0FFFFFFFU;
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

void fat32_initialize(void)
{
    mounted = 0;

    if (!ata_is_available())
        return;

    /*
     * A normal partitioned FAT32 disk is preferred.
     * Super-floppy FAT32 is also accepted.
     */
    if (find_partition())
        return;

    if (mount_partition(0))
        return;
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
        cluster < 2)
        return -1;

    unsigned int count = 0;

    while (cluster >= 2 &&
           cluster < FAT32_MAX_CLUSTER)
    {
        uint32_t base = cluster_lba(cluster);

        for (uint32_t sector = 0;
             sector < sectors_per_cluster;
             sector++)
        {
            if (!read_sector(base + sector, sector_buffer))
                return -1;

            for (unsigned int offset = 0;
                 offset < 512;
                 offset += 32)
            {
                const uint8_t* entry =
                    &sector_buffer[offset];

                if (entry[0] == 0x00)
                    return (int)count;

                if (entry[0] == 0xE5 ||
                    entry[11] == 0x0F)
                    continue;

                if (count >= capacity)
                    return (int)count;

                short_name(entry, entries[count].name);

                uint32_t high =
                    (uint32_t)fat16(&entry[20]);
                uint32_t low =
                    (uint32_t)fat16(&entry[26]);

                entries[count].first_cluster =
                    (high << 16) | low;

                entries[count].size =
                    fat32(&entry[28]);

                entries[count].directory =
                    (entry[11] & 0x10) != 0;

                if (entries[count].name[0] != 0 &&
                    entries[count].name[0] != '.' )
                    count++;
            }
        }

        cluster = next_cluster(cluster);
    }

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

    while (remaining &&
           cluster >= 2 &&
           cluster < FAT32_MAX_CLUSTER)
    {
        uint32_t base = cluster_lba(cluster);

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
    }

    buffer[written] = 0;
    return (int)written;
}
