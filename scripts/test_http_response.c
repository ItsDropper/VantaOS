/*
 * Copyright (c) 2026 ItsDropper
 * Licensed under the MIT License.
 *
 * Host-side tests for strict HTTP response framing. No kernel boot or network
 * access is required; this compiles the exact parser used by the downloader.
 */
#include "http_response.h"

#include <stdio.h>
#include <string.h>

#define MAX_BODY (8U * 1024U * 1024U)

static int expect(const char *name, const char *wire,
                  http_response_status_t expected, uint16_t expected_status,
                  int has_length, uint32_t expected_length)
{
    http_response_t response = {0U, 0U, 0U};
    http_response_status_t actual =
        http_response_parse(wire, strlen(wire), MAX_BODY, &response);
    if (actual != expected ||
        (actual == HTTP_RESPONSE_OK &&
         (response.status_code != expected_status ||
          response.has_content_length != (uint8_t)has_length ||
          (has_length && response.content_length != expected_length)))) {
        fprintf(stderr,
                "%s: got parser=%d status=%u has_length=%u length=%u; expected parser=%d status=%u has_length=%d length=%u\n",
                name, (int)actual, (unsigned int)response.status_code,
                (unsigned int)response.has_content_length,
                (unsigned int)response.content_length, (int)expected,
                (unsigned int)expected_status, has_length,
                (unsigned int)expected_length);
        return 0;
    }
    return 1;
}

int main(void)
{
    int ok = 1;
    ok &= expect("HTTP/1.1 content length",
        "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\n",
        HTTP_RESPONSE_OK, 200U, 1, 5U);
    ok &= expect("case-insensitive field",
        "HTTP/1.0 200 OK\r\ncontent-length:\t0005 \r\n\r\n",
        HTTP_RESPONSE_OK, 200U, 1, 5U);
    ok &= expect("empty field section",
        "HTTP/1.1 204 No Content\r\n\r\n",
        HTTP_RESPONSE_OK, 204U, 0, 0U);
    ok &= expect("zero length",
        "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n",
        HTTP_RESPONSE_OK, 200U, 1, 0U);
    ok &= expect("substring is not a field",
        "HTTP/1.1 200 OK\r\nX-Content-Length: nope\r\n\r\n",
        HTTP_RESPONSE_OK, 200U, 0, 0U);
    ok &= expect("header-like text in value",
        "HTTP/1.1 200 OK\r\nX-Note: Transfer-Encoding: chunked\r\n\r\n",
        HTTP_RESPONSE_OK, 200U, 0, 0U);
    ok &= expect("redirect status is parsed",
        "HTTP/1.1 302 Found\r\nLocation: /new\r\n\r\n",
        HTTP_RESPONSE_OK, 302U, 0, 0U);
    ok &= expect("transfer encoding rejected",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n",
        HTTP_RESPONSE_UNSUPPORTED_TRANSFER_ENCODING, 0U, 0, 0U);
    ok &= expect("case-insensitive transfer encoding",
        "HTTP/1.1 200 OK\r\ntransfer-encoding: identity\r\n\r\n",
        HTTP_RESPONSE_UNSUPPORTED_TRANSFER_ENCODING, 0U, 0, 0U);
    ok &= expect("duplicate content length rejected",
        "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Length: 5\r\n\r\n",
        HTTP_RESPONSE_AMBIGUOUS_CONTENT_LENGTH, 0U, 0, 0U);
    ok &= expect("conflicting content length rejected",
        "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\n",
        HTTP_RESPONSE_AMBIGUOUS_CONTENT_LENGTH, 0U, 0, 0U);
    ok &= expect("comma-list rejected",
        "HTTP/1.1 200 OK\r\nContent-Length: 5, 5\r\n\r\n",
        HTTP_RESPONSE_INVALID_CONTENT_LENGTH, 0U, 0, 0U);
    ok &= expect("signed length rejected",
        "HTTP/1.1 200 OK\r\nContent-Length: +5\r\n\r\n",
        HTTP_RESPONSE_INVALID_CONTENT_LENGTH, 0U, 0, 0U);
    ok &= expect("empty length rejected",
        "HTTP/1.1 200 OK\r\nContent-Length: \r\n\r\n",
        HTTP_RESPONSE_INVALID_CONTENT_LENGTH, 0U, 0, 0U);
    ok &= expect("length over policy rejected",
        "HTTP/1.1 200 OK\r\nContent-Length: 8388609\r\n\r\n",
        HTTP_RESPONSE_CONTENT_LENGTH_TOO_LARGE, 0U, 0, 0U);
    ok &= expect("decimal overflow rejected",
        "HTTP/1.1 200 OK\r\nContent-Length: 42949672960\r\n\r\n",
        HTTP_RESPONSE_INVALID_CONTENT_LENGTH, 0U, 0, 0U);
    ok &= expect("folded header rejected",
        "HTTP/1.1 200 OK\r\nX-Test: one\r\n two\r\n\r\n",
        HTTP_RESPONSE_BAD_HEADER, 0U, 0, 0U);
    ok &= expect("invalid field name rejected",
        "HTTP/1.1 200 OK\r\nBad Header: value\r\n\r\n",
        HTTP_RESPONSE_BAD_HEADER, 0U, 0, 0U);
    ok &= expect("bare LF rejected",
        "HTTP/1.1 200 OK\nContent-Length: 5\r\n\r\n",
        HTTP_RESPONSE_BAD_STATUS_LINE, 0U, 0, 0U);
    ok &= expect("bad protocol version rejected",
        "HTTP/2 200 OK\r\n\r\n",
        HTTP_RESPONSE_BAD_STATUS_LINE, 0U, 0, 0U);
    ok &= expect("missing terminator rejected",
        "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n",
        HTTP_RESPONSE_BAD_HEADER, 0U, 0, 0U);
    ok &= expect("status reason control byte rejected",
        "HTTP/1.1 200 \x01OK\r\n\r\n",
        HTTP_RESPONSE_BAD_STATUS_LINE, 0U, 0, 0U);
    ok &= expect("header value control byte rejected",
        "HTTP/1.1 200 OK\r\nX-Test: one\x01two\r\n\r\n",
        HTTP_RESPONSE_BAD_HEADER, 0U, 0, 0U);

    if (!ok) return 1;
    puts("HTTP response parser tests passed (status, strict headers and framing).");
    return 0;
}
