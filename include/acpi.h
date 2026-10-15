#ifndef ACPI_H
#define ACPI_H

#include <stdint.h>

#define ACPI_MAX_CPUS 32U
#define ACPI_MAX_ISO 16U

typedef struct
{
    uint8_t source_irq;
    uint32_t gsi;
    uint16_t flags;
} acpi_irq_override_t;

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

#endif
