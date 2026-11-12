/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_INTEL_SUNRISEPOINT_H
#define VANTA_INTEL_SUNRISEPOINT_H

#include "acpi_aml.h"

#include <stdint.h>

#define INTEL_SPTLP_PIN_COUNT 152U
#define INTEL_SPTLP_COMMUNITY_COUNT 3U

typedef struct
{
    uint16_t first_pin;
    uint16_t last_pin;
    uint8_t index;
} intel_sptlp_community_t;

/* Return the Sunrise Point-LP community containing a global GPIO number. */
int intel_sptlp_pin_community(uint16_t pin, uint8_t *community);

/*
 * Validate an ACPI GPIO connection against its resolved INT344B controller.
 * This is metadata-only: it never changes pad ownership, mux, direction,
 * electrical configuration, interrupt enable, or status registers.
 */
int intel_sptlp_validate_gpio_resource(
    const acpi_aml_device_t *controller,
    const acpi_aml_resource_t *resource);

#endif
