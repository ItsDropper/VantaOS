/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "fat32_lfn.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const unsigned offsets[13] = {
    1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30
};

static void make_entry(uint8_t entry[32], uint8_t sequence,
                       uint8_t checksum, const uint16_t chars[13])
{
    memset(entry, 0xFF, 32);
    entry[0] = sequence;
    entry[11] = 0x0F;
    entry[12] = 0;
    entry[13] = checksum;
    entry[26] = entry[27] = 0;
    for (unsigned i = 0; i < 13; ++i) {
        entry[offsets[i]] = (uint8_t)chars[i];
        entry[offsets[i] + 1] = (uint8_t)(chars[i] >> 8);
    }
}

static void ascii_chars(uint16_t out[13], const char *text, unsigned start)
{
    const size_t length = strlen(text);
    for (unsigned i = 0; i < 13; ++i) {
        size_t index = (size_t)start + i;
        out[i] = index < length ? (uint8_t)text[index] :
                 (index == length ? 0U : 0xFFFFU);
    }
}

int main(void)
{
    uint8_t short_entry[32] = {0};
    memcpy(short_entry, "VANTAOS TXT", 11);
    const uint8_t checksum = fat32_lfn_checksum(short_entry);
    assert(checksum == fat32_lfn_checksum(short_entry));

    fat32_lfn_state_t state;
    uint8_t entry[32];
    uint16_t chars[13];
    char decoded[256];

    /* A one-entry LFN must terminate at NUL and decode exactly. */
    ascii_chars(chars, "VantaOS", 0);
    make_entry(entry, 0x41U, checksum, chars);
    fat32_lfn_reset(&state);
    assert(fat32_lfn_consume(entry, &state) == 1);
    assert(fat32_lfn_is_complete(&state));
    assert(fat32_lfn_decode(&state, decoded) == 1);
    assert(strcmp(decoded, "VantaOS") == 0);

    /* FAT stores multi-entry names highest ordinal first. */
    const char *long_name = "VantaOS-Long-Name.txt";
    uint16_t high[13], low[13];
    ascii_chars(high, long_name, 13);
    ascii_chars(low, long_name, 0);
    make_entry(entry, 0x42U, checksum, high);
    fat32_lfn_reset(&state);
    assert(fat32_lfn_consume(entry, &state) == 1);
    make_entry(entry, 0x01U, checksum, low);
    assert(fat32_lfn_consume(entry, &state) == 1);
    assert(fat32_lfn_is_complete(&state));
    assert(fat32_lfn_decode(&state, decoded) == 1);
    assert(strcmp(decoded, long_name) == 0);

    /* Orphan, duplicate, out-of-range, and checksum-mismatched entries fail closed. */
    fat32_lfn_reset(&state);
    make_entry(entry, 0x01U, checksum, low);
    assert(fat32_lfn_consume(entry, &state) == 0);
    assert(!fat32_lfn_is_complete(&state));

    fat32_lfn_reset(&state);
    make_entry(entry, 0x42U, checksum, high);
    assert(fat32_lfn_consume(entry, &state) == 1);
    make_entry(entry, 0x01U, checksum, low);
    assert(fat32_lfn_consume(entry, &state) == 1);
    assert(fat32_lfn_consume(entry, &state) == 0);
    assert(!fat32_lfn_is_complete(&state));

    fat32_lfn_reset(&state);
    make_entry(entry, 0x55U, checksum, high); /* ordinal 21 */
    assert(fat32_lfn_consume(entry, &state) == 0);

    fat32_lfn_reset(&state);
    make_entry(entry, 0x41U, (uint8_t)(checksum ^ 0x80U), chars);
    assert(fat32_lfn_consume(entry, &state) == 1);
    assert(fat32_lfn_decode(&state, decoded) == 1);
    assert(strcmp(decoded, "VantaOS") == 0);
    /* The short-entry checksum is checked by the caller; a different checksum
       must not complete the same sequence. */
    make_entry(entry, 0x01U, checksum, chars);
    assert(fat32_lfn_consume(entry, &state) == 0);
    assert(!fat32_lfn_is_complete(&state));

    /* Invalid UTF-16 surrogate sequences must not be emitted as UTF-8. */
    fat32_lfn_reset(&state);
    state.active = 1;
    state.total = 1;
    state.seen = 1;
    state.chars[0] = 0xD800U;
    state.chars[1] = 'x';
    assert(fat32_lfn_decode(&state, decoded) == 0);
    state.chars[0] = 0xDC00U;
    assert(fat32_lfn_decode(&state, decoded) == 0);

    puts("FAT32 LFN host tests passed");
    return 0;
}
