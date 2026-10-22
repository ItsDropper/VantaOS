/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef VANTA_SETUP_CONFIG_H
#define VANTA_SETUP_CONFIG_H

#include <stddef.h>

#define VANTA_SETUP_CONFIG_MAX_BYTES 160U
#define VANTA_SETUP_LANGUAGE_MAX 3U
#define VANTA_SETUP_KEYBOARD_MAX 3U
#define VANTA_SETUP_TIMEZONE_MAX 40U

typedef struct {
    char language[VANTA_SETUP_LANGUAGE_MAX];
    char keyboard[VANTA_SETUP_KEYBOARD_MAX];
    char timezone[VANTA_SETUP_TIMEZONE_MAX];
} vanta_setup_config_t;

/* IDs are deliberately limited to the zone choices shipped by setup. */
int vanta_setup_timezone_supported(const char *timezone);
size_t vanta_setup_timezone_count(void);
const char *vanta_setup_timezone_at(size_t index);
int vanta_setup_config_valid(const vanta_setup_config_t *config);

/* Versioned, bounded, newline-terminated UTF-8 text; no dynamic allocation. */
int vanta_setup_config_encode(
    const vanta_setup_config_t *config,
    char *output,
    size_t output_capacity,
    size_t *output_length);

int vanta_setup_config_decode(
    const char *input,
    size_t input_length,
    vanta_setup_config_t *output);

#endif
