#include "wifi_crypto.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int from_hex(const char *hex, uint8_t *out, size_t length)
{
    for (size_t i = 0U; i < length; ++i) {
        unsigned int value;
        if (sscanf(hex + i * 2U, "%2x", &value) != 1)
            return 0;
        out[i] = (uint8_t)value;
    }
    return 1;
}

int main(void)
{
    static const uint8_t password[] = "password";
    static const uint8_t salt[] = "salt";
    static const char *expected_one =
        "0c60c80f961f0e71f3a9b524af6012062fe037a6";
    static const char *expected_two =
        "ea6c014dc72d6f8ccd1ed92ace1d41f0d8de8957";
    uint8_t output[20];
    uint8_t expected[20];

    if (!from_hex(expected_one, expected, sizeof(expected)) ||
        !wifi_pbkdf2_sha1(password, sizeof(password) - 1U,
                          salt, sizeof(salt) - 1U, 1U,
                          output, sizeof(output)) ||
        memcmp(output, expected, sizeof(output)) != 0)
        return 1;

    if (!from_hex(expected_two, expected, sizeof(expected)) ||
        !wifi_pbkdf2_sha1(password, sizeof(password) - 1U,
                          salt, sizeof(salt) - 1U, 2U,
                          output, sizeof(output)) ||
        memcmp(output, expected, sizeof(output)) != 0)
        return 2;

    memset(output, 0xA5, sizeof(output));
    if (wifi_pbkdf2_sha1(NULL, sizeof(password) - 1U,
                         salt, sizeof(salt) - 1U, 1U,
                         output, sizeof(output)) != 0)
        return 3;
    for (size_t i = 0U; i < sizeof(output); ++i)
        if (output[i] != 0xA5U)
            return 4;

    if (wifi_pbkdf2_sha1(password, sizeof(password) - 1U,
                         salt, sizeof(salt) - 1U, 4097U,
                         output, sizeof(output)) != 0 ||
        wifi_pbkdf2_sha1(password, sizeof(password) - 1U,
                         salt, sizeof(salt) - 1U, 1U,
                         output, 65U) != 0)
        return 5;

    puts("Wi-Fi PBKDF2-HMAC-SHA1 test vectors passed.");
    return 0;
}
