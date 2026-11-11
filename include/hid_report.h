/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef HID_REPORT_H
#define HID_REPORT_H

#include <stddef.h>
#include <stdint.h>

#define HID_REPORT_MAX_IDS 32U
#define HID_REPORT_MAX_BYTES 4096U

typedef struct
{
    uint8_t report_id;
    /* Includes the leading Report ID byte when report_id is nonzero. */
    uint16_t input_bytes;
} hid_input_report_t;

typedef struct
{
    uint32_t report_count;
    hid_input_report_t reports[HID_REPORT_MAX_IDS];
} hid_report_layout_t;

/*
 * Parse a bounded HID report descriptor and calculate every input report's
 * wire length. Long items are skipped safely; malformed item streams, invalid
 * global state, mixed numbered/unnumbered input reports, and oversized
 * reports are rejected. This does not interpret usages or create input events.
 */
int hid_report_parse(const uint8_t* descriptor, size_t length,
                    hid_report_layout_t* layout);

#endif
