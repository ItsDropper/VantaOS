/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_INTEL_SUNRISEPOINT_H
#define VANTA_INTEL_SUNRISEPOINT_H

#include "acpi_aml.h"

#include <stddef.h>
#include <stdint.h>

#define INTEL_SPTLP_PIN_COUNT 152U
#define INTEL_SPTLP_COMMUNITY_COUNT 3U
#define INTEL_SPTLP_PAD_OWN_OFFSET 0x020U
#define INTEL_SPTLP_PADCFGLOCK_OFFSET 0x0A0U
#define INTEL_SPTLP_HOSTSW_OWN_OFFSET 0x0D0U
#define INTEL_SPTLP_GPI_IE_OFFSET 0x120U
#define INTEL_SPTLP_MMIO_MIN_LENGTH 0x124U
#define INTEL_SPTLP_GPP_SIZE 24U
#define INTEL_SPTLP_GPI_IS_OFFSET 0x100U
#define INTEL_SPTLP_MMIO_MAX_LENGTH 0x10000U

typedef struct
{
    uint16_t first_pin;
    uint16_t last_pin;
    uint8_t index;
} intel_sptlp_community_t;

typedef struct
{
    uint8_t community;
    uint8_t group;
    uint8_t bit;
    uint32_t interrupt_status_offset;
    uint32_t interrupt_enable_offset;
} intel_sptlp_irq_location_t;

typedef struct
{
    void *context;
    void *(*map)(void *context,uint64_t physical_address,size_t length);
    int (*unmap)(void *context,void *mapping,size_t length);
    uint32_t (*read32)(void *context,const volatile uint8_t *mapping,
                       uint32_t offset);
} intel_sptlp_mmio_ops_t;

typedef struct
{
    uint64_t base;
    uint64_t length;
    uint32_t pad_ownership;
    uint32_t pad_config_lock;
    uint32_t host_software_ownership;
    uint32_t interrupt_enable;
} intel_sptlp_community_snapshot_t;

typedef struct
{
    intel_sptlp_community_snapshot_t communities[INTEL_SPTLP_COMMUNITY_COUNT];
} intel_sptlp_snapshot_t;

/* Return the Sunrise Point-LP community containing a global GPIO number. */
int intel_sptlp_pin_community(uint16_t pin, uint8_t *community);
int intel_sptlp_pin_irq_location(uint16_t pin,
                                 intel_sptlp_irq_location_t *location);

/*
 * Map the three ACPI-assigned community windows and take a bounded read-only
 * register snapshot. All mappings are released before returning; no register
 * is written and no pin configuration or interrupt state is changed.
 */
int intel_sptlp_probe_controller(const acpi_aml_device_t *controller,
                                 const intel_sptlp_mmio_ops_t *ops,
                                 intel_sptlp_snapshot_t *snapshot);

/*
 * Validate an ACPI GPIO connection against its resolved INT344B controller.
 * This is metadata-only: it never changes pad ownership, mux, direction,
 * electrical configuration, interrupt enable, or status registers.
 */
int intel_sptlp_validate_gpio_resource(
    const acpi_aml_device_t *controller,
    const acpi_aml_resource_t *resource);

#endif
