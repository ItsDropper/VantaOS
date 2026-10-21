/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "partition_scan.h"

#define SECTOR 512U
#define GPT_MAX 128U
#define GPT_ENTRY_MAX 512U
#define EBR_MAX 128U
#define MBR_OFF 446U
#define GPT_ARRAY_MAX (GPT_MAX * GPT_ENTRY_MAX)

static uint8_t entry_storage[GPT_ARRAY_MAX];

static uint16_t rd16(const uint8_t *p)
{ return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t rd64(const uint8_t *p)
{ return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4U) << 32); }
static uint32_t crc_step(uint32_t crc, const uint8_t *p, size_t n)
{
    for (size_t i = 0U; i < n; ++i) {
        crc ^= p[i];
        for (unsigned b = 0U; b < 8U; ++b)
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return crc;
}
static uint32_t crc(const uint8_t *p, size_t n)
{ return ~crc_step(0xFFFFFFFFU, p, n); }
static int zeros(const uint8_t *p, size_t n)
{
    for (size_t i = 0U; i < n; ++i) if (p[i]) return 0;
    return 1;
}
static int read_checked(partition_read_fn read, void *ctx, uint64_t disk,
                        uint64_t lba, uint32_t count, void *buffer)
{
    return read && buffer && count && lba < disk &&
           (uint64_t)count <= disk - lba && read(ctx, lba, count, buffer);
}
static int range_ok(uint64_t start, uint64_t length, uint64_t disk)
{ return length && start < disk && length <= disk - start; }
static int overlap(const partition_scan_entry_t *out, size_t n,
                   uint64_t start, uint64_t length)
{
    uint64_t end = start + length;
    for (size_t i = 0U; i < n; ++i) {
        uint64_t other_end = out[i].start_lba + out[i].sector_count;
        if (start < other_end && out[i].start_lba < end) return 1;
    }
    return 0;
}
static int append(partition_scan_entry_t *out, size_t cap, size_t *n,
                  uint64_t start, uint64_t length, uint8_t type,
                  partition_scheme_t scheme, const uint8_t *guid,
                  const char *name)
{
    if (*n >= cap || *n >= PARTITION_SCAN_MAX) return 0;
    if (overlap(out, *n, start, length)) return -1;
    partition_scan_entry_t *e = &out[*n];
    e->start_lba = start; e->sector_count = length;
    e->mbr_type = type; e->scheme = (uint8_t)scheme;
    for (size_t i = 0U; i < 16U; ++i) e->type_guid[i] = guid ? guid[i] : 0U;
    size_t i = 0U;
    if (name) while (i + 1U < sizeof(e->name) && name[i]) {
        e->name[i] = name[i]; ++i;
    }
    e->name[i] = '\0'; ++*n; return 1;
}
static int valid_gpt_header(const uint8_t *h, uint64_t current, uint64_t disk)
{
    if (h[0]!='E'||h[1]!='F'||h[2]!='I'||h[3]!=' '||
        h[4]!='P'||h[5]!='A'||h[6]!='R'||h[7]!='T') return 0;
    uint32_t size = rd32(h + 12U);
    if (size < 92U || size > SECTOR || rd64(h + 24U) != current ||
        rd64(h + 32U) >= disk || rd64(h + 40U) > rd64(h + 48U) ||
        rd64(h + 48U) >= disk) return 0;
    uint8_t copy[SECTOR];
    for (size_t i = 0U; i < SECTOR; ++i) copy[i] = h[i];
    uint32_t expected = rd32(copy + 16U);
    for (size_t i = 16U; i < 20U; ++i) copy[i] = 0U;
    return crc(copy, size) == expected;
}
static int read_array(partition_read_fn read, void *ctx, uint64_t disk,
                      uint64_t lba, uint32_t count, uint32_t size,
                      uint32_t expected_crc, uint8_t *storage)
{
    uint64_t bytes = (uint64_t)count * size;
    uint64_t sectors = (bytes + SECTOR - 1U) / SECTOR;
    if (!count || count > GPT_MAX || size < 128U || size > GPT_ENTRY_MAX ||
        (size & 7U) || bytes > GPT_ARRAY_MAX ||
        !range_ok(lba, sectors, disk)) return 0;
    uint64_t copied = 0U;
    for (uint64_t i = 0U; i < sectors; ++i) {
        uint8_t block[SECTOR];
        if (!read_checked(read, ctx, disk, lba + i, 1U, block)) return 0;
        size_t amount = bytes - copied < SECTOR ? (size_t)(bytes - copied) : SECTOR;
        for (size_t j = 0U; j < amount; ++j) storage[copied + j] = block[j];
        copied += amount;
    }
    return crc(storage, (size_t)bytes) == expected_crc;
}
static partition_scan_status_t scan_gpt(partition_read_fn read, void *ctx,
    uint64_t disk, partition_scan_entry_t *out, size_t cap, size_t *out_count)
{
    uint8_t primary[SECTOR], backup[SECTOR], backup_array[GPT_ARRAY_MAX];
    if (!read_checked(read, ctx, disk, 1U, 1U, primary)) return PARTITION_SCAN_IO_ERROR;
    if (!valid_gpt_header(primary, 1U, disk)) return PARTITION_SCAN_BAD_TABLE;
    uint64_t backup_lba = rd64(primary + 32U);
    if (!read_checked(read, ctx, disk, backup_lba, 1U, backup)) return PARTITION_SCAN_IO_ERROR;
    if (!valid_gpt_header(backup, backup_lba, disk) ||
        rd64(backup + 32U) != 1U || rd32(backup + 80U) != rd32(primary + 80U) ||
        rd32(backup + 84U) != rd32(primary + 84U) ||
        rd32(backup + 88U) != rd32(primary + 88U))
        return PARTITION_SCAN_BAD_TABLE;
    for (size_t i = 0U; i < 16U; ++i)
        if (backup[56U + i] != primary[56U + i]) return PARTITION_SCAN_BAD_TABLE;
    uint32_t n = rd32(primary + 80U), size = rd32(primary + 84U);
    uint32_t array_crc = rd32(primary + 88U);
    if (!read_array(read, ctx, disk, rd64(primary + 72U), n, size,
                    array_crc, entry_storage) ||
        !read_array(read, ctx, disk, rd64(backup + 72U), n, size,
                    array_crc, backup_array))
        return PARTITION_SCAN_BAD_TABLE;
    uint64_t first_usable = rd64(primary + 40U), last_usable = rd64(primary + 48U);
    size_t count = 0U;
    for (uint32_t i = 0U; i < n; ++i) {
        const uint8_t *r = entry_storage + (size_t)i * size;
        if (zeros(r, 16U)) continue;
        uint64_t first = rd64(r + 32U), last = rd64(r + 40U);
        if (first > last || first < first_usable || last > last_usable || last >= disk)
            return PARTITION_SCAN_BAD_TABLE;
        char name[PARTITION_NAME_MAX]; size_t pos = 0U;
        for (size_t j = 0U; j < 72U && pos + 1U < sizeof(name); j += 2U) {
            uint16_t ch = rd16(r + 56U + j);
            if (!ch) break;
            name[pos++] = ch >= 32U && ch < 127U ? (char)ch : '?';
        }
        name[pos] = '\0';
        int added = append(out, cap, &count, first, last - first + 1U, 0U,
                           PARTITION_SCHEME_GPT, r, name);
        if (added <= 0) return added < 0 ? PARTITION_SCAN_BAD_TABLE : PARTITION_SCAN_CAPACITY;
    }
    if (!count) return PARTITION_SCAN_NO_PARTITIONS;
    *out_count = count; return PARTITION_SCAN_OK;
}
