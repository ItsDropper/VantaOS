/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef ACPI_H
#define ACPI_H

#include <stdint.h>

#define ACPI_MAX_CPUS 32U
#define ACPI_MAX_ISO 16U
#define ACPI_MAX_MCFG_REGIONS 8U


#define ACPI_MAX_SSDT 16U
typedef struct
{
    uint64_t dsdt_address;
    uint32_t dsdt_length;
    uint64_t ssdt_addresses[ACPI_MAX_SSDT];
    uint32_t ssdt_lengths[ACPI_MAX_SSDT];
    uint32_t ssdt_count;
    uint32_t ssdt_total;
    uint16_t sci_interrupt;
    uint8_t fadt_revision;
    uint8_t reserved;
} acpi_table_inventory_t;

/* Read-only, checksum-validated FADT/DSDT/SSDT discovery. No AML execution. */
int acpi_discover_tables(acpi_table_inventory_t* inventory);

typedef struct
{
    uint8_t source_irq;
    uint32_t gsi;
    uint16_t flags;
} acpi_irq_override_t;

typedef struct
{
    uint64_t base_address;
    uint16_t segment_group;
    uint8_t start_bus;
    uint8_t end_bus;
} acpi_mcfg_region_t;

typedef struct
{
    uint64_t local_apic_address;
    uint64_t io_apic_address;
    uint32_t io_apic_gsi_base;
    uint8_t io_apic_id;
    uint32_t cpu_apic_ids[ACPI_MAX_CPUS];
    uint32_t cpu_count;
    acpi_irq_override_t overrides[ACPI_MAX_ISO];
    uint32_t override_count;
} acpi_platform_info_t;

/* Parses ACPI RSDP and MADT; returns zero if no valid APIC topology is found. */
int acpi_discover_platform(acpi_platform_info_t* info);
/* Emits checksummed root-table metadata for hardware diagnostics. */
void acpi_log_table_inventory(void);
/* Returns the number of validated PCIe ECAM regions copied to the output. */
uint32_t acpi_discover_pci_mcfg(acpi_mcfg_region_t* regions,
                                uint32_t capacity);

#endif
