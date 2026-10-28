/*
 * Copyright (c) 2026 ItsDropper
 * Licensed under the MIT License.
 *
 * Deterministic tests for HTTP body size and EOF framing independent of TCP/VFS.
 */
#include "http_body.h"

#include <stdio.h>

#define LIMIT 8U

static int expect_status(const char *name, http_body_status_t actual,
                         http_body_status_t expected)
{
    if (actual == expected) return 1;
    fprintf(stderr, "%s: got %d, expected %d\n",
            name, (int)actual, (int)expected);
    return 0;
}

int main(void)
{
    http_body_t body;
    int ok = 1;

    ok &= expect_status("initialize", http_body_initialize(&body, LIMIT),
                        HTTP_BODY_OK);
    ok &= expect_status("configure exact length",
        http_body_set_content_length(&body, 1, 5U), HTTP_BODY_OK);
    ok &= expect_status("consume first chunk", http_body_consume(&body, 2U),
                        HTTP_BODY_OK);
    ok &= expect_status("consume second chunk", http_body_consume(&body, 3U),
                        HTTP_BODY_OK);
    if (http_body_bytes_received(&body) != 5U) {
        fputs("exact body byte count mismatch\n", stderr);
        ok = 0;
    }
    ok &= expect_status("exact length plus EOF", http_body_finish(&body, 1),
                        HTTP_BODY_OK);
    ok &= expect_status("consume after finish",
                        http_body_consume(&body, 1U), HTTP_BODY_BAD_ARGUMENT);

    ok &= expect_status("truncated initialize",
        http_body_initialize(&body, LIMIT), HTTP_BODY_OK);
    ok &= expect_status("truncated configure",
        http_body_set_content_length(&body, 1, 5U), HTTP_BODY_OK);
    ok &= expect_status("truncated bytes", http_body_consume(&body, 4U),
                        HTTP_BODY_OK);
    ok &= expect_status("truncated EOF mismatch",
        http_body_finish(&body, 1), HTTP_BODY_LENGTH_MISMATCH);

    ok &= expect_status("oversize initialize",
        http_body_initialize(&body, LIMIT), HTTP_BODY_OK);
    ok &= expect_status("oversize configure",
        http_body_set_content_length(&body, 1, 5U), HTTP_BODY_OK);
    ok &= expect_status("declared length overrun",
        http_body_consume(&body, 6U), HTTP_BODY_DECLARED_LENGTH_EXCEEDED);
    if (http_body_bytes_received(&body) != 0U) {
        fputs("rejected body chunk changed state\n", stderr);
        ok = 0;
    }

    ok &= expect_status("limit initialize",
        http_body_initialize(&body, LIMIT), HTTP_BODY_OK);
    ok &= expect_status("no content length",
        http_body_set_content_length(&body, 0, 0U), HTTP_BODY_OK);
    ok &= expect_status("consume up to limit", http_body_consume(&body, LIMIT),
                        HTTP_BODY_OK);
    ok &= expect_status("one byte over limit",
        http_body_consume(&body, 1U), HTTP_BODY_LIMIT_EXCEEDED);
    ok &= expect_status("limit transfer orderly EOF",
        http_body_finish(&body, 1), HTTP_BODY_OK);

    ok &= expect_status("EOF requirement initialize",
        http_body_initialize(&body, LIMIT), HTTP_BODY_OK);
    ok &= expect_status("EOF requirement configure",
        http_body_set_content_length(&body, 0, 0U), HTTP_BODY_OK);
    ok &= expect_status("non-orderly end",
        http_body_finish(&body, 0), HTTP_BODY_PEER_NOT_CLOSED);
    ok &= expect_status("finish after EOF failure",
        http_body_finish(&body, 1), HTTP_BODY_OK);

    ok &= expect_status("oversized declared length initialize",
        http_body_initialize(&body, LIMIT), HTTP_BODY_OK);
    ok &= expect_status("oversized declared length",
        http_body_set_content_length(&body, 1, LIMIT + 1U),
        HTTP_BODY_LIMIT_EXCEEDED);

    ok &= expect_status("unconfigured consume initialize",
        http_body_initialize(&body, LIMIT), HTTP_BODY_OK);
    ok &= expect_status("consume before framing",
        http_body_consume(&body, 1U), HTTP_BODY_BAD_ARGUMENT);
    ok &= expect_status("duplicate framing configuration",
        http_body_set_content_length(&body, 0, 0U), HTTP_BODY_OK);
    ok &= expect_status("second framing configuration",
        http_body_set_content_length(&body, 0, 0U), HTTP_BODY_BAD_ARGUMENT);

    /* The counter must reject UINT32 overflow without corrupting its state. */
    ok &= expect_status("uint32 boundary initialize",
        http_body_initialize(&body, UINT32_MAX), HTTP_BODY_OK);
    ok &= expect_status("uint32 boundary framing",
        http_body_set_content_length(&body, 0, 0U), HTTP_BODY_OK);
    ok &= expect_status("consume uint32 maximum",
        http_body_consume(&body, (size_t)UINT32_MAX), HTTP_BODY_OK);
    ok &= expect_status("reject uint32 overflow",
        http_body_consume(&body, 1U), HTTP_BODY_LIMIT_EXCEEDED);
    if (http_body_bytes_received(&body) != UINT32_MAX) {
        fputs("overflow rejection changed received-byte count\\n", stderr);
        ok = 0;
    }
    ok &= expect_status("finish maximum-size body",
        http_body_finish(&body, 1), HTTP_BODY_OK);

    if (!ok) return 1;
    puts("HTTP body framing tests passed (limits, Content-Length and orderly EOF).");
    return 0;
}
