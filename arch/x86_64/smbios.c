/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "serial.h"
#include "smbios.h"

#include <stddef.h>
#include <stdint.h>

#define SMBIOS_SCAN_START 0xF0000U
#define SMBIOS_SCAN_END   0x100000U
#define SMBIOS_TABLE_LIMIT 0x40000000ULL
#define SMBIOS_MAX_TABLE_SIZE (1024U * 1024U)

typedef struct __attribute__((packed))
{
    char anchor[4];
    uint8_t checksum;
    uint8_t length;
    uint8_t major;
    uint8_t minor;
    uint16_t maximum_structure_size;
    uint8_t entry_revision;
    uint8_t formatted_area[5];
    char intermediate_anchor[5];
    uint8_t intermediate_checksum;
    uint16_t table_length;
    uint32_t table_address;
    uint16_t structure_count;
    uint8_t bcd_revision;
} smbios2_entry_t;

typedef struct __attribute__((packed))
{
    char anchor[5];
    uint8_t checksum;
    uint8_t length;
    uint8_t major;
    uint8_t minor;
    uint8_t document_revision;
    uint8_t entry_revision;
    uint8_t reserved;
    uint32_t maximum_table_size;
    uint64_t table_address;
} smbios3_entry_t;

typedef struct __attribute__((packed))
{
    uint8_t type;
    uint8_t length;
    uint16_t handle;
} smbios_header_t;

typedef struct
{
    uint64_t table_address;
    uint32_t table_length;
    uint32_t structure_limit;
    uint8_t major;
    uint8_t minor;
    uint8_t is_3x;
} smbios_location_t;

static uint8_t checksum8(const void* memory, uint32_t length)
{
    const uint8_t* bytes = (const uint8_t*)memory;
    uint8_t sum = 0U;
    for (uint32_t i = 0U; i < length; ++i)
        sum = (uint8_t)(sum + bytes[i]);
    return sum;
}

static int bytes_equal(const char* left, const char* right, size_t length)
{
    for (size_t i = 0U; i < length; ++i)
        if (left[i] != right[i])
            return 0;
    return 1;
}

static void write_u32(uint32_t value)
{
    char digits[10];
    uint32_t count = 0U;
    do
    {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    while (count != 0U)
    {
        char digit = digits[--count];
        serial_write_bytes(&digit, 1U);
    }
}

static void write_string(const uint8_t* structure, uint8_t structure_length,
                         const uint8_t* table_end, uint8_t string_index)
{
    if (string_index == 0U || structure_length < 4U || !table_end)
    {
        serial_write("unknown");
        return;
    }

    const uint8_t* cursor = structure + structure_length;
    for (uint32_t current = 1U; current <= string_index; ++current)
    {
        if (cursor >= table_end || *cursor == 0U)
        {
            serial_write("unknown");
            return;
        }
        if (current == string_index)
        {
            uint32_t emitted = 0U;
            while (cursor < table_end && *cursor != 0U && emitted < 96U)
            {
                char character = (*cursor >= 0x20U && *cursor <= 0x7EU) ?
                    (char)*cursor : '?';
                serial_write_bytes(&character, 1U);
                ++cursor;
                ++emitted;
            }
            if (cursor < table_end && *cursor != 0U)
                serial_write("...");
            return;
        }
        while (cursor < table_end && *cursor != 0U)
            ++cursor;
        if (cursor >= table_end)
        {
            serial_write("unknown");
            return;
        }
        ++cursor;
    }
    serial_write("unknown");
}

static int find_entrypoint(smbios_location_t* location)
{
    if (!location)
        return 0;

    for (uintptr_t address = SMBIOS_SCAN_START;
         address < SMBIOS_SCAN_END; address += 16U)
    {
        const uint8_t* bytes = (const uint8_t*)address;

        if (bytes_equal((const char*)bytes, "_SM3_", 5U))
        {
            const smbios3_entry_t* entry =
                (const smbios3_entry_t*)bytes;
            if (entry->length < sizeof(*entry) || entry->length > 64U ||
                checksum8(entry, entry->length) != 0U ||
                entry->maximum_table_size == 0U ||
                entry->maximum_table_size > SMBIOS_MAX_TABLE_SIZE ||
                entry->table_address == 0U ||
                entry->table_address >= SMBIOS_TABLE_LIMIT ||
                entry->maximum_table_size >
                    SMBIOS_TABLE_LIMIT - entry->table_address)
                continue;

            location->table_address = entry->table_address;
            location->table_length = entry->maximum_table_size;
            location->structure_limit = 0U;
            location->major = entry->major;
            location->minor = entry->minor;
            location->is_3x = 1U;
            return 1;
        }

        if (bytes_equal((const char*)bytes, "_SM_", 4U))
        {
            const smbios2_entry_t* entry =
                (const smbios2_entry_t*)bytes;
            if (entry->length < sizeof(*entry) || entry->length > 64U ||
                checksum8(entry, entry->length) != 0U ||
                !bytes_equal(entry->intermediate_anchor, "_DMI_", 5U) ||
                checksum8(&entry->intermediate_anchor,
                          15U) != 0U ||
                entry->table_length == 0U ||
                entry->table_address == 0U ||
                entry->table_address >= SMBIOS_TABLE_LIMIT ||
                entry->table_length >
                    SMBIOS_TABLE_LIMIT - entry->table_address ||
                entry->structure_count == 0U)
                continue;

            location->table_address = entry->table_address;
            location->table_length = entry->table_length;
            location->structure_limit = entry->structure_count;
            location->major = entry->major;
            location->minor = entry->minor;
            location->is_3x = 0U;
            return 1;
        }
    }
    return 0;
}

static uint32_t memory_device_mib(const uint8_t* structure,
                                  uint8_t length)
{
    if (length < 14U)
        return 0U;
    uint16_t size = (uint16_t)structure[12] |
                    ((uint16_t)structure[13] << 8U);
    if (size == 0U || size == 0xFFFFU)
        return 0U;
    if (size == 0x7FFFU)
    {
        if (length < 32U)
            return 0U;
        return (uint32_t)structure[28] |
               ((uint32_t)structure[29] << 8U) |
               ((uint32_t)structure[30] << 16U) |
               ((uint32_t)structure[31] << 24U);
    }
    if ((size & 0x8000U) != 0U)
        return (uint32_t)(size & 0x7FFFU) / 1024U;
    return size;
}

static void inventory_structure(const uint8_t* structure,
                                uint8_t length, const uint8_t* table_end,
                                uint32_t* memory_mib,
                                uint32_t* memory_devices)
{
    switch (structure[0])
    {
        case 0U: /* BIOS Information */
            if (length >= 6U)
            {
                serial_write("VANTAOS_HW_SMBIOS_BIOS vendor=");
                write_string(structure, length, table_end, structure[4]);
                serial_write(" version=");
                write_string(structure, length, table_end, structure[5]);
                serial_write("\n");
            }
            break;
        case 1U: /* System Information; deliberately omit serial/UUID. */
            if (length >= 8U)
            {
                serial_write("VANTAOS_HW_SMBIOS_SYSTEM manufacturer=");
                write_string(structure, length, table_end, structure[4]);
                serial_write(" product=");
                write_string(structure, length, table_end, structure[5]);
                serial_write(" version=");
                write_string(structure, length, table_end, structure[6]);
                serial_write("\n");
            }
            break;
        case 2U: /* Baseboard Information; deliberately omit serial. */
            if (length >= 8U)
            {
                serial_write("VANTAOS_HW_SMBIOS_BOARD manufacturer=");
                write_string(structure, length, table_end, structure[4]);
                serial_write(" product=");
                write_string(structure, length, table_end, structure[5]);
                serial_write(" version=");
                write_string(structure, length, table_end, structure[6]);
                serial_write("\n");
            }
            break;
        case 4U: /* Processor Information; omit processor ID/serial data. */
            if (length >= 17U)
            {
                serial_write("VANTAOS_HW_SMBIOS_CPU manufacturer=");
                write_string(structure, length, table_end, structure[7]);
                serial_write(" model=");
                write_string(structure, length, table_end, structure[16]);
                serial_write("\n");
            }
            break;
        case 17U:
            if (length >= 14U)
            {
                uint32_t size = memory_device_mib(structure, length);
                if (size != 0U && *memory_mib <= UINT32_MAX - size)
                    *memory_mib += size;
                if (size != 0U && *memory_devices != UINT32_MAX)
                    ++*memory_devices;
            }
            break;
        default:
            break;
    }
}

void smbios_inventory_initialize(void)
{
    smbios_location_t location = {0};
    if (!find_entrypoint(&location))
    {
        serial_write("VANTAOS_HW_SMBIOS_UNAVAILABLE\n");
        return;
    }

    serial_write("VANTAOS_HW_SMBIOS_VERSION=");
    write_u32(location.major);
    serial_write(".");
    write_u32(location.minor);
    serial_write(location.is_3x ? " format=3.x\n" : " format=2.x\n");

    const uint8_t* table = (const uint8_t*)(uintptr_t)location.table_address;
    uint32_t offset = 0U;
    uint32_t structures = 0U;
    uint32_t memory_mib = 0U;
    uint32_t memory_devices = 0U;

    while (offset <= location.table_length &&
           location.table_length - offset >= sizeof(smbios_header_t) &&
           (location.structure_limit == 0U ||
            structures < location.structure_limit))
    {
        const uint8_t* structure = table + offset;
        uint8_t length = structure[1];
        if (length < sizeof(smbios_header_t) ||
            length > location.table_length - offset)
        {
            serial_write("VANTAOS_HW_SMBIOS_MALFORMED offset=");
            write_u32(offset);
            serial_write("\n");
            break;
        }

        uint32_t next = offset + length;
        /* Each structure is followed by a double-NUL-terminated string set. */
        while (next + 1U < location.table_length)
        {
            if (table[next] == 0U && table[next + 1U] == 0U)
            {
                next += 2U;
                break;
            }
            ++next;
        }
        if (next > location.table_length ||
            next <= offset + length)
        {
            serial_write("VANTAOS_HW_SMBIOS_MALFORMED_STRINGS\n");
            break;
        }

        inventory_structure(structure, length, table + location.table_length,
                            &memory_mib, &memory_devices);
        ++structures;
        offset = next;
        if (structure[0] == 127U)
            break;
    }

    serial_write("VANTAOS_HW_SMBIOS_STRUCTURES=");
    write_u32(structures);
    serial_write("\n");
    serial_write("VANTAOS_HW_MEMORY_MODULES=");
    write_u32(memory_devices);
    serial_write(" capacity_mib=");
    write_u32(memory_mib);
    serial_write("\n");
}
