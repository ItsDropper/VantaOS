/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef ACPI_AML_H
#define ACPI_AML_H

#include <stdint.h>

#define ACPI_AML_PATH_MAX 64U
#define ACPI_AML_ID_MAX 16U
#define ACPI_AML_MAX_CIDS 4U
#define ACPI_AML_MAX_RESOURCES 8U
#define ACPI_AML_MAX_I2C_RESOURCES 4U

typedef struct
{
    uint8_t descriptor_tag;
    uint16_t descriptor_length;
    uint8_t resource_type;
    uint8_t flags;
    uint64_t minimum;
    uint64_t maximum;
    uint64_t address;
    uint64_t length;
    uint32_t irq_mask;
    uint32_t irq;
    uint8_t serial_bus_type;
    uint8_t general_flags;
    uint16_t type_flags;
    uint32_t connection_speed_hz;
    uint16_t slave_address;
    char resource_source[ACPI_AML_PATH_MAX];
} acpi_aml_resource_t;

typedef struct
{
    char path[ACPI_AML_PATH_MAX];
    char parent_path[ACPI_AML_PATH_MAX];
    uint64_t adr;
    uint8_t adr_valid;
    uint8_t bbn;
    uint8_t bbn_valid;
    uint16_t segment;
    uint8_t segment_valid;
    char hid[ACPI_AML_ID_MAX];
    char cids[ACPI_AML_MAX_CIDS][ACPI_AML_ID_MAX];
    uint32_t cid_count;
    uint64_t uid;
    uint8_t uid_valid;
    uint8_t uid_is_string;
    char uid_string[ACPI_AML_ID_MAX];
    uint8_t sta_valid;
    uint8_t sta_dynamic;
    uint8_t invalid;
    uint32_t sta;
    uint8_t hid_descriptor_address_valid;
    uint16_t hid_descriptor_address;
    uint32_t resource_count;
    uint32_t resource_total;
    uint32_t i2c_count;
    acpi_aml_resource_t resources[ACPI_AML_MAX_RESOURCES];
} acpi_aml_device_t;

typedef void (*acpi_aml_device_callback_t)(
    const acpi_aml_device_t* device, void* context);

/*
 * Parses the AML bytecode region (the bytes after the 36-byte SDT header).
 * This is a bounded, non-executing namespace subset: Scope, Device, static
 * Name values and Buffer resource templates. Methods and control-flow
 * packages are skipped, never evaluated. Returns zero on malformed input or
 * a parser limit; callbacks may already have received earlier devices.
 */
int acpi_aml_parse_table(const uint8_t* aml, uint32_t length,
                         acpi_aml_device_callback_t callback,
                         void* context, uint32_t* devices_found);

#endif
