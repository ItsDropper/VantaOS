/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef HID_REPORT_H
#define HID_REPORT_H

#include <stddef.h>
#include <stdint.h>

#define HID_REPORT_MAX_IDS 32U
#define HID_REPORT_MAX_BYTES 4096U
#define HID_REPORT_MAX_FIELDS 64U

typedef struct
{
    uint8_t report_id;
    uint16_t input_bytes;
} hid_input_report_t;

typedef struct
{
    uint8_t report_id;
    uint16_t usage_page;
    uint16_t usage_min;
    uint16_t usage_max;
    uint16_t bit_offset;
    uint16_t report_count;
    uint8_t bit_size;
    uint8_t flags;
    int32_t logical_min;
} hid_input_field_t;

typedef struct
{
    uint32_t report_count;
    hid_input_report_t reports[HID_REPORT_MAX_IDS];
    uint32_t field_count;
    hid_input_field_t fields[HID_REPORT_MAX_FIELDS];
    uint16_t top_usage_page;
    uint16_t top_usage;
    uint8_t has_keyboard;
    uint8_t has_mouse;
    uint8_t has_touchpad;
} hid_report_layout_t;

/*
 * Bounded HID short/long item parser. Produces input report lengths and the
 * usage/bit-field metadata needed by the conservative keyboard/pointer path.
 * It does not interpret arbitrary vendor-defined usages or output reports.
 */
int hid_report_parse(const uint8_t* descriptor, size_t length,
                    hid_report_layout_t* layout);

#endif
