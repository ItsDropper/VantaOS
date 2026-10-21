/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "vanta_setup_config.h"

static size_t text_length(const char *text)
{
    size_t length = 0U;
    if (!text)
        return 0U;
    while (text[length])
        ++length;
    return length;
}

static int text_equal(const char *a, const char *b)
{
    size_t i = 0U;
    if (!a || !b)
        return 0;
    while (a[i] && b[i] && a[i] == b[i])
        ++i;
    return a[i] == '\0' && b[i] == '\0';
}

static int slice_equal(const char *slice, size_t length, const char *text)
{
    size_t expected = text_length(text);
    if (!slice || !text || length != expected)
        return 0;
    for (size_t i = 0U; i < length; ++i)
        if (slice[i] != text[i])
            return 0;
    return 1;
}

static int copy_text(char *destination, size_t capacity,
                     const char *source, size_t length)
{
    if (!destination || !source || capacity == 0U || length >= capacity)
        return 0;
    for (size_t i = 0U; i < length; ++i)
        destination[i] = source[i];
    destination[length] = '\0';
    return 1;
}

int vanta_setup_timezone_supported(const char *timezone)
{
    return bounded_equal(timezone, VANTA_SETUP_TIMEZONE_MAX, "Etc/UTC") ||
           bounded_equal(timezone, VANTA_SETUP_TIMEZONE_MAX, "Europe/Helsinki") ||
           bounded_equal(timezone, VANTA_SETUP_TIMEZONE_MAX, "Europe/Stockholm") ||
           bounded_equal(timezone, VANTA_SETUP_TIMEZONE_MAX, "Europe/London") ||
           bounded_equal(timezone, VANTA_SETUP_TIMEZONE_MAX, "America/New_York");
}

int vanta_setup_config_valid(const vanta_setup_config_t *config)
{
    if (!config)
        return 0;
    if (!(bounded_equal(config->language, sizeof(config->language), "en") ||
          bounded_equal(config->language, sizeof(config->language), "fi")))
        return 0;
    if (!(bounded_equal(config->keyboard, sizeof(config->keyboard), "us") ||
          bounded_equal(config->keyboard, sizeof(config->keyboard), "fi")))
        return 0;
    return vanta_setup_timezone_supported(config->timezone);
}

static int append_text(char *output, size_t capacity, size_t *used,
                       const char *text)
{
    size_t length = text_length(text);
    if (!output || !used || !text || *used > capacity ||
        length > capacity - *used)
        return 0;
    for (size_t i = 0U; i < length; ++i)
        output[(*used)++] = text[i];
    return 1;
}

int vanta_setup_config_encode(
    const vanta_setup_config_t *config,
    char *output,
    size_t output_capacity,
    size_t *output_length)
{
    size_t used = 0U;
    if (!vanta_setup_config_valid(config) || !output || !output_length)
        return 0;
    if (!append_text(output, output_capacity, &used, "version=1\n") ||
        !append_text(output, output_capacity, &used, "language=") ||
        !append_text(output, output_capacity, &used, config->language) ||
        !append_text(output, output_capacity, &used, "\nkeyboard=") ||
        !append_text(output, output_capacity, &used, config->keyboard) ||
        !append_text(output, output_capacity, &used, "\ntimezone=") ||
        !append_text(output, output_capacity, &used, config->timezone) ||
        !append_text(output, output_capacity, &used, "\n"))
        return 0;
    *output_length = used;
    return 1;
}

static int parse_line(
    const char *input,
    size_t input_length,
    size_t *position,
    const char *expected_key,
    char *value,
    size_t value_capacity)
{
    size_t start;
    size_t equals;
    size_t end;

    if (!input || !position || !expected_key || !value ||
        *position >= input_length)
        return 0;
    start = *position;
    equals = start;
    while (equals < input_length && input[equals] != '=' &&
           input[equals] != '\n')
        ++equals;
    if (equals >= input_length || input[equals] != '=' ||
        !slice_equal(input + start, equals - start, expected_key))
        return 0;
    end = equals + 1U;
    while (end < input_length && input[end] != '\n')
        ++end;
    if (end >= input_length || input[end] != '\n' ||
        !copy_text(value, value_capacity, input + equals + 1U,
                   end - equals - 1U))
        return 0;
    *position = end + 1U;
    return 1;
}

int vanta_setup_config_decode(
    const char *input,
    size_t input_length,
    vanta_setup_config_t *output)
{
    size_t position = 0U;
    char version[2];
    vanta_setup_config_t parsed = {{0}, {0}, {0}};

    if (!input || !output || input_length == 0U ||
        input_length > VANTA_SETUP_CONFIG_MAX_BYTES)
        return 0;
    if (!parse_line(input, input_length, &position, "version",
                    version, sizeof(version)) ||
        !text_equal(version, "1") ||
        !parse_line(input, input_length, &position, "language",
                    parsed.language, sizeof(parsed.language)) ||
        !parse_line(input, input_length, &position, "keyboard",
                    parsed.keyboard, sizeof(parsed.keyboard)) ||
        !parse_line(input, input_length, &position, "timezone",
                    parsed.timezone, sizeof(parsed.timezone)) ||
        position != input_length || !vanta_setup_config_valid(&parsed))
        return 0;
    *output = parsed;
    return 1;
}
