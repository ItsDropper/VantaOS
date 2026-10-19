/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "fat32_lfn.h"

static uint16_t lfn_read_u16(const uint8_t* bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

uint8_t fat32_lfn_checksum(const uint8_t* short_entry)
{
    uint8_t sum = 0;
    for (unsigned int i = 0; i < 11; i++)
        sum = (uint8_t)(((sum & 1U) ? 0x80U : 0U) +
                        (sum >> 1) + short_entry[i]);
    return sum;
}

void fat32_lfn_reset(fat32_lfn_state_t* state)
{
    if (!state)
        return;
    for (unsigned int i = 0; i < FAT32_LFN_CHARS; i++)
        state->chars[i] = 0xFFFFU;
    state->active = 0;
    state->expected_checksum = 0;
    state->total = 0;
    state->seen = 0;
}

int fat32_lfn_consume(const uint8_t* entry, fat32_lfn_state_t* state)
{
    if (!entry || !state)
        return 0;

    uint8_t sequence = entry[0];
    uint8_t ordinal = sequence & 0x1FU;
    int is_last = (sequence & 0x40U) != 0;

    if (ordinal == 0 || ordinal > FAT32_LFN_MAX_ENTRIES ||
        entry[11] != 0x0FU || entry[12] != 0 ||
        lfn_read_u16(&entry[26]) != 0)
    {
        fat32_lfn_reset(state);
        return 0;
    }

    if (is_last)
    {
        fat32_lfn_reset(state);
        state->active = 1;
        state->expected_checksum = entry[13];
        state->total = ordinal;
    }

    if (!state->active || ordinal > state->total ||
        entry[13] != state->expected_checksum ||
        (state->seen & (1U << (ordinal - 1U))) != 0)
    {
        fat32_lfn_reset(state);
        return 0;
    }

    static const uint8_t offsets[13] = {
        1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30
    };
    unsigned int base = (unsigned int)(ordinal - 1U) * 13U;
    for (unsigned int i = 0; i < 13; i++)
        state->chars[base + i] = lfn_read_u16(&entry[offsets[i]]);
    state->seen |= 1U << (ordinal - 1U);
    return 1;
}

static unsigned int lfn_emit_utf8(uint32_t cp, char* out,
                                  unsigned int pos)
{
    if (cp <= 0x7FU)
    {
        if (pos < 255U) out[pos++] = (char)cp;
    }
    else if (cp <= 0x7FFU)
    {
        if (pos + 2U <= 255U)
        {
            out[pos++] = (char)(0xC0U | (cp >> 6));
            out[pos++] = (char)(0x80U | (cp & 0x3FU));
        }
    }
    else if (cp <= 0xFFFFU)
    {
        if (pos + 3U <= 255U)
        {
            out[pos++] = (char)(0xE0U | (cp >> 12));
            out[pos++] = (char)(0x80U | ((cp >> 6) & 0x3FU));
            out[pos++] = (char)(0x80U | (cp & 0x3FU));
        }
    }
    else if (cp <= 0x10FFFFU)
    {
        if (pos + 4U <= 255U)
        {
            out[pos++] = (char)(0xF0U | (cp >> 18));
            out[pos++] = (char)(0x80U | ((cp >> 12) & 0x3FU));
            out[pos++] = (char)(0x80U | ((cp >> 6) & 0x3FU));
            out[pos++] = (char)(0x80U | (cp & 0x3FU));
        }
    }
    return pos;
}

int fat32_lfn_decode(const fat32_lfn_state_t* state, char* name)
{
    if (!state || !name)
        return 0;
    unsigned int pos = 0;
    for (unsigned int i = 0; i < FAT32_LFN_CHARS; i++)
    {
        uint32_t cp = state->chars[i];
        if (cp == 0 || cp == 0xFFFFU)
            break;

        if (cp >= 0xD800U && cp <= 0xDBFFU)
        {
            if (i + 1U >= FAT32_LFN_CHARS ||
                state->chars[i + 1U] < 0xDC00U ||
                state->chars[i + 1U] > 0xDFFFU)
                return 0;
            cp = 0x10000U + ((cp - 0xD800U) << 10) +
                 ((uint32_t)state->chars[++i] - 0xDC00U);
        }
        else if (cp >= 0xDC00U && cp <= 0xDFFFU)
            return 0;

        unsigned int next = lfn_emit_utf8(cp, name, pos);
        if (next == pos)
            break;
        pos = next;
    }
    name[pos] = 0;
    return pos != 0;
}

int fat32_lfn_is_complete(const fat32_lfn_state_t* state)
{
    if (!state || state->total == 0 ||
        state->total > FAT32_LFN_MAX_ENTRIES)
        return 0;
    return state->seen == ((1U << state->total) - 1U);
}
