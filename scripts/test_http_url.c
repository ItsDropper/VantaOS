/*
 * Copyright (c) 2026 ItsDropper
 * Licensed under the MIT License.
 *
 * Host regression tests for the exact HTTP URL parser used by the terminal
 * downloader. These tests require neither a kernel boot nor network access.
 */
#include "http_url.h"

#include <stdio.h>
#include <string.h>

static int expect_ok(const char *url, const char *expected_host,
                     const char *expected_path, uint32_t expected_port)
{
    char host[96];
    char path[96];
    uint32_t port = 0U;
    http_url_status_t status = http_url_parse(
        url, host, sizeof(host), path, sizeof(path), &port);
    if (status != HTTP_URL_OK || strcmp(host, expected_host) != 0 ||
        strcmp(path, expected_path) != 0 || port != expected_port)
    {
        fprintf(stderr, "HTTP URL parse failed: '%s' status=%d host='%s' path='%s' port=%u\n",
                url, (int)status, status == HTTP_URL_OK ? host : "",
                status == HTTP_URL_OK ? path : "", (unsigned int)port);
        return 0;
    }
    return 1;
}

static int expect_status(const char *url, http_url_status_t expected)
{
    char host[96];
    char path[96];
    uint32_t port = 0U;
    http_url_status_t actual = http_url_parse(
        url, host, sizeof(host), path, sizeof(path), &port);
    if (actual != expected)
    {
        fprintf(stderr, "HTTP URL status mismatch: '%s' got=%d expected=%d\n",
                url, (int)actual, (int)expected);
        return 0;
    }
    return 1;
}

int main(void)
{
    char long_url[160];
    int ok = 1;

    ok &= expect_ok("http://example.com", "example.com", "/", 80U);
    ok &= expect_ok("http://example.com/path?q=one#section",
                    "example.com", "/path?q=one", 80U);
    ok &= expect_ok("http://127.0.0.1:8080/?x=1",
                    "127.0.0.1", "/?x=1", 8080U);
    ok &= expect_ok("http://example.com:65535/file",
                    "example.com", "/file", 65535U);
    ok &= expect_ok("http://example.com?x=1",
                    "example.com", "/?x=1", 80U);
    ok &= expect_ok("http://example.com#fragment",
                    "example.com", "/", 80U);

    ok &= expect_status("https://example.com/file",
                        HTTP_URL_HTTPS_UNSUPPORTED);
    ok &= expect_status("ftp://example.com/file", HTTP_URL_BAD_SCHEME);
    ok &= expect_status("http:///missing-host", HTTP_URL_BAD_HOST);
    ok &= expect_status("http://example.com:0/", HTTP_URL_BAD_PORT);
    ok &= expect_status("http://example.com:65536/", HTTP_URL_BAD_PORT);
    ok &= expect_status("http://example.com:abc/", HTTP_URL_BAD_PORT);
    ok &= expect_status("http://example.com:/", HTTP_URL_BAD_PORT);
    ok &= expect_status("http://bad host/file", HTTP_URL_BAD_HOST);
    ok &= expect_status("http://user@example.com/", HTTP_URL_BAD_HOST);
    ok &= expect_status("http://[::1]/", HTTP_URL_BAD_HOST);
    ok &= expect_status("http://example.com/a b", HTTP_URL_BAD_PATH);

    (void)strcpy(long_url, "http://example.com/");
    size_t n = strlen(long_url);
    while (n + 1U < sizeof(long_url)) long_url[n++] = 'a';
    long_url[n] = '\0';
    ok &= expect_status(long_url, HTTP_URL_PATH_TOO_LONG);

    if (!ok) return 1;
    puts("HTTP URL parser tests passed (scheme, host, port, path, query, fragments and bounds).");
    return 0;
}
