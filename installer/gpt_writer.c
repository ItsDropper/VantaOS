/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "vanta_installer_gpt.h"

#define GPT_HEADER_SIZE 92U
#define GPT_HEADER_REVISION 0x00010000U
#define GPT_PRIMARY_ENTRIES_LBA 2ULL
#define GPT_BACKUP_ENTRIES_OFFSET 33ULL
#define GPT_ENTRY_NAME_OFFSET 56U
#define GPT_ENTRY_NAME_BYTES 72U

static void zero_bytes(uint8_t *p, size_t n)
{
    for (size_t i = 0U; i < n; ++i)
        p[i] = 0U;
}

static void copy_bytes(uint8_t *out, const uint8_t *in, size_t n)
{
    for (size_t i = 0U; i < n; ++i)
        out[i] = in[i];
}

static int bytes_equal(const uint8_t *a, const uint8_t *b, size_t n)
{
    for (size_t i = 0U; i < n; ++i)
        if (a[i] != b[i])
            return 0;
    return 1;
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8U);
}

static void put32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0U; i < 4U; ++i)
        p[i] = (uint8_t)(v >> (i * 8U));
}

static void put64(uint8_t *p, uint64_t v)
{
    for (unsigned i = 0U; i < 8U; ++i)
        p[i] = (uint8_t)(v >> (i * 8U));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0U; i < n; ++i) {
        crc ^= p[i];
        for (unsigned bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

static int guid_is_zero(const uint8_t guid[16])
{
    uint8_t any = 0U;
    for (size_t i = 0U; i < 16U; ++i)
        any |= guid[i];
    return any == 0U;
}

static void put_ascii_name(uint8_t *entry, const char *name)
{
    size_t i = 0U;
    while (name[i] && i < GPT_ENTRY_NAME_BYTES / 2U) {
        put16(entry + GPT_ENTRY_NAME_OFFSET + i * 2U, (uint8_t)name[i]);
        ++i;
    }
}

static int valid_inputs(
    const vanta_installer_disk_io_t *io,
    const vanta_installer_layout_t *layout,
    const uint8_t disk_guid[16],
    const uint8_t efi_guid[16],
    const uint8_t system_guid[16],
    const void *workspace,
    size_t workspace_size)
{
    if (!io || !io->read || !io->write || !io->flush || !io->context ||
        !layout || !disk_guid || !efi_guid || !system_guid || !workspace)
        return 0;
    if (workspace_size < VANTA_INSTALLER_GPT_WORKSPACE_SIZE)
        return 0;
    if (layout->disk_sector_count < VANTA_INSTALLER_MIN_DISK_SECTORS ||
        layout->first_usable_lba != 34U ||
        layout->last_usable_lba != layout->disk_sector_count -
            VANTA_INSTALLER_GPT_RESERVED_SECTORS ||
        layout->efi.first_lba != VANTA_INSTALLER_ALIGNMENT_SECTORS ||
        layout->efi.sector_count != VANTA_INSTALLER_ESP_SECTORS ||
        layout->system.first_lba % VANTA_INSTALLER_ALIGNMENT_SECTORS != 0U ||
        layout->system.sector_count == 0U ||
        layout->efi.first_lba + layout->efi.sector_count >
            layout->system.first_lba ||
        layout->system.first_lba > layout->last_usable_lba ||
        layout->system.sector_count - 1U >
            layout->last_usable_lba - layout->system.first_lba)
        return 0;
    if (guid_is_zero(disk_guid) || guid_is_zero(efi_guid) ||
        guid_is_zero(system_guid) || bytes_equal(disk_guid, efi_guid, 16U) ||
        bytes_equal(disk_guid, system_guid, 16U) ||
        bytes_equal(efi_guid, system_guid, 16U))
        return 0;
    return 1;
}

static void make_header(
    uint8_t sector[VANTA_INSTALLER_SECTOR_SIZE],
    uint64_t current_lba,
    uint64_t backup_lba,
    uint64_t entries_lba,
    const vanta_installer_layout_t *layout,
    const uint8_t disk_guid[16],
    uint32_t entries_crc)
{
    zero_bytes(sector, VANTA_INSTALLER_SECTOR_SIZE);
    copy_bytes(sector, (const uint8_t *)"EFI PART", 8U);
    put32(sector + 8U, GPT_HEADER_REVISION);
    put32(sector + 12U, GPT_HEADER_SIZE);
    put64(sector + 24U, current_lba);
    put64(sector + 32U, backup_lba);
    put64(sector + 40U, layout->first_usable_lba);
    put64(sector + 48U, layout->last_usable_lba);
    copy_bytes(sector + 56U, disk_guid, 16U);
    put64(sector + 72U, entries_lba);
    put32(sector + 80U, VANTA_INSTALLER_GPT_ENTRY_COUNT);
    put32(sector + 84U, VANTA_INSTALLER_GPT_ENTRY_SIZE);
    put32(sector + 88U, entries_crc);
    put32(sector + 16U, crc32(sector, GPT_HEADER_SIZE));
}

static void make_entries(
    uint8_t *entries,
    const vanta_installer_layout_t *layout,
    const uint8_t efi_guid[16],
    const uint8_t system_guid[16])
{
    static const uint8_t efi_type_guid[16] = {
        0x28U, 0x73U, 0x2AU, 0xC1U, 0x1FU, 0xF8U, 0xD2U, 0x11U,
        0xBAU, 0x4BU, 0x00U, 0xA0U, 0xC9U, 0x3EU, 0xC9U, 0x3BU
    };
    static const uint8_t basic_data_guid[16] = {
        0xA2U, 0xA0U, 0xD0U, 0xEBU, 0xE5U, 0xB9U, 0x33U, 0x44U,
        0x87U, 0xC0U, 0x68U, 0xB6U, 0xB7U, 0x26U, 0x99U, 0xC7U
    };
    uint8_t *efi = entries;
    uint8_t *system = entries + VANTA_INSTALLER_GPT_ENTRY_SIZE;

    zero_bytes(entries, VANTA_INSTALLER_GPT_WORKSPACE_SIZE);
    copy_bytes(efi, efi_type_guid, 16U);
    copy_bytes(efi + 16U, efi_guid, 16U);
    put64(efi + 32U, layout->efi.first_lba);
    put64(efi + 40U, layout->efi.first_lba + layout->efi.sector_count - 1U);
    put_ascii_name(efi, "VantaOS EFI");

    copy_bytes(system, basic_data_guid, 16U);
    copy_bytes(system + 16U, system_guid, 16U);
    put64(system + 32U, layout->system.first_lba);
    put64(system + 40U,
          layout->system.first_lba + layout->system.sector_count - 1U);
    put_ascii_name(system, "VantaOS System");
}

static void make_protective_mbr(
    uint8_t sector[VANTA_INSTALLER_SECTOR_SIZE],
    uint64_t disk_sector_count)
{
    uint8_t *entry;
    uint64_t protected_count = disk_sector_count - 1U;
    zero_bytes(sector, VANTA_INSTALLER_SECTOR_SIZE);
    entry = sector + 446U;
    entry[0] = 0U;
    entry[1] = 0U;
    entry[2] = 2U;
    entry[3] = 0U;
    entry[4] = 0xEEU;
    entry[5] = 0xFFU;
    entry[6] = 0xFFU;
    entry[7] = 0xFFU;
    put32(entry + 8U, 1U);
    put32(entry + 12U,
          protected_count > UINT32_MAX ? UINT32_MAX : (uint32_t)protected_count);
    sector[510U] = 0x55U;
    sector[511U] = 0xAAU;
}

static int verify_sector(
    const vanta_installer_disk_io_t *io,
    uint64_t lba,
    const uint8_t expected[VANTA_INSTALLER_SECTOR_SIZE])
{
    uint8_t actual[VANTA_INSTALLER_SECTOR_SIZE];
    return io->read(io->context, lba, 1U, actual) &&
           bytes_equal(actual, expected, sizeof(actual));
}

vanta_installer_gpt_status_t vanta_installer_write_gpt(
    const vanta_installer_disk_io_t *io,
    const vanta_installer_layout_t *layout,
    const uint8_t disk_guid[16],
    const uint8_t efi_partition_guid[16],
    const uint8_t system_partition_guid[16],
    void *workspace,
    size_t workspace_size)
{
    uint8_t header[VANTA_INSTALLER_SECTOR_SIZE];
    uint8_t protective_mbr[VANTA_INSTALLER_SECTOR_SIZE];
    uint8_t *entries = (uint8_t *)workspace;
    uint64_t backup_entries_lba;
    uint32_t entries_crc;

    if (!valid_inputs(io, layout, disk_guid, efi_partition_guid,
                      system_partition_guid, workspace, workspace_size))
        return VANTA_INSTALLER_GPT_INVALID_ARGUMENT;

    backup_entries_lba = layout->disk_sector_count -
                         GPT_BACKUP_ENTRIES_OFFSET;
    make_entries(entries, layout, efi_partition_guid, system_partition_guid);
    entries_crc = crc32(entries, VANTA_INSTALLER_GPT_WORKSPACE_SIZE);

    /* Publish backup metadata first so a primary-header write failure is recoverable. */
    if (!io->write(io->context, backup_entries_lba,
                   VANTA_INSTALLER_GPT_ARRAY_SECTORS, entries))
        return VANTA_INSTALLER_GPT_IO_ERROR;
    make_header(header, layout->disk_sector_count - 1U, 1U,
                backup_entries_lba, layout, disk_guid, entries_crc);
    if (!io->write(io->context, layout->disk_sector_count - 1U, 1U, header) ||
        !io->flush(io->context))
        return VANTA_INSTALLER_GPT_IO_ERROR;

    if (!io->write(io->context, GPT_PRIMARY_ENTRIES_LBA,
                   VANTA_INSTALLER_GPT_ARRAY_SECTORS, entries))
        return VANTA_INSTALLER_GPT_IO_ERROR;
    make_header(header, 1U, layout->disk_sector_count - 1U,
                GPT_PRIMARY_ENTRIES_LBA, layout, disk_guid, entries_crc);
    if (!io->write(io->context, 1U, 1U, header))
        return VANTA_INSTALLER_GPT_IO_ERROR;

    make_protective_mbr(protective_mbr, layout->disk_sector_count);
    if (!io->write(io->context, 0U, 1U, protective_mbr) ||
        !io->flush(io->context))
        return VANTA_INSTALLER_GPT_IO_ERROR;

    if (!verify_sector(io, 0U, protective_mbr))
        return VANTA_INSTALLER_GPT_VERIFY_ERROR;
    make_header(header, 1U, layout->disk_sector_count - 1U,
                GPT_PRIMARY_ENTRIES_LBA, layout, disk_guid, entries_crc);
    if (!verify_sector(io, 1U, header))
        return VANTA_INSTALLER_GPT_VERIFY_ERROR;
    make_header(header, layout->disk_sector_count - 1U, 1U,
                backup_entries_lba, layout, disk_guid, entries_crc);
    if (!verify_sector(io, layout->disk_sector_count - 1U, header))
        return VANTA_INSTALLER_GPT_VERIFY_ERROR;

    {
        uint8_t actual[VANTA_INSTALLER_SECTOR_SIZE];
        for (uint32_t i = 0U; i < VANTA_INSTALLER_GPT_ARRAY_SECTORS; ++i) {
            if (!io->read(io->context, GPT_PRIMARY_ENTRIES_LBA + i, 1U, actual) ||
                !bytes_equal(actual, entries + (size_t)i * sizeof(actual),
                             sizeof(actual)))
                return VANTA_INSTALLER_GPT_VERIFY_ERROR;
            if (!io->read(io->context, backup_entries_lba + i, 1U, actual) ||
                !bytes_equal(actual, entries + (size_t)i * sizeof(actual),
                             sizeof(actual)))
                return VANTA_INSTALLER_GPT_VERIFY_ERROR;
        }
    }
    return VANTA_INSTALLER_GPT_OK;
}
