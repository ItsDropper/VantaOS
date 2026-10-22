/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "vanta_setup_config.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_round_trip(void)
{
    const vanta_setup_config_t input = {
        "fi", "fi", "Europe/Helsinki", "VANTA-TEST-PC"
    };
    vanta_setup_config_t output = {{0}, {0}, {0}, {0}};
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
    assert(strcmp(output.device_name, "VANTA-TEST-PC") == 0);
}

static void test_supported_timezones(void)
{
    assert(vanta_setup_timezone_count() == 36U);
    assert(vanta_setup_timezone_at(0U) != NULL);
    assert(vanta_setup_timezone_at(36U) == NULL);
    assert(strcmp(vanta_setup_timezone_at(0U), "Europe/Helsinki") == 0);
    assert(strcmp(vanta_setup_timezone_at(1U), "Etc/UTC") == 0);
    assert(strcmp(vanta_setup_timezone_at(35U), "Europe/London") == 0);
    assert(vanta_setup_timezone_supported(vanta_setup_timezone_at(0U)));
    assert(vanta_setup_timezone_supported(vanta_setup_timezone_at(35U)));
    assert(vanta_setup_timezone_supported("Europe/Helsinki"));
    assert(vanta_setup_timezone_supported("Etc/UTC"));
    assert(vanta_setup_timezone_supported("Europe/London"));
    for (size_t i = 0U; i < vanta_setup_timezone_count(); ++i)
        assert(vanta_setup_timezone_supported(vanta_setup_timezone_at(i)));
    assert(vanta_setup_timezone_supported("Etc/UTC"));
    assert(vanta_setup_timezone_supported("Europe/Helsinki"));
    assert(vanta_setup_timezone_supported("Europe/Stockholm"));
    assert(vanta_setup_timezone_supported("Europe/London"));
    assert(vanta_setup_timezone_supported("America/New_York"));
    assert(vanta_setup_timezone_supported("America/Chicago"));
    assert(vanta_setup_timezone_supported("America/Denver"));
    assert(vanta_setup_timezone_supported("America/Los_Angeles"));
    assert(vanta_setup_timezone_supported("America/Toronto"));
    assert(vanta_setup_timezone_supported("America/Vancouver"));
    assert(vanta_setup_timezone_supported("America/Sao_Paulo"));
    assert(vanta_setup_timezone_supported("Europe/Oslo"));
    assert(vanta_setup_timezone_supported("Europe/Copenhagen"));
    assert(vanta_setup_timezone_supported("Europe/Berlin"));
    assert(vanta_setup_timezone_supported("Europe/Paris"));
    assert(vanta_setup_timezone_supported("Europe/Madrid"));
    assert(vanta_setup_timezone_supported("Europe/Rome"));
    assert(vanta_setup_timezone_supported("Europe/Amsterdam"));
    assert(vanta_setup_timezone_supported("Europe/Brussels"));
    assert(vanta_setup_timezone_supported("Europe/Zurich"));
    assert(vanta_setup_timezone_supported("Europe/Vienna"));
    assert(vanta_setup_timezone_supported("Europe/Warsaw"));
    assert(vanta_setup_timezone_supported("Europe/Prague"));
    assert(vanta_setup_timezone_supported("Europe/Budapest"));
    assert(vanta_setup_timezone_supported("Europe/Athens"));
    assert(vanta_setup_timezone_supported("Europe/Istanbul"));
    assert(vanta_setup_timezone_supported("Europe/Lisbon"));
    assert(vanta_setup_timezone_supported("Europe/Dublin"));
    assert(vanta_setup_timezone_supported("Europe/Kyiv"));
    assert(vanta_setup_timezone_supported("Asia/Tokyo"));
    assert(vanta_setup_timezone_supported("Asia/Seoul"));
    assert(vanta_setup_timezone_supported("Asia/Shanghai"));
    assert(vanta_setup_timezone_supported("Asia/Kolkata"));
    assert(vanta_setup_timezone_supported("Asia/Singapore"));
    assert(vanta_setup_timezone_supported("Australia/Sydney"));
    assert(vanta_setup_timezone_supported("Pacific/Auckland"));
    assert(!vanta_setup_timezone_supported("Europe/Imaginary"));
    assert(!vanta_setup_timezone_supported("../etc/passwd"));
    assert(vanta_setup_device_name_valid("VANTA-PC"));
    assert(vanta_setup_device_name_valid("desktop-01"));
    assert(!vanta_setup_device_name_valid(""));
    assert(!vanta_setup_device_name_valid("-VANTA"));
    assert(!vanta_setup_device_name_valid("VANTA-"));
    assert(!vanta_setup_device_name_valid("VANTA PC"));
    assert(!vanta_setup_device_name_valid("../etc"));
}

static void test_rejects_invalid_or_injected_config(void)
{
    vanta_setup_config_t output = {{0}, {0}, {0}, {0}};
    const char unsupported[] =
        "version=2\nlanguage=en\nkeyboard=us\ntimezone=Europe/Imaginary\ndevice_name=VANTA-PC\n";
    const char injected[] =
        "version=2\nlanguage=en\nkeyboard=us\ntimezone=Etc/UTC\ndevice_name=VANTA-PC\nextra=1\n";
    const char wrong_version[] =
        "version=1\nlanguage=en\nkeyboard=us\ntimezone=Etc/UTC\ndevice_name=VANTA-PC\n";
    assert(!vanta_setup_config_decode(unsupported, sizeof(unsupported) - 1U, &output));
    assert(!vanta_setup_config_decode(injected, sizeof(injected) - 1U, &output));
    assert(!vanta_setup_config_decode(wrong_version, sizeof(wrong_version) - 1U, &output));
    assert(!vanta_setup_config_decode("version=2\n", 10U, &output));
}

static void test_rejects_short_output_buffer(void)
{
    const vanta_setup_config_t input = {
        "en", "us", "Etc/UTC", "VANTA-PC"
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
