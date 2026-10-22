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

static int bounded_equal(const char *value, size_t capacity, const char *expected)
{
    size_t length = text_length(expected);
    if (!value || !expected || length >= capacity)
        return 0;
    for (size_t i = 0U; i < length; ++i)
        if (value[i] != expected[i])
            return 0;
    return value[length] == '\0';
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

static const char *const supported_timezones[] = {
    "Europe/Helsinki", "Etc/UTC", "Europe/Stockholm", "Europe/Oslo",
    "Europe/Copenhagen", "Europe/Berlin", "Europe/Paris", "Europe/Madrid",
    "Europe/Rome", "Europe/Amsterdam", "Europe/Brussels", "Europe/Zurich",
    "Europe/Vienna", "Europe/Warsaw", "Europe/Prague", "Europe/Budapest",
    "Europe/Athens", "Europe/Istanbul", "Europe/Lisbon", "Europe/Dublin",
    "Europe/Kyiv", "America/New_York", "America/Chicago", "America/Denver",
    "America/Los_Angeles", "America/Toronto", "America/Vancouver",
    "America/Sao_Paulo", "Asia/Tokyo", "Asia/Seoul", "Asia/Shanghai",
    "Asia/Kolkata", "Asia/Singapore", "Australia/Sydney", "Pacific/Auckland",
    "Europe/London"
};

size_t vanta_setup_timezone_count(void)
{
    return sizeof(supported_timezones) / sizeof(supported_timezones[0]);
}

const char *vanta_setup_timezone_at(size_t index)
{
    return index < vanta_setup_timezone_count() ? supported_timezones[index] : NULL;
}

int vanta_setup_timezone_supported(const char *timezone)
{
    if (!timezone)
        return 0;
    for (size_t i = 0U; i < vanta_setup_timezone_count(); ++i)
        if (bounded_equal(timezone, VANTA_SETUP_TIMEZONE_MAX,
                          supported_timezones[i]))
            return 1;
    return 0;
}

int vanta_setup_device_name_valid(const char *device_name)
{
    size_t length = text_length(device_name);
    if (!device_name || length == 0U ||
        length >= VANTA_SETUP_DEVICE_NAME_MAX)
        return 0;
    for (size_t i = 0U; i < length; ++i) {
        char ch = device_name[i];
        int alphanumeric = (ch >= 'a' && ch <= 'z') ||
                           (ch >= 'A' && ch <= 'Z') ||
                           (ch >= '0' && ch <= '9');
        if (!alphanumeric && !(ch == '-' && i > 0U && i + 1U < length))
            return 0;
    }
    return 1;
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
    return vanta_setup_timezone_supported(config->timezone) &&
           vanta_setup_device_name_valid(config->device_name);
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
    if (!append_text(output, output_capacity, &used, "version=2\n") ||
        !append_text(output, output_capacity, &used, "language=") ||
        !append_text(output, output_capacity, &used, config->language) ||
        !append_text(output, output_capacity, &used, "\nkeyboard=") ||
        !append_text(output, output_capacity, &used, config->keyboard) ||
        !append_text(output, output_capacity, &used, "\ntimezone=") ||
        !append_text(output, output_capacity, &used, config->timezone) ||
        !append_text(output, output_capacity, &used, "\ndevice_name=") ||
        !append_text(output, output_capacity, &used, config->device_name) ||
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
    vanta_setup_config_t parsed = {{0}, {0}, {0}, {0}};

    if (!input || !output || input_length == 0U ||
        input_length > VANTA_SETUP_CONFIG_MAX_BYTES)
        return 0;
    if (!parse_line(input, input_length, &position, "version",
                    version, sizeof(version)) ||
        !text_equal(version, "2") ||
        !parse_line(input, input_length, &position, "language",
                    parsed.language, sizeof(parsed.language)) ||
        !parse_line(input, input_length, &position, "keyboard",
                    parsed.keyboard, sizeof(parsed.keyboard)) ||
        !parse_line(input, input_length, &position, "timezone",
                    parsed.timezone, sizeof(parsed.timezone)) ||
        !parse_line(input, input_length, &position, "device_name",
                    parsed.device_name, sizeof(parsed.device_name)) ||
        position != input_length || !vanta_setup_config_valid(&parsed))
        return 0;
    *output = parsed;
    return 1;
}
