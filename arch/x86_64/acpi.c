/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "acpi.h"
#include "vanta_boot_info.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

#define ACPI_IDENTITY_LIMIT 0x40000000ULL
#define ACPI_RSDP_VALID_FLAG (1U << 3)

typedef struct __attribute__((packed))
{
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
} acpi_rsdp_v1_t;

typedef struct __attribute__((packed))
{
    acpi_rsdp_v1_t first;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} acpi_rsdp_v2_t;

typedef struct __attribute__((packed))
{
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} acpi_sdt_header_t;

static uint8_t checksum8(const void* memory, uint32_t length)
{
    const uint8_t* bytes = (const uint8_t*)memory;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < length; i++)
        sum = (uint8_t)(sum + bytes[i]);
    return sum;
}

static int physical_range(uint64_t address, uint64_t length)
{
    return address != 0 && length != 0 &&
           address < ACPI_IDENTITY_LIMIT &&
           length <= ACPI_IDENTITY_LIMIT - address;
}

static const acpi_rsdp_v1_t* scan_rsdp(void)
{
    for (uintptr_t address = 0xE0000U; address < 0x100000U; address += 16U)
    {
        const acpi_rsdp_v1_t* rsdp =
            (const acpi_rsdp_v1_t*)address;
        static const char signature[8] = {'R','S','D',' ','P','T','R',' '};
        int match = 1;
        for (unsigned int i = 0; i < sizeof(signature); i++)
            if (rsdp->signature[i] != signature[i])
                match = 0;
        if (match && checksum8(rsdp, 20U) == 0)
        {
            if (rsdp->revision >= 2)
            {
                const acpi_rsdp_v2_t* extended =
                    (const acpi_rsdp_v2_t*)rsdp;
                if (extended->length < sizeof(acpi_rsdp_v2_t) ||
                    extended->length > 4096U ||
                    !physical_range(address, extended->length) ||
                    checksum8(extended, extended->length) != 0)
                    continue;
            }
            return rsdp;
        }
    }
    return NULL;
}

static int signature_is(const char* a, const char* b)
{
    for (unsigned int i = 0; i < 4; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

static int table_valid(const acpi_sdt_header_t* table,
                       const char* signature)
{
    if (!table || !signature_is(table->signature, signature) ||
        table->length < sizeof(acpi_sdt_header_t) ||
        !physical_range((uint64_t)(uintptr_t)table, table->length))
        return 0;
    return checksum8(table, table->length) == 0;
}

static const acpi_sdt_header_t* acpi_get_root_table(
    unsigned int* entry_size_out)
{
    const acpi_rsdp_v1_t* rsdp = NULL;
    const vanta_boot_info_t* boot = kernel_get_boot_info();
    if (boot &&
        (boot->flags & ACPI_RSDP_VALID_FLAG) &&
        physical_range(boot->acpi_rsdp_address, sizeof(acpi_rsdp_v1_t)))
    {
        const acpi_rsdp_v1_t* candidate =
            (const acpi_rsdp_v1_t*)(uintptr_t)boot->acpi_rsdp_address;
        if (checksum8(candidate, 20U) == 0)
            rsdp = candidate;
    }
    if (!rsdp)
        rsdp = scan_rsdp();
    if (!rsdp)
        return NULL;

    uint64_t root_address = rsdp->rsdt_address;
    unsigned int entry_size = sizeof(uint32_t);
    if (rsdp->revision >= 2)
    {
        const acpi_rsdp_v2_t* extended = (const acpi_rsdp_v2_t*)rsdp;
        if (extended->length >= sizeof(acpi_rsdp_v2_t) &&
            physical_range((uint64_t)(uintptr_t)rsdp, extended->length) &&
            checksum8(extended, extended->length) == 0 &&
            physical_range(extended->xsdt_address,
                           sizeof(acpi_sdt_header_t)))
        {
            root_address = extended->xsdt_address;
            entry_size = sizeof(uint64_t);
        }
    }

    if (!physical_range(root_address, sizeof(acpi_sdt_header_t)))
        return NULL;
    const acpi_sdt_header_t* root =
        (const acpi_sdt_header_t*)(uintptr_t)root_address;
    const char* signature =
        entry_size == sizeof(uint64_t) ? "XSDT" : "RSDT";
    if (!table_valid(root, signature) ||
        (root->length - sizeof(*root)) % entry_size != 0)
        return NULL;

    if (entry_size_out)
        *entry_size_out = entry_size;
    return root;
}

static void add_cpu(acpi_platform_info_t* info, uint32_t apic_id,
                    uint32_t flags)
{
    if ((flags & 3U) == 0 || info->cpu_count >= ACPI_MAX_CPUS)
        return;
    for (uint32_t i = 0; i < info->cpu_count; i++)
        if (info->cpu_apic_ids[i] == apic_id)
            return;
    info->cpu_apic_ids[info->cpu_count++] = apic_id;
}

static int parse_madt(const acpi_sdt_header_t* table,
                      acpi_platform_info_t* info)
{
    if (!table_valid(table, "APIC") || table->length < 44U)
        return 0;

    const uint8_t* bytes = (const uint8_t*)table;
    info->local_apic_address = *(const uint32_t*)(bytes + 36);
    uint32_t cursor = 44U;

    while (cursor + 2U <= table->length)
    {
        const uint8_t* entry = bytes + cursor;
        uint8_t type = entry[0];
        uint8_t length = entry[1];
        if (length < 2U || length > table->length - cursor)
            return 0;

        if (type == 0 && length >= 8U)
        {
            uint32_t flags = *(const uint32_t*)(entry + 4);
            add_cpu(info, entry[3], flags);
        }
        else if (type == 1 && length >= 12U && info->io_apic_address == 0)
        {
            info->io_apic_id = entry[2];
            info->io_apic_address = *(const uint32_t*)(entry + 4);
            info->io_apic_gsi_base = *(const uint32_t*)(entry + 8);
        }
        else if (type == 2 && length >= 10U &&
                 info->override_count < ACPI_MAX_ISO)
        {
            acpi_irq_override_t* override =
                &info->overrides[info->override_count++];
            override->source_irq = entry[3];
            override->gsi = *(const uint32_t*)(entry + 4);
            override->flags = *(const uint16_t*)(entry + 8);
        }
        else if (type == 5 && length >= 12U)
        {
            info->local_apic_address = *(const uint64_t*)(entry + 4);
        }
        else if (type == 9 && length >= 16U)
        {
            uint32_t apic_id = *(const uint32_t*)(entry + 8);
            uint32_t flags = *(const uint32_t*)(entry + 12);
            add_cpu(info, apic_id, flags);
        }

        cursor += length;
    }

    return info->local_apic_address != 0 &&
           info->io_apic_address != 0 &&
           info->cpu_count != 0;
}

int acpi_discover_platform(acpi_platform_info_t* info)
{
    if (!info)
        return 0;

    uint8_t* clear = (uint8_t*)info;
    for (size_t i = 0; i < sizeof(*info); i++)
        clear[i] = 0;

    unsigned int entry_size = 0U;
    const acpi_sdt_header_t* root = acpi_get_root_table(&entry_size);
    if (!root)
        return 0;

    uint32_t entries = (root->length - sizeof(*root)) / entry_size;
    const uint8_t* table_entries =
        (const uint8_t*)root + sizeof(*root);
    for (uint32_t i = 0; i < entries; i++)
    {
        uint64_t address = entry_size == sizeof(uint64_t) ?
            *(const uint64_t*)(table_entries + i * entry_size) :
            *(const uint32_t*)(table_entries + i * entry_size);
        if (!physical_range(address, sizeof(acpi_sdt_header_t)))
            continue;
        const acpi_sdt_header_t* table =
            (const acpi_sdt_header_t*)(uintptr_t)address;
        if (signature_is(table->signature, "APIC"))
            return parse_madt(table, info);
    }
    return 0;
}

uint32_t acpi_discover_pci_mcfg(acpi_mcfg_region_t* regions,
                                uint32_t capacity)
{
    unsigned int entry_size = 0U;
    const acpi_sdt_header_t* root = acpi_get_root_table(&entry_size);
    if (!root || (!regions && capacity != 0U))
        return 0U;

    uint32_t entries = (root->length - sizeof(*root)) / entry_size;
    const uint8_t* table_entries = (const uint8_t*)root + sizeof(*root);
    for (uint32_t i = 0; i < entries; i++)
    {
        uint64_t address = entry_size == sizeof(uint64_t) ?
            *(const uint64_t*)(table_entries + i * entry_size) :
            *(const uint32_t*)(table_entries + i * entry_size);
        if (!physical_range(address, sizeof(acpi_sdt_header_t)))
            continue;
        const acpi_sdt_header_t* table =
            (const acpi_sdt_header_t*)(uintptr_t)address;
        if (!signature_is(table->signature, "MCFG"))
            continue;
        if (!table_valid(table, "MCFG") || table->length < 44U ||
            ((table->length - 44U) % 16U) != 0U)
            return 0U;

        const uint8_t* bytes = (const uint8_t*)table;
        for (uint32_t reserved = 36U; reserved < 44U; reserved++)
            if (bytes[reserved] != 0U)
                return 0U;

        uint32_t count = 0U;
        for (uint32_t cursor = 44U; cursor < table->length; cursor += 16U)
        {
            uint64_t base = *(const uint64_t*)(bytes + cursor);
            uint16_t segment = *(const uint16_t*)(bytes + cursor + 8U);
            uint8_t start_bus = bytes[cursor + 10U];
            uint8_t end_bus = bytes[cursor + 11U];
            uint32_t reserved = *(const uint32_t*)(bytes + cursor + 12U);
            uint64_t bus_count = (uint64_t)end_bus - start_bus + 1U;
            uint64_t span = bus_count << 20U;

            if (reserved != 0U || base == 0U ||
                (base & 0xFFFFFU) != 0U || end_bus < start_bus ||
                span > UINT64_MAX - base)
                continue;

            /* Reject overlapping ECAM windows within the same segment. */
            int overlaps = 0;
            if (regions)
                for (uint32_t j = 0; j < count && j < capacity; j++)
                {
                    const acpi_mcfg_region_t* prior = &regions[j];
                    if (prior->segment_group != segment)
                        continue;
                    if (start_bus <= prior->end_bus &&
                        prior->start_bus <= end_bus)
                    {
                        overlaps = 1;
                        continue;
                    }
                    uint64_t prior_span =
                        ((uint64_t)prior->end_bus - prior->start_bus + 1U)
                        << 20U;
                    uint64_t prior_end = prior->base_address + prior_span;
                    uint64_t end = base + span;
                    if (base < prior_end && prior->base_address < end)
                        overlaps = 1;
                }
            if (overlaps)
                continue;

            if (regions && count < capacity)
            {
                regions[count].base_address = base;
                regions[count].segment_group = segment;
                regions[count].start_bus = start_bus;
                regions[count].end_bus = end_bus;
            }
            if (count != UINT32_MAX)
                count++;
        }
        return count < capacity ? count : capacity;
    }
    return 0U;
}



static uint16_t acpi_read16(const uint8_t* p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8U));
}
static uint32_t acpi_read32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}
static uint64_t acpi_read64(const uint8_t* p)
{
    return (uint64_t)acpi_read32(p) |
           ((uint64_t)acpi_read32(p + 4U) << 32U);
}
static const acpi_sdt_header_t* acpi_validated_table(
    uint64_t address, const char* signature)
{
    if (!physical_range(address, sizeof(acpi_sdt_header_t)))
        return NULL;
    const acpi_sdt_header_t* table =
        (const acpi_sdt_header_t*)(uintptr_t)address;
    return table_valid(table, signature) ? table : NULL;
}

int acpi_discover_tables(acpi_table_inventory_t* inventory)
{
    if (!inventory)
        return 0;
    uint8_t* out = (uint8_t*)inventory;
    for (size_t i = 0U; i < sizeof(*inventory); ++i)
        out[i] = 0U;

    unsigned int entry_size = 0U;
    const acpi_sdt_header_t* root = acpi_get_root_table(&entry_size);
    if (!root)
        return 0;
    const uint8_t* entries = (const uint8_t*)root + sizeof(*root);
    uint32_t count = (root->length - (uint32_t)sizeof(*root)) / entry_size;
    const acpi_sdt_header_t* fadt = NULL;

    for (uint32_t i = 0U; i < count; ++i)
    {
        uint64_t address = entry_size == sizeof(uint64_t) ?
            acpi_read64(entries + i * entry_size) :
            acpi_read32(entries + i * entry_size);
        if (!physical_range(address, sizeof(acpi_sdt_header_t)))
            continue;
        const acpi_sdt_header_t* table =
            (const acpi_sdt_header_t*)(uintptr_t)address;
        if (!table_valid(table, table->signature))
            continue;
        if (signature_is(table->signature, "FACP"))
        {
            if (!fadt)
                fadt = table;
        }
        else if (signature_is(table->signature, "SSDT"))
        {
            if (inventory->ssdt_total != UINT32_MAX)
                ++inventory->ssdt_total;
            if (inventory->ssdt_count < ACPI_MAX_SSDT)
            {
                uint32_t slot = inventory->ssdt_count++;
                inventory->ssdt_addresses[slot] = address;
                inventory->ssdt_lengths[slot] = table->length;
            }
        }
    }

    /* SCI_INT and DSDT are present in the ACPI 1.0 FADT layout. */
    if (!fadt || fadt->length < 48U)
        return 0;
    const uint8_t* bytes = (const uint8_t*)fadt;
    inventory->fadt_revision = fadt->revision;
    inventory->sci_interrupt = acpi_read16(bytes + 46U);

    uint64_t legacy_dsdt = acpi_read32(bytes + 40U);
    uint64_t dsdt_address = legacy_dsdt;
    if (fadt->length >= 148U)
    {
        uint64_t extended_dsdt = acpi_read64(bytes + 140U);
        if (physical_range(extended_dsdt, sizeof(acpi_sdt_header_t)))
            dsdt_address = extended_dsdt;
    }
    const acpi_sdt_header_t* dsdt =
        acpi_validated_table(dsdt_address, "DSDT");
    if (!dsdt && dsdt_address != legacy_dsdt)
    {
        dsdt_address = legacy_dsdt;
        dsdt = acpi_validated_table(dsdt_address, "DSDT");
    }
    if (!dsdt)
        return 0;
    inventory->dsdt_address = dsdt_address;
    inventory->dsdt_length = dsdt->length;
    return 1;
}


/*
 * Read-only firmware table inventory for physical bring-up. Every table is
 * range-checked and checksum-validated before any OEM metadata is emitted.
 */
void acpi_log_table_inventory(void)
{
    unsigned int entry_size = 0U;
    const acpi_sdt_header_t* root = acpi_get_root_table(&entry_size);
    if (!root)
    {
        serial_write("VANTAOS_HW_ACPI_UNAVAILABLE\n");
        return;
    }

    serial_write("VANTAOS_HW_ACPI_ROOT=");
    serial_write(entry_size == sizeof(uint64_t) ? "XSDT" : "RSDT");
    serial_write(" revision=");
    {
        char digits[3];
        uint32_t value = root->revision;
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
    serial_write(" oem=");
    for (uint32_t i = 0U; i < sizeof(root->oem_id); ++i)
    {
        char character = (root->oem_id[i] >= 0x20 &&
                          root->oem_id[i] <= 0x7E) ?
                         root->oem_id[i] : '?';
        serial_write_bytes(&character, 1U);
    }
    serial_write("\n");

    uint32_t entries = (root->length - sizeof(*root)) / entry_size;
    const uint8_t* table_entries = (const uint8_t*)root + sizeof(*root);
    uint32_t valid = 0U;
    for (uint32_t i = 0U; i < entries; ++i)
    {
        uint64_t address = entry_size == sizeof(uint64_t) ?
            *(const uint64_t*)(table_entries + i * entry_size) :
            *(const uint32_t*)(table_entries + i * entry_size);
        if (!physical_range(address, sizeof(acpi_sdt_header_t)))
            continue;
        const acpi_sdt_header_t* table =
            (const acpi_sdt_header_t*)(uintptr_t)address;
        if (table->length < sizeof(*table) ||
            table->length > (16U * 1024U * 1024U) ||
            !physical_range(address, table->length) ||
            checksum8(table, table->length) != 0U)
            continue;

        serial_write("VANTAOS_HW_ACPI_TABLE signature=");
        for (uint32_t j = 0U; j < sizeof(table->signature); ++j)
        {
            char character = (table->signature[j] >= 0x20 &&
                              table->signature[j] <= 0x7E) ?
                             table->signature[j] : '?';
            serial_write_bytes(&character, 1U);
        }
        serial_write(" length=");
        {
            char digits[10];
            uint32_t value = table->length;
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
        serial_write(" oem=");
        for (uint32_t j = 0U; j < sizeof(table->oem_id); ++j)
        {
            char character = (table->oem_id[j] >= 0x20 &&
                              table->oem_id[j] <= 0x7E) ?
                             table->oem_id[j] : '?';
            serial_write_bytes(&character, 1U);
        }
        serial_write(" table_id=");
        for (uint32_t j = 0U; j < sizeof(table->oem_table_id); ++j)
        {
            char character = (table->oem_table_id[j] >= 0x20 &&
                              table->oem_table_id[j] <= 0x7E) ?
                             table->oem_table_id[j] : '?';
            serial_write_bytes(&character, 1U);
        }
        serial_write("\n");
        ++valid;
    }

    serial_write("VANTAOS_HW_ACPI_TABLE_COUNT=");
    {
        char digits[10];
        uint32_t value = valid;
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
    serial_write("\n");

    acpi_table_inventory_t namespace_tables;
    if (acpi_discover_tables(&namespace_tables))
    {
        serial_write("VANTAOS_HW_ACPI_DSDT address=0x");
        char hex[16];
        for (uint32_t i = 0U; i < 16U; ++i)
            hex[i] = "0123456789ABCDEF"[(namespace_tables.dsdt_address >>
                         ((15U - i) * 4U)) & 0x0FU];
        serial_write_bytes(hex, sizeof(hex));
        serial_write(" length=");
        {
            char digits[10];
            uint32_t value = namespace_tables.dsdt_length, n = 0U;
            do { digits[n++] = (char)('0' + value % 10U); value /= 10U; }
            while (value && n < sizeof(digits));
            while (n) { char c = digits[--n]; serial_write_bytes(&c, 1U); }
        }
        serial_write(" sci=");
        {
            char digits[5];
            uint32_t value = namespace_tables.sci_interrupt, n = 0U;
            do { digits[n++] = (char)('0' + value % 10U); value /= 10U; }
            while (value && n < sizeof(digits));
            while (n) { char c = digits[--n]; serial_write_bytes(&c, 1U); }
        }
        serial_write(" ssdt=");
        {
            char digits[5];
            uint32_t value = namespace_tables.ssdt_total, n = 0U;
            do { digits[n++] = (char)('0' + value % 10U); value /= 10U; }
            while (value && n < sizeof(digits));
            while (n) { char c = digits[--n]; serial_write_bytes(&c, 1U); }
        }
        serial_write("\n");
    }
    else
        serial_write("VANTAOS_HW_ACPI_NAMESPACE_TABLES_UNAVAILABLE\n");
}
