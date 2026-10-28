/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef VANTA_HTTP_URL_H
#define VANTA_HTTP_URL_H

#include <stddef.h>
#include <stdint.h>

typedef enum
{
    HTTP_URL_OK = 0,
    HTTP_URL_HTTPS_UNSUPPORTED,
    HTTP_URL_BAD_SCHEME,
    HTTP_URL_BAD_HOST,
    HTTP_URL_BAD_PORT,
    HTTP_URL_PATH_TOO_LONG,
    HTTP_URL_BAD_PATH
} http_url_status_t;

/*
 * Parse the deliberately small HTTP URL subset used by the terminal fetcher.
 * Supports DNS names/IPv4 literals, an optional decimal TCP port, path/query,
 * and fragments (which are removed). IPv6 literals and HTTPS are unsupported.
 */
http_url_status_t http_url_parse(const char *url,
                                 char *host, size_t host_capacity,
                                 char *path, size_t path_capacity,
                                 uint32_t *port);

#endif
