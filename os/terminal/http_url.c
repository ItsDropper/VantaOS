/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "http_url.h"

static int starts_with(const char *text, const char *prefix)
{
    size_t i = 0U;
    while (prefix[i])
    {
        if (text[i] != prefix[i]) return 0;
        ++i;
    }
    return 1;
}

http_url_status_t http_url_parse(const char *url,
                                 char *host, size_t host_capacity,
                                 char *path, size_t path_capacity,
                                 uint32_t *port)
{
    if (!url || !host || host_capacity < 2U || !path ||
        path_capacity < 2U || !port)
        return HTTP_URL_BAD_SCHEME;

    if (starts_with(url, "https://"))
        return HTTP_URL_HTTPS_UNSUPPORTED;
    if (!starts_with(url, "http://"))
        return HTTP_URL_BAD_SCHEME;

    const char *authority = url + 7U;
    const char *cursor = authority;
    const char *port_start = 0;
    while (*cursor && *cursor != '/' && *cursor != '?' && *cursor != '#')
    {
        if (*cursor == ':')
        {
            port_start = cursor + 1U;
            break;
        }
        ++cursor;
    }

    const char *path_start = cursor;
    size_t host_length;
    uint32_t parsed_port = 80U;
    if (port_start)
    {
        path_start = port_start;
        while (*path_start && *path_start != '/' &&
               *path_start != '?' && *path_start != '#')
            ++path_start;
        size_t digits = (size_t)(path_start - port_start);
        if (!digits || digits > 5U)
            return HTTP_URL_BAD_PORT;
        parsed_port = 0U;
        for (size_t i = 0U; i < digits; ++i)
        {
            char ch = port_start[i];
            if (ch < '0' || ch > '9')
                return HTTP_URL_BAD_PORT;
            uint32_t digit = (uint32_t)(ch - '0');
            if (parsed_port > (65535U - digit) / 10U)
                return HTTP_URL_BAD_PORT;
            parsed_port = parsed_port * 10U + digit;
        }
        if (!parsed_port || parsed_port > 65535U)
            return HTTP_URL_BAD_PORT;
        host_length = (size_t)((port_start - 1U) - authority);
    }
    else
    {
        host_length = (size_t)(cursor - authority);
    }

    if (!host_length || host_length >= host_capacity)
        return HTTP_URL_BAD_HOST;
    for (size_t i = 0U; i < host_length; ++i)
    {
        char ch = authority[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '.' || ch == '-'))
            return HTTP_URL_BAD_HOST;
        host[i] = ch;
    }
    host[host_length] = '\0';
    size_t label_start = 0U;
    for (size_t i = 0U; i <= host_length; ++i)
    {
        if (i != host_length && host[i] != '.')
            continue;
        size_t label_length = i - label_start;
        if (!label_length || label_length > 63U ||
            host[label_start] == '-' || host[i - 1U] == '-')
            return HTTP_URL_BAD_HOST;
        label_start = i + 1U;
    }

    size_t path_length = 0U;
    if (*path_start == '#')
        path_start = "/";
    if (*path_start == '?' || *path_start == '\0')
    {
        path[path_length++] = '/';
        if (*path_start == '?')
        {
            size_t q = 0U;
            while (path_start[q] && path_start[q] != '#')
            {
                if (path_length >= path_capacity - 1U)
                    return HTTP_URL_PATH_TOO_LONG;
                path[path_length++] = path_start[q++];
            }
        }
    }
    else
    {
        while (path_start[path_length] && path_start[path_length] != '#')
        {
            if (path_length >= path_capacity - 1U)
                return HTTP_URL_PATH_TOO_LONG;
            path[path_length] = path_start[path_length];
            ++path_length;
        }
    }
    path[path_length] = '\0';

    for (size_t i = 0U; i < path_length; ++i)
    {
        unsigned char ch = (unsigned char)path[i];
        if (ch <= 0x20U || ch == 0x7FU)
            return HTTP_URL_BAD_PATH;
    }

    *port = parsed_port;
    return HTTP_URL_OK;
}
