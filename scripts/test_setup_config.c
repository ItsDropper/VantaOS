/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "vanta_setup_config.h"

#include <assert.h>
#include <stdio.h>

static void test_round_trip(void)
{
    const vanta_setup_config_t input = {
        "fi", "fi", "Europe/Helsinki"
    };
    vanta_setup_config_t output = {{0}, {0}, {0}};
    char encoded[VANTA_SETUP_CONFIG_MAX_BYTES];
    size_t length = 0U;

    assert(vanta_setup_config_valid(&input));
    assert(vanta_setup_config_encode(
        &input, encoded, sizeof(encoded), &length));
    assert(length > 0U && length < sizeof(encoded));
    assert(encoded[length - 1U] == '\n');
    assert(vanta_setup_config_decode(encoded, length, &output));
    assert(output.language[0] == 'f' && output.language[1] == 'i');
    assert(output.keyboard[0] == 'f' && output.keyboard[1] == 'i');
    assert(output.timezone[0] == 'E');
    assert(output.timezone[14] == 'i');
}

static void test_supported_timezones(void)
{
    assert(vanta_setup_timezone_supported("Etc/UTC"));
    assert(vanta_setup_timezone_supported("Europe/Helsinki"));
    assert(vanta_setup_timezone_supported("Europe/Stockholm"));
    assert(vanta_setup_timezone_supported("Europe/London"));
    assert(vanta_setup_timezone_supported("America/New_York"));
    assert(!vanta_setup_timezone_supported("Europe/Imaginary"));
    assert(!vanta_setup_timezone_supported("../etc/passwd"));
}

static void test_rejects_invalid_or_injected_config(void)
{
    vanta_setup_config_t output = {{0}, {0}, {0}};
    const char unsupported[] =
        "version=1\nlanguage=en\nkeyboard=us\ntimezone=Europe/Imaginary\n";
    const char injected[] =
        "version=1\nlanguage=en\nkeyboard=us\ntimezone=Etc/UTC\nextra=1\n";
    const char wrong_version[] =
        "version=2\nlanguage=en\nkeyboard=us\ntimezone=Etc/UTC\n";
    assert(!vanta_setup_config_decode(unsupported, sizeof(unsupported) - 1U, &output));
    assert(!vanta_setup_config_decode(injected, sizeof(injected) - 1U, &output));
    assert(!vanta_setup_config_decode(wrong_version, sizeof(wrong_version) - 1U, &output));
    assert(!vanta_setup_config_decode("version=1\n", 10U, &output));
}

static void test_rejects_short_output_buffer(void)
{
    const vanta_setup_config_t input = {
        "en", "us", "Etc/UTC"
    };
    char output[4];
    size_t length = 0U;
    assert(!vanta_setup_config_encode(&input, output, sizeof(output), &length));
}

int main(void)
{
    test_round_trip();
    test_supported_timezones();
    test_rejects_invalid_or_injected_config();
    test_rejects_short_output_buffer();
    puts("VANTAOS_SETUP_CONFIG_OK");
    return 0;
}
