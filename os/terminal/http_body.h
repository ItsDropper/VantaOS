/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef VANTAOS_HTTP_BODY_H
#define VANTAOS_HTTP_BODY_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    HTTP_BODY_OK = 0,
    HTTP_BODY_BAD_ARGUMENT,
    HTTP_BODY_LIMIT_EXCEEDED,
    HTTP_BODY_DECLARED_LENGTH_EXCEEDED,
    HTTP_BODY_LENGTH_MISMATCH,
    HTTP_BODY_PEER_NOT_CLOSED
} http_body_status_t;

typedef struct {
    uint32_t limit;
    uint32_t expected_length;
    uint32_t received_length;
    uint8_t has_expected_length;
    uint8_t framing_configured;
    uint8_t finished;
} http_body_t;

http_body_status_t http_body_initialize(http_body_t *body, uint32_t limit);
http_body_status_t http_body_set_content_length(http_body_t *body,
                                                int has_content_length,
                                                uint32_t content_length);
http_body_status_t http_body_consume(http_body_t *body, size_t length);
/* Exact Content-Length completes framing; otherwise orderly EOF is required. */
http_body_status_t http_body_finish(http_body_t *body, int orderly_eof);
uint32_t http_body_bytes_received(const http_body_t *body);
const char *http_body_status_string(http_body_status_t status);

#endif
