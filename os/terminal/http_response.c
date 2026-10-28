/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "http_response.h"

#define HTTP_HEADER_BLOCK_MAX 2048U

static char ascii_lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

static int field_name_char(unsigned char c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9'))
        return 1;
    switch (c) {
    case '!': case '#': case '$': case '%': case '&': case 39:
    case '*': case '+': case '-': case '.': case '^': case '_':
    case '`': case '|': case '~':
        return 1;
    default:
        return 0;
    }
}

static int field_name_equals(const char *headers, size_t start,
                             size_t length, const char *expected)
{
    size_t i = 0U;
    while (expected[i]) {
        if (i >= length || ascii_lower(headers[start + i]) != expected[i])
            return 0;
        ++i;
    }
    return i == length;
}

static int parse_decimal(const char *text, size_t length, uint32_t *out)
{
    uint32_t value = 0U;
    if (!text || !out || length == 0U) return 0;
    for (size_t i = 0U; i < length; ++i) {
        uint32_t digit;
        if (text[i] < '0' || text[i] > '9') return 0;
        digit = (uint32_t)(text[i] - '0');
        if (value > (UINT32_MAX - digit) / 10U) return 0;
        value = value * 10U + digit;
    }
    *out = value;
    return 1;
}

http_response_status_t http_response_parse(const char *headers,
                                           size_t header_length,
                                           uint32_t max_body_length,
                                           http_response_t *out)
{
    size_t line_end = 0U;
    size_t at;
    int found_content_length = 0;
    uint32_t content_length = 0U;
    uint16_t status_code;

    if (!headers || !out || header_length < 4U ||
        header_length > HTTP_HEADER_BLOCK_MAX)
        return HTTP_RESPONSE_BAD_ARGUMENT;
    if (headers[header_length - 4U] != '\r' ||
        headers[header_length - 3U] != '\n' ||
        headers[header_length - 2U] != '\r' ||
        headers[header_length - 1U] != '\n')
        return HTTP_RESPONSE_BAD_HEADER;

    /* Accept only explicit HTTP/1.0 and HTTP/1.1 status lines. */
    if (header_length < 13U ||
        headers[0] != 'H' || headers[1] != 'T' ||
        headers[2] != 'T' || headers[3] != 'P' ||
        headers[4] != '/' || headers[5] != '1' ||
        headers[6] != '.' ||
        (headers[7] != '0' && headers[7] != '1') ||
        headers[8] != ' ' ||
        headers[9] < '0' || headers[9] > '9' ||
        headers[10] < '0' || headers[10] > '9' ||
        headers[11] < '0' || headers[11] > '9' ||
        (headers[12] != ' ' && headers[12] != '\r'))
        return HTTP_RESPONSE_BAD_STATUS_LINE;

    status_code = (uint16_t)((headers[9] - '0') * 100 +
                             (headers[10] - '0') * 10 +
                             (headers[11] - '0'));
    for (line_end = 0U; line_end + 1U < header_length; ++line_end) {
        if (headers[line_end] == '\n' &&
            (line_end == 0U || headers[line_end - 1U] != '\r'))
            return HTTP_RESPONSE_BAD_STATUS_LINE;
        if (headers[line_end] == '\r' &&
            headers[line_end + 1U] != '\n')
            return HTTP_RESPONSE_BAD_STATUS_LINE;
        if (headers[line_end] == '\r' && headers[line_end + 1U] == '\n')
            break;
    }
    if (line_end + 1U >= header_length)
        return HTTP_RESPONSE_BAD_STATUS_LINE;
    for (size_t i = 12U; i < line_end; ++i) {
        unsigned char ch = (unsigned char)headers[i];
        if ((ch < 0x20U && ch != '\t') || ch == 0x7FU)
            return HTTP_RESPONSE_BAD_STATUS_LINE;
    }

    at = line_end + 2U;
    while (at < header_length) {
        size_t end = at;
        if (at == header_length - 2U &&
            headers[at] == '\r' && headers[at + 1U] == '\n') {
            out->status_code = status_code;
            out->has_content_length = (uint8_t)found_content_length;
            out->content_length = content_length;
            return HTTP_RESPONSE_OK;
        }
        size_t colon;
        size_t value_start, value_end;

        while (end + 1U < header_length &&
               !(headers[end] == '\r' && headers[end + 1U] == '\n')) {
            if (headers[end] == '\n' ||
                (headers[end] == '\r' && headers[end + 1U] != '\n'))
                return HTTP_RESPONSE_BAD_HEADER;
            ++end;
        }
        if (end + 1U >= header_length)
            return HTTP_RESPONSE_BAD_HEADER;
        if (end == at) {
            if (end + 4U != header_length)
                return HTTP_RESPONSE_BAD_HEADER;
            out->status_code = status_code;
            out->has_content_length = (uint8_t)found_content_length;
            out->content_length = content_length;
            return HTTP_RESPONSE_OK;
        }
        /* Obsolete folded fields are rejected, not silently reinterpreted. */
        if (headers[at] == ' ' || headers[at] == '\t')
            return HTTP_RESPONSE_BAD_HEADER;
        colon = at;
        while (colon < end && headers[colon] != ':') {
            if (!field_name_char((unsigned char)headers[colon]))
                return HTTP_RESPONSE_BAD_HEADER;
            ++colon;
        }
        if (colon == at || colon == end)
            return HTTP_RESPONSE_BAD_HEADER;

        value_start = colon + 1U;
        while (value_start < end &&
               (headers[value_start] == ' ' || headers[value_start] == '\t'))
            ++value_start;
        value_end = end;
        while (value_end > value_start &&
               (headers[value_end - 1U] == ' ' ||
                headers[value_end - 1U] == '\t'))
            --value_end;
        for (size_t i = value_start; i < value_end; ++i) {
            unsigned char c = (unsigned char)headers[i];
            if ((c < 0x20U && c != '\t') || c == 0x7FU)
                return HTTP_RESPONSE_BAD_HEADER;
        }

        if (field_name_equals(headers, at, colon - at,
                              "transfer-encoding"))
            return HTTP_RESPONSE_UNSUPPORTED_TRANSFER_ENCODING;
        if (field_name_equals(headers, at, colon - at, "content-length")) {
            uint32_t parsed;
            if (found_content_length)
                return HTTP_RESPONSE_AMBIGUOUS_CONTENT_LENGTH;
            if (!parse_decimal(headers + value_start,
                               value_end - value_start, &parsed))
                return HTTP_RESPONSE_INVALID_CONTENT_LENGTH;
            if (parsed > max_body_length)
                return HTTP_RESPONSE_CONTENT_LENGTH_TOO_LARGE;
            found_content_length = 1;
            content_length = parsed;
        }
        at = end + 2U;
    }
    return HTTP_RESPONSE_BAD_HEADER;
}
