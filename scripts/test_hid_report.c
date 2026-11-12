/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "hid_report.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void)
{
    /* Generic Desktop / Keyboard, 8-byte unnumbered input report. */
    static const uint8_t keyboard[] = {
        0x05,0x01, 0x09,0x06, 0xA1,0x01,
        0x05,0x07, 0x19,0xE0, 0x29,0xE7,
        0x15,0x00, 0x25,0x01, 0x75,0x01, 0x95,0x08, 0x81,0x02,
        0x95,0x01, 0x75,0x08, 0x81,0x01,
        0x95,0x06, 0x75,0x08, 0x15,0x00, 0x25,0x65,
        0x05,0x07, 0x19,0x00, 0x29,0x65, 0x81,0x00,
        0xC0
    };
    hid_report_layout_t layout;
    assert(hid_report_parse(keyboard, sizeof(keyboard), &layout));
    assert(layout.report_count == 1U);
    assert(layout.reports[0].report_id == 0U);
    assert(layout.reports[0].input_bytes == 8U);
    assert(layout.has_keyboard == 1U);
    assert(layout.fields[0].usage_page == 0x07U);
    assert(layout.fields[0].usage_min == 0xE0U);
    assert(layout.fields[0].usage_max == 0xE7U);

    /* Report ID 3 adds one byte to the 8-byte payload. */
    static const uint8_t numbered[] = {
        0x85,0x03, 0x75,0x08, 0x95,0x08, 0x81,0x02
    };
    assert(hid_report_parse(numbered, sizeof(numbered), &layout));
    assert(layout.report_count == 1U);
    assert(layout.reports[0].report_id == 3U);
    assert(layout.reports[0].input_bytes == 9U);

    /* Global Push/Pop restores report size and count before the next Input. */
    static const uint8_t pushed[] = {
        0x75,0x08, 0x95,0x02, 0xA4,
        0x75,0x01, 0x95,0x04, 0x81,0x02,
        0xB4, 0x81,0x02
    };
    assert(hid_report_parse(pushed, sizeof(pushed), &layout));
    assert(layout.reports[0].input_bytes == 3U);

    static const uint8_t truncated_short[] = {0x75};
    static const uint8_t truncated_long[] = {0xFE,0x04,0x01,0xAA};
    static const uint8_t invalid_report_id[] = {0x85,0x00,0x75,0x08,0x95,0x01,0x81,0x02};
    static const uint8_t zero_size[] = {0x75,0x00,0x95,0x01,0x81,0x02};
    static const uint8_t unmatched_collection[] = {0xC0};
    static const uint8_t unclosed_collection[] = {0xA1,0x01};
    static const uint8_t mixed_ids[] = {
        0x75,0x08,0x95,0x01,0x81,0x02,
        0x85,0x01,0x81,0x02
    };
    static const uint8_t huge_report[] = {
        0x75,0x20,0x96,0x01,0x04,0x81,0x02
    };
    assert(!hid_report_parse(truncated_short, sizeof(truncated_short), &layout));
    assert(!hid_report_parse(truncated_long, sizeof(truncated_long), &layout));
    assert(!hid_report_parse(invalid_report_id, sizeof(invalid_report_id), &layout));
    assert(!hid_report_parse(zero_size, sizeof(zero_size), &layout));
    assert(!hid_report_parse(unmatched_collection, sizeof(unmatched_collection), &layout));
    assert(!hid_report_parse(unclosed_collection, sizeof(unclosed_collection), &layout));
    assert(!hid_report_parse(mixed_ids, sizeof(mixed_ids), &layout));
    assert(!hid_report_parse(huge_report, sizeof(huge_report), &layout));
    assert(!hid_report_parse(NULL, 1U, &layout));
    assert(!hid_report_parse(keyboard, 0U, &layout));
    assert(!hid_report_parse(keyboard, sizeof(keyboard), NULL));

    /* A 32-bit Usage item embeds the Usage Page in its upper 16 bits. */
    static const uint8_t embedded_page[] = {
        0x0B,0x06,0x00,0x01,0x00, 0xA1,0x01,
        0x05,0x07, 0x19,0xE0, 0x29,0xE7,
        0x15,0x00, 0x25,0x01, 0x75,0x01, 0x95,0x08, 0x81,0x02,
        0xC0
    };
    assert(hid_report_parse(embedded_page,sizeof(embedded_page),&layout));
    assert(layout.has_keyboard==1U);
    assert(layout.fields[0].usage_page==0x07U);
    static const uint8_t touch_collections[]={
        0x05,0x0D,0x09,0x05,0xA1,0x01,
        0x09,0x22,0xA1,0x02,
        0x05,0x01,0x09,0x30,0x15,0x00,0x25,0x7F,
        0x75,0x08,0x95,0x01,0x81,0x02,0xC0,
        0x05,0x0D,0x09,0x22,0xA1,0x02,
        0x05,0x01,0x09,0x30,0x15,0x00,0x25,0x7F,
        0x75,0x08,0x95,0x01,0x81,0x02,0xC0,0xC0
    };
    assert(hid_report_parse(touch_collections,sizeof(touch_collections),&layout));
    assert(layout.has_touchpad==1U&&layout.field_count==2U);
    assert(layout.fields[0].collection_id!=0U);
    assert(layout.fields[0].collection_id!=layout.fields[1].collection_id);
    assert(layout.fields[0].logical_max==127);
    assert(layout.fields[1].logical_max==127);

    puts("HID report descriptor sizing and contact-collection tests passed");
    return 0;
}
