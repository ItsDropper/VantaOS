#include "fat32.h"
#include "fat32_lfn.h"
#include "ata.h"

#define FAT32_PARTITION_OFFSET 446
#define FAT32_MAX_CLUSTER 0x0FFFFFF7U
#define FAT32_EOC_MIN 0x0FFFFFF8U
#define FAT32_BAD_CLUSTER 0xFFFFFFFEU
#define FAT32_LBA28_MAX 0x0FFFFFFFU
#define FAT32_INVALID_LBA 0xFFFFFFFFU

static int mounted;
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
    return ata_read_sectors(lba, 1, buffer);
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

    if (lba > FAT32_LBA28_MAX ||
        (partition_sectors &&
         (partition_sectors - 1U > FAT32_LBA28_MAX - lba)) ||
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
        total - 1U > FAT32_LBA28_MAX - lba)
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
        uint32_t length =
            fat32(&sector_buffer[offset + 12]);

        if (length != 0 && mount_partition(start, length))
            return 1;
    }

    return 0;
}

static uint32_t cluster_lba(uint32_t cluster)
{
    if (!mounted || cluster < 2 || cluster > cluster_count + 1U)
        return FAT32_INVALID_LBA;

    uint64_t lba = (uint64_t)data_lba +
        (uint64_t)(cluster - 2U) * sectors_per_cluster;
    if (lba + sectors_per_cluster > (uint64_t)partition_lba + total_sectors ||
        lba + sectors_per_cluster - 1U > FAT32_LBA28_MAX)
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
    return ata_write_sectors(lba, 1, data);
}

static int fat32_raw_entry(uint32_t cluster, uint32_t* value)
{
    if (cluster < 2 || cluster > cluster_count + 1U || !value)
        return 0;
    uint32_t offset = cluster * 4U;
    if ((uint64_t)offset + 4U > (uint64_t)sectors_per_fat * 512U)
        return 0;
    uint32_t lba = fat_lba + offset / 512U;
    if (!read_sector(lba, sector_buffer))
        return 0;
    *value = fat32(&sector_buffer[offset % 512U]) & 0x0FFFFFFFU;
    return 1;
}

static int fat32_set_raw_entry(uint32_t cluster, uint32_t value)
{
    if (cluster < 2 || cluster > cluster_count + 1U || value > FAT32_MAX_CLUSTER)
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
    for (unsigned int i = 0; i < 11; i++) out[i] = ' ';
    if (!input || !*input) return 0;
    for (unsigned int i = 0; input[i]; i++)
    {
        char ch = input[i];
        if (ch == '.')
        {
            if (dot || !base) return 0;
            dot = 1;
            continue;
        }
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
              ch == '_' || ch == '-' || ch == '{
    mounted = 0;

    if (!ata_is_available())
        return;

    /*
     * A normal partitioned FAT32 disk is preferred.
     * Super-floppy FAT32 is also accepted.
     */
    if (find_partition())
        return;

    if (mount_partition(0, 0))
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
 || ch == '~'))
            return 0;
        if (!dot)
        {
            if (base == 8) return 0;
            out[base++] = (uint8_t)ch;
        }
        else
        {
            if (ext == 3) return 0;
            out[8U + ext++] = (uint8_t)ch;
        }
    }
    return base != 0;
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
    if (!out || dir < 2 || dir > cluster_count + 1U) return 0;
    out->found = out->free_found = 0;
    uint32_t cluster = dir;
    for (uint32_t steps = 0; steps < cluster_count; steps++)
    {
        uint32_t base = cluster_lba(cluster);
        if (base == FAT32_INVALID_LBA) return 0;
        out->last_cluster = cluster;
        for (uint32_t s = 0; s < sectors_per_cluster; s++)
        {
            uint32_t lba = base + s;
            if (!read_sector(lba, sector_buffer)) return 0;
            for (uint32_t off = 0; off < 512; off += 32)
            {
                const uint8_t* e = &sector_buffer[off];
                if (e[0] == 0 || e[0] == 0xE5)
                {
                    if (!out->free_found)
                    {
                        out->free_found = 1;
                        out->free_lba = lba;
                        out->free_offset = off;
                    }
                    if (e[0] == 0) return 1;
                    continue;
                }
                if (e[11] == 0x0F || (e[11] & 0x08U) || !wanted) continue;
                unsigned int k;
                for (k = 0; k < 11 && e[k] == wanted[k]; k++) {}
                if (k == 11)
                {
                    out->found = 1;
                    out->entry_lba = lba;
                    out->entry_offset = off;
                    out->attributes = e[11];
                    out->first_cluster = ((uint32_t)fat16(&e[20]) << 16) | fat16(&e[26]);
                    out->size = fat32(&e[28]);
                    return 1;
                }
            }
        }
        uint32_t next;
        if (!fat32_raw_entry(cluster, &next)) return 0;
        if (next >= FAT32_EOC_MIN) return 1;
        if (next < 2 || next > cluster_count + 1U || next == 0x0FFFFFF7U) return 0;
        cluster = next;
    }
    return 0;
}

static int fat32_parent_and_leaf(const char* path, uint32_t* parent, uint8_t leaf[11])
{
    if (!path || path[0] != '/' || !parent || !leaf) return 0;
    const char* p = path;
    while (*p == '/') p++;
    uint32_t dir = root_cluster;
    for (;;)
    {
        char component[13];
        unsigned int n = 0;
        while (p[n] && p[n] != '/')
        {
            if (n >= 12) return 0;
            component[n] = p[n];
            n++;
        }
        if (!n) return 0;
        component[n] = 0;
        p += n;
        while (*p == '/') p++;
        uint8_t name[11];
        if (!fat32_make_short_name(component, name)) return 0;
        if (!*p)
        {
            *parent = dir;
            for (unsigned int i = 0; i < 11; i++) leaf[i] = name[i];
            return 1;
        }
        fat32_slot_t found;
        if (!fat32_find_entry(dir, name, &found) || !found.found ||
            !(found.attributes & 0x10U) || found.first_cluster < 2)
            return 0;
        dir = found.first_cluster;
    }
}

static int fat32_write_entry(uint32_t lba, uint32_t off, const uint8_t name[11],
                             uint32_t cluster, uint32_t size)
{
    if (off > 480 || (off & 31U)) return 0;
    if (!read_sector(lba, sector_buffer)) return 0;
    uint8_t* e = &sector_buffer[off];
    for (unsigned int i = 0; i < 11; i++) e[i] = name[i];
    e[11] = 0x20;
    e[12] = e[13] = 0;
    for (unsigned int i = 14; i < 20; i++) e[i] = 0;
    e[20] = (uint8_t)(cluster >> 16);
    e[21] = (uint8_t)(cluster >> 24);
    e[26] = (uint8_t)cluster;
    e[27] = (uint8_t)(cluster >> 8);
    e[28] = (uint8_t)size;
    e[29] = (uint8_t)(size >> 8);
    e[30] = (uint8_t)(size >> 16);
    e[31] = (uint8_t)(size >> 24);
    return fat32_write_sector(lba, sector_buffer);
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

    if (mount_partition(0, 0))
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


int fat32_create_file(const char* path)
{
    uint32_t parent;
    uint8_t leaf[11];
    fat32_slot_t entry;
    if (!fat32_parent_and_leaf(path, &parent, leaf) ||
        !fat32_find_entry(parent, leaf, &entry) || entry.found)
        return -1;

    uint32_t lba = 0, off = 0;
    if (!entry.free_found)
    {
        uint32_t new_cluster = fat32_allocate_one();
        if (!new_cluster || !fat32_set_raw_entry(entry.last_cluster, new_cluster))
            return -1;
        lba = cluster_lba(new_cluster);
        off = 0;
        if (lba == FAT32_INVALID_LBA) return -1;
    }
    return fat32_write_entry(lba, off, leaf, 0, 0) ? 0 : -1;
}

int fat32_write_file(const char* path, const char* data, uint32_t size)
{
    if (!data || size > 512U) return -1;
    uint32_t parent;
    uint8_t leaf[11];
    fat32_slot_t entry;
    if (!fat32_parent_and_leaf(path, &parent, leaf) ||
        !fat32_find_entry(parent, leaf, &entry) ||
        (entry.found && (entry.attributes & 0x01U)))
        return -1;

    uint32_t new_cluster = 0;
    if (size)
    {
        new_cluster = fat32_allocate_one();
        if (!new_cluster) return -1;
        uint32_t base = cluster_lba(new_cluster);
        if (base == FAT32_INVALID_LBA || !read_sector(base, sector_buffer))
        {
            fat32_release_chain(new_cluster);
            return -1;
        }
        for (unsigned int i = 0; i < 512; i++)
            sector_buffer[i] = i < size ? (uint8_t)data[i] : 0;
        if (!fat32_write_sector(base, sector_buffer))
        {
            fat32_release_chain(new_cluster);
            return -1;
        }
    }

    uint32_t lba = 0, off = 0;
    if (entry.found)
    {
        lba = entry.entry_lba;
        off = entry.entry_offset;
    }
    else
    {
        if (!entry.free_found)
        {
            uint32_t new_dir_cluster = fat32_allocate_one();
            if (!new_dir_cluster || !fat32_set_raw_entry(entry.last_cluster, new_dir_cluster))
            {
                if (new_cluster) fat32_release_chain(new_cluster);
                return -1;
            }
            lba = cluster_lba(new_dir_cluster);
            off = 0;
        }
        else
        {
            lba = entry.free_lba;
            off = entry.free_offset;
        }
    }

    if (!fat32_write_entry(lba, off, leaf, new_cluster, size))
        return -1; /* Keep new allocation: the directory write may have reached disk. */

    if (entry.found && entry.first_cluster >= 2)
        fat32_release_chain(entry.first_cluster);
    return 0;
}
