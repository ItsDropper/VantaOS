#include "acpi.h"
#include "vanta_boot_info.h"

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
        return 0;

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
        return 0;
    const acpi_sdt_header_t* root =
        (const acpi_sdt_header_t*)(uintptr_t)root_address;
    const char* root_signature =
        entry_size == sizeof(uint64_t) ? "XSDT" : "RSDT";
    if (!table_valid(root, root_signature) ||
        (root->length - sizeof(*root)) % entry_size != 0)
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
