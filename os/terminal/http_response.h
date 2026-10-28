/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef VANTAOS_HTTP_RESPONSE_H
#define VANTAOS_HTTP_RESPONSE_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    HTTP_RESPONSE_OK = 0,
    HTTP_RESPONSE_BAD_ARGUMENT,
    HTTP_RESPONSE_BAD_STATUS_LINE,
    HTTP_RESPONSE_BAD_HEADER,
    HTTP_RESPONSE_UNSUPPORTED_TRANSFER_ENCODING,
    HTTP_RESPONSE_INVALID_CONTENT_LENGTH,
    HTTP_RESPONSE_CONTENT_LENGTH_TOO_LARGE,
    HTTP_RESPONSE_AMBIGUOUS_CONTENT_LENGTH
} http_response_status_t;

typedef struct {
    uint16_t status_code;
    uint8_t has_content_length;
    uint32_t content_length;
} http_response_t;

/*
 * Parse a complete HTTP/1.0 or HTTP/1.1 response header block.
 *
 * header_length must include the terminating CRLF CRLF. This deliberately
 * accepts only unambiguous framing: any Transfer-Encoding field is rejected,
 * and Content-Length may appear at most once. The caller owns header storage.
 * max_body_length bounds a declared Content-Length before any body is written.
 */
http_response_status_t http_response_parse(const char *headers,
                                           size_t header_length,
                                           uint32_t max_body_length,
                                           http_response_t *out);

#endif
