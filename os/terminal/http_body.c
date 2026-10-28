/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "http_body.h"

http_body_status_t http_body_initialize(http_body_t *body, uint32_t limit)
{
    if (!body || limit == 0U)
        return HTTP_BODY_BAD_ARGUMENT;
    body->limit = limit;
    body->expected_length = 0U;
    body->received_length = 0U;
    body->has_expected_length = 0U;
    body->framing_configured = 0U;
    body->finished = 0U;
    return HTTP_BODY_OK;
}

http_body_status_t http_body_set_content_length(http_body_t *body,
                                                int has_content_length,
                                                uint32_t content_length)
{
    if (!body || body->limit == 0U || body->finished ||
        body->framing_configured ||
        (has_content_length != 0 && has_content_length != 1))
        return HTTP_BODY_BAD_ARGUMENT;
    if (has_content_length && content_length > body->limit)
        return HTTP_BODY_LIMIT_EXCEEDED;
    if (has_content_length && body->received_length > content_length)
        return HTTP_BODY_DECLARED_LENGTH_EXCEEDED;

    body->has_expected_length = (uint8_t)has_content_length;
    body->expected_length = has_content_length ? content_length : 0U;
    body->framing_configured = 1U;
    return HTTP_BODY_OK;
}

http_body_status_t http_body_consume(http_body_t *body, size_t length)
{
    uint32_t next_length;
    if (!body || body->limit == 0U || !body->framing_configured ||
        body->finished)
        return HTTP_BODY_BAD_ARGUMENT;
    if (length > (size_t)(UINT32_MAX - body->received_length))
        return HTTP_BODY_LIMIT_EXCEEDED;
    next_length = body->received_length + (uint32_t)length;
    if (next_length > body->limit)
        return HTTP_BODY_LIMIT_EXCEEDED;
    if (body->has_expected_length && next_length > body->expected_length)
        return HTTP_BODY_DECLARED_LENGTH_EXCEEDED;

    /* State changes only after every bound has been checked. */
    body->received_length = next_length;
    return HTTP_BODY_OK;
}

http_body_status_t http_body_finish(http_body_t *body, int orderly_eof)
{
    if (!body || body->limit == 0U || !body->framing_configured ||
        body->finished || (orderly_eof != 0 && orderly_eof != 1))
        return HTTP_BODY_BAD_ARGUMENT;
    if (!orderly_eof)
        return HTTP_BODY_PEER_NOT_CLOSED;
    if (body->has_expected_length &&
        body->received_length != body->expected_length)
        return HTTP_BODY_LENGTH_MISMATCH;
    body->finished = 1U;
    return HTTP_BODY_OK;
}

uint32_t http_body_bytes_received(const http_body_t *body)
{
    return body ? body->received_length : 0U;
}

const char *http_body_status_string(http_body_status_t status)
{
    switch (status) {
    case HTTP_BODY_OK:
        return "ok";
    case HTTP_BODY_BAD_ARGUMENT:
        return "invalid body-framing state";
    case HTTP_BODY_LIMIT_EXCEEDED:
        return "download exceeds the configured size limit";
    case HTTP_BODY_DECLARED_LENGTH_EXCEEDED:
        return "response body exceeds Content-Length";
    case HTTP_BODY_LENGTH_MISMATCH:
        return "response body length does not match Content-Length";
    case HTTP_BODY_PEER_NOT_CLOSED:
        return "server did not close the response cleanly";
    default:
        return "unknown body-framing error";
    }
}
