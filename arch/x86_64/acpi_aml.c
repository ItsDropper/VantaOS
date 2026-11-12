/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "acpi_aml.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define AML_MAX_DEPTH 16U
#define AML_MAX_DEVICES 64U
#define AML_MAX_TABLE_BYTES (16U * 1024U * 1024U)

typedef struct
{
    uint8_t kind;
    uint64_t integer;
    const uint8_t* bytes;
    uint32_t length;
    uint32_t package_count;
    const uint8_t* package_begin;
    const uint8_t* package_end;
} aml_value_t;

enum
{
    AML_VALUE_INTEGER = 1,
    AML_VALUE_STRING,
    AML_VALUE_BUFFER,
    AML_VALUE_PACKAGE,
    AML_VALUE_REFERENCE
};

typedef struct
{
    acpi_aml_device_callback_t callback;
    void* callback_context;
    uint32_t devices_found;
} aml_parser_t;

static uint16_t aml_u16(const uint8_t* p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8U));
}

static uint32_t aml_u32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

static int aml_read_package(const uint8_t** cursor, const uint8_t* end,
                            const uint8_t** body, const uint8_t** package_end)
{
    const uint8_t* start = *cursor;
    if (start >= end)
        return 0;
    uint8_t lead = *(*cursor)++;
    uint32_t follow = (uint32_t)(lead >> 6U);
    uint32_t length;
    if (follow == 0U)
        length = lead & 0x3FU;
    else
    {
        if ((uint32_t)(end - *cursor) < follow)
            return 0;
        length = lead & 0x0FU;
        for (uint32_t i = 0U; i < follow; ++i)
            length |= (uint32_t)(*(*cursor)++) << (4U + i * 8U);
    }
    uint32_t encoded = (uint32_t)(*cursor - start);
    if (length < encoded || length > (uint32_t)(end - start))
        return 0;
    *body = *cursor;
    *package_end = start + length;
    return 1;
}

static int aml_name_char(uint8_t c, int first)
{
    if (c == '_' || (c >= 'A' && c <= 'Z'))
        return 1;
    return !first && c >= '0' && c <= '9';
}

/* Decode an AML NameString into a bounded canonical path. */
static int aml_read_name(const uint8_t** cursor, const uint8_t* end,
                         const char* scope, char output[ACPI_AML_PATH_MAX],
                         char final_segment[5])
{
    const uint8_t* p = *cursor;
    if (p >= end)
        return 0;
    char path[ACPI_AML_PATH_MAX];
    size_t used = 0U;
    size_t scope_len = strlen(scope);
    if (scope_len >= sizeof(path))
        return 0;
    memcpy(path, scope, scope_len + 1U);
    used = scope_len;

    int rooted = 0;
    if (*p == '\\')
    {
        rooted = 1;
        ++p;
        path[0] = '\\';
        path[1] = '\0';
        used = 1U;
    }
    while (p < end && *p == '^')
    {
        ++p;
        if (!rooted && used > 1U)
        {
            while (used > 1U && path[used - 1U] != '.')
                --used;
            if (used > 1U)
                --used;
            path[used] = '\0';
        }
    }

    uint32_t segments = 1U;
    if (p < end && *p == 0x00U)
    {
        ++p;
        segments = 0U;
    }
    else if (p < end && *p == 0x2EU)
    {
        ++p;
        segments = 2U;
    }
    else if (p < end && *p == 0x2FU)
    {
        ++p;
        if (p >= end)
            return 0;
        segments = *p++;
        if (segments == 0U)
            return 0;
    }

    final_segment[0] = '\0';
    for (uint32_t i = 0U; i < segments; ++i)
    {
        if ((size_t)(end - p) < 4U)
            return 0;
        char segment[5];
        for (uint32_t j = 0U; j < 4U; ++j)
        {
            if (!aml_name_char(p[j], j == 0U))
                return 0;
            segment[j] = (char)p[j];
        }
        segment[4] = '\0';
        p += 4U;

        if (used > 1U)
        {
            if (used + 1U >= sizeof(path))
                return 0;
            path[used++] = '.';
        }
        if (used + 4U >= sizeof(path))
            return 0;
        memcpy(path + used, segment, 4U);
        used += 4U;
        path[used] = '\0';
        memcpy(final_segment, segment, sizeof(segment));
    }
    memcpy(output, path, used + 1U);
    *cursor = p;
    return 1;
}

static int aml_read_integer(const uint8_t** cursor, const uint8_t* end,
                            uint64_t* value)
{
    if (*cursor >= end)
        return 0;
    uint8_t op = *(*cursor)++;
    switch (op)
    {
        case 0x00U: *value = 0U; return 1;
        case 0x01U: *value = 1U; return 1;
        case 0xFFU: *value = UINT64_MAX; return 1;
        case 0x0AU:
            if ((size_t)(end - *cursor) < 1U) return 0;
            *value = *(*cursor)++; return 1;
        case 0x0BU:
            if ((size_t)(end - *cursor) < 2U) return 0;
            *value = aml_u16(*cursor); *cursor += 2U; return 1;
        case 0x0CU:
            if ((size_t)(end - *cursor) < 4U) return 0;
            *value = aml_u32(*cursor); *cursor += 4U; return 1;
        case 0x0EU:
            if ((size_t)(end - *cursor) < 8U) return 0;
            *value = (uint64_t)aml_u32(*cursor) |
                     ((uint64_t)aml_u32(*cursor + 4U) << 32U);
            *cursor += 8U; return 1;
        default:
            --*cursor;
            return 0;
    }
}

static int aml_read_value(const uint8_t** cursor, const uint8_t* end,
                          const char* scope, aml_value_t* value)
{
    memset(value, 0, sizeof(*value));
    if (*cursor >= end)
        return 0;
    const uint8_t* p = *cursor;
    uint64_t integer;
    if (aml_read_integer(&p, end, &integer))
    {
        value->kind = AML_VALUE_INTEGER;
        value->integer = integer;
        *cursor = p;
        return 1;
    }

    uint8_t op = *(*cursor)++;
    if (op == 0x0DU)
    {
        const uint8_t* begin = *cursor;
        while (*cursor < end && **cursor != 0U)
            ++*cursor;
        if (*cursor >= end)
            return 0;
        value->kind = AML_VALUE_STRING;
        value->bytes = begin;
        value->length = (uint32_t)(*cursor - begin);
        ++*cursor;
        return 1;
    }
    if (op == 0x11U)
    {
        const uint8_t* body;
        const uint8_t* package_end;
        if (!aml_read_package(cursor, end, &body, &package_end))
            return 0;
        const uint8_t* data = body;
        uint64_t declared_size;
        if (!aml_read_integer(&data, package_end, &declared_size) ||
            declared_size > (uint64_t)(package_end - data))
            return 0;
        value->kind = AML_VALUE_BUFFER;
        value->bytes = data;
        value->length = (uint32_t)declared_size;
        value->package_begin = data;
        value->package_end = package_end;
        *cursor = package_end;
        return 1;
    }
    if (op == 0x12U || op == 0x13U)
    {
        const uint8_t* body;
        const uint8_t* package_end;
        if (!aml_read_package(cursor, end, &body, &package_end))
            return 0;
        uint64_t count;
        if (op == 0x12U)
        {
            if (body >= package_end)
                return 0;
            count = *body++;
        }
        else if (!aml_read_integer(&body, package_end, &count))
            return 0;
        if (count > 256U)
            return 0;
        value->kind = AML_VALUE_PACKAGE;
        value->package_count = (uint32_t)count;
        value->package_begin = body;
        value->package_end = package_end;
        *cursor = package_end;
        return 1;
    }

    /* A NameString-valued object is recorded as a reference, not evaluated. */
    if (op == '\\' || op == '^' || op == '_' ||
        (op >= 'A' && op <= 'Z') || op == 0x2EU || op == 0x2FU ||
        op == 0x00U)
    {
        --*cursor;
        char path[ACPI_AML_PATH_MAX], segment[5];
        if (!aml_read_name(cursor, end, scope, path, segment))
            return 0;
        value->kind = AML_VALUE_REFERENCE;
        return 1;
    }
    return 0;
}

static void aml_copy_ascii(char* output, size_t capacity,
                           const uint8_t* input, uint32_t length)
{
    if (capacity == 0U)
        return;
    size_t count = length < capacity - 1U ? length : capacity - 1U;
    for (size_t i = 0U; i < count; ++i)
        output[i] = (input[i] >= 0x20U && input[i] <= 0x7EU) ?
                    (char)input[i] : '?';
    output[count] = '\0';
}

static int aml_eisa_id(uint64_t value, char output[ACPI_AML_ID_MAX])
{
    uint32_t encoded = (uint32_t)value;
    value = ((encoded & 0x000000FFU) << 24U) |
            ((encoded & 0x0000FF00U) << 8U) |
            ((encoded & 0x00FF0000U) >> 8U) |
            ((encoded & 0xFF000000U) >> 24U);
    uint16_t manufacturer = (uint16_t)(value >> 16U);
    if (manufacturer == 0U || manufacturer == 0xFFFFU)
        return 0;
    char id[8];
    for (uint32_t i = 0U; i < 3U; ++i)
    {
        uint32_t shift = 10U - i * 5U;
        uint32_t letter = (manufacturer >> shift) & 0x1FU;
        if (letter == 0U || letter > 26U)
            return 0;
        id[i] = (char)('A' + letter - 1U);
    }
    static const char hex[] = "0123456789ABCDEF";
    uint16_t product = (uint16_t)value;
    id[3] = hex[(product >> 12U) & 0x0FU];
    id[4] = hex[(product >> 8U) & 0x0FU];
    id[5] = hex[(product >> 4U) & 0x0FU];
    id[6] = hex[product & 0x0FU];
    id[7] = '\0';
    memcpy(output, id, sizeof(id));
    return 1;
}

static void aml_store_id(char output[ACPI_AML_ID_MAX],
                         const aml_value_t* value)
{
    if (value->kind == AML_VALUE_STRING)
        aml_copy_ascii(output, ACPI_AML_ID_MAX,
                       value->bytes, value->length);
    else if (value->kind == AML_VALUE_INTEGER)
        (void)aml_eisa_id(value->integer, output);
}

static acpi_aml_resource_t* aml_new_resource(
    acpi_aml_device_t* device, uint8_t tag, uint32_t descriptor_length)
{
    ++device->resource_total;
    if (device->resource_count >= ACPI_AML_MAX_RESOURCES)
        return NULL;
    acpi_aml_resource_t* resource =
        &device->resources[device->resource_count++];
    memset(resource, 0, sizeof(*resource));
    resource->descriptor_tag = tag;
    resource->descriptor_length = (uint16_t)descriptor_length;
    return resource;
}

static uint64_t aml_read_width(const uint8_t* p, uint32_t width)
{
    if (width == 2U)
        return aml_u16(p);
    if (width == 4U)
        return aml_u32(p);
    if (width == 8U)
        return (uint64_t)aml_u32(p) |
               ((uint64_t)aml_u32(p + 4U) << 32U);
    return 0U;
}

static int aml_parse_resources(acpi_aml_device_t* device,
                               const uint8_t* bytes, uint32_t length)
{
    uint32_t cursor = 0U;
    int end_tag_seen = 0;
    while (cursor < length)
    {
        uint8_t tag = bytes[cursor];
        if ((tag & 0x80U) == 0U)
        {
            uint32_t item_length = tag & 0x07U;
            uint32_t item_type = (tag >> 3U) & 0x0FU;
            if (item_length > length - cursor - 1U)
                return 0;
            if (item_type == 0x0FU)
            {
                if (item_length != 1U)
                    return 0;
                end_tag_seen = 1;
                cursor += item_length + 1U;
                break;
            }
            acpi_aml_resource_t* resource =
                aml_new_resource(device, tag, item_length);
            if (resource && item_type == 0x04U && item_length >= 2U)
            {
                resource->resource_type = 3U;
                resource->irq_mask = aml_u16(bytes + cursor + 1U);
                if (item_length >= 3U)
                    resource->flags = bytes[cursor + 3U];
            }
            else if (resource && item_type == 0x08U && item_length == 7U)
            {
                resource->resource_type = 2U;
                resource->flags = bytes[cursor + 1U];
                resource->minimum = aml_u16(bytes + cursor + 2U);
                resource->maximum = aml_u16(bytes + cursor + 4U);
                resource->address = resource->minimum;
                resource->length = bytes[cursor + 7U];
            }
            else if (resource && item_type == 0x09U && item_length == 3U)
            {
                resource->resource_type = 2U;
                resource->address = aml_u16(bytes + cursor + 1U);
                resource->minimum = resource->address;
                resource->maximum = resource->address;
                resource->length = bytes[cursor + 3U];
            }
            cursor += item_length + 1U;
        }
        else
        {
            if (length - cursor < 3U)
                return 0;
            uint32_t item_length = aml_u16(bytes + cursor + 1U);
            if (item_length > length - cursor - 3U)
                return 0;
            const uint8_t* payload = bytes + cursor + 3U;
            acpi_aml_resource_t* resource =
                aml_new_resource(device, tag, item_length);
            if (resource)
            {
                if (tag == 0x8EU)
                {
                    if (item_length < 9U)
                        return 0;
                    if (payload[2U] == 1U)
                    {
                        uint16_t type_data_length = aml_u16(payload + 7U);
                        uint32_t source_offset = 9U + type_data_length;
                        int source_terminated = 0;
                        if (source_offset < item_length)
                            for (uint32_t j = source_offset; j < item_length; ++j)
                                if (payload[j] == 0U)
                                {
                                    source_terminated = 1;
                                    break;
                                }
                        if ((payload[0U] != 1U && payload[0U] != 2U) ||
                            payload[6U] != 1U || type_data_length < 6U ||
                            source_offset >= item_length || !source_terminated)
                            return 0;
                        resource->resource_type = 4U;
                        resource->serial_bus_type = payload[2U];
                        resource->general_flags = payload[3U];
                        resource->type_flags = aml_u16(payload + 4U);
                        resource->connection_speed_hz = aml_u32(payload + 9U);
                        resource->slave_address = aml_u16(payload + 13U);
                        uint32_t source_length = 0U;
                        while (source_offset + source_length < item_length &&
                               payload[source_offset + source_length] != 0U)
                            ++source_length;
                        aml_copy_ascii(resource->resource_source,
                                       sizeof(resource->resource_source),
                                       payload + source_offset,
                                       source_length);
                        ++device->i2c_count;
                    }
                }
                else if (tag == 0x8CU)
                {
                    /* ACPI GPIO Connection Descriptor; offsets include its 3-byte header. */
                    if (item_length < 22U || payload[0U] != 1U ||
                        payload[1U] > 1U ||
                        (aml_u16(payload + 2U) & 0xFFFEU) != 0U ||
                        payload[13U] != 0U)
                        return 0;
                    uint16_t pin_offset = aml_u16(payload + 11U);
                    uint16_t source_offset = aml_u16(payload + 14U);
                    uint16_t vendor_offset = aml_u16(payload + 16U);
                    uint16_t vendor_length = aml_u16(payload + 18U);
                    uint32_t descriptor_length = item_length + 3U;
                    if (pin_offset < 23U || source_offset <= pin_offset ||
                        vendor_offset < source_offset ||
                        vendor_offset > descriptor_length ||
                        vendor_length > descriptor_length - vendor_offset)
                        return 0;
                    uint32_t pin_begin = (uint32_t)pin_offset - 3U;
                    uint32_t source_begin = (uint32_t)source_offset - 3U;
                    uint32_t vendor_begin = (uint32_t)vendor_offset - 3U;
                    if (pin_begin < 20U || source_begin > item_length ||
                        vendor_begin > item_length || source_begin < pin_begin ||
                        ((source_begin - pin_begin) & 1U) != 0U)
                        return 0;
                    uint32_t pin_count = (source_begin - pin_begin) / 2U;
                    if (pin_count == 0U || pin_count > ACPI_AML_MAX_GPIO_PINS ||
                        vendor_begin <= source_begin)
                        return 0;
                    uint32_t source_length = vendor_begin - source_begin;
                    uint32_t source_name_length = 0U;
                    while (source_name_length < source_length &&
                           payload[source_begin + source_name_length] != 0U)
                        ++source_name_length;
                    /* ACPI permits the Resource Source string without a NUL. */
                    if (source_name_length == 0U)
                        return 0;
                    resource->resource_type = 5U;
                    resource->gpio_connection_type = payload[1U];
                    resource->gpio_general_flags = aml_u16(payload + 2U);
                    resource->gpio_interrupt_flags = aml_u16(payload + 4U);
                    resource->gpio_pin_configuration = payload[6U];
                    resource->gpio_drive_strength = aml_u16(payload + 7U);
                    resource->gpio_debounce_timeout = aml_u16(payload + 9U);
                    resource->gpio_pin_count = (uint16_t)pin_count;
                    for (uint32_t pin = 0U; pin < pin_count; ++pin)
                        resource->gpio_pins[pin] =
                            aml_u16(payload + pin_begin + pin * 2U);
                    aml_copy_ascii(resource->resource_source,
                                   sizeof(resource->resource_source),
                                   payload + source_begin, source_name_length);
                }
                else if (tag == 0x86U && item_length == 9U)
                {
                    resource->resource_type = 1U;
                    resource->flags = payload[0U];
                    resource->address = aml_u32(payload + 1U);
                    resource->minimum = resource->address;
                    resource->maximum = resource->address;
                    resource->length = aml_u32(payload + 5U);
                }
                else if (tag == 0x85U && item_length == 17U)
                {
                    resource->resource_type = 1U;
                    resource->flags = payload[0U];
                    resource->minimum = aml_u32(payload + 1U);
                    resource->maximum = aml_u32(payload + 5U);
                    resource->address = resource->minimum;
                    resource->length = aml_u32(payload + 13U);
                }
                else if (tag == 0x89U)
                {
                    if (item_length < 2U ||
                        payload[1U] > (item_length - 2U) / 4U)
                        return 0;
                    resource->resource_type = 3U;
                    resource->flags = payload[0U];
                    if (payload[1U] != 0U)
                        resource->irq = aml_u32(payload + 2U);
                }
                else if (tag == 0x87U || tag == 0x88U || tag == 0x8AU)
                {
                    uint32_t width = tag == 0x88U ? 2U :
                                     tag == 0x87U ? 4U : 8U;
                    if (item_length != 3U + width * 5U)
                        return 0;
                    resource->resource_type = payload[0U] == 0U ? 1U :
                                              payload[0U] == 1U ? 2U : 5U;
                    resource->flags = payload[1U];
                    resource->minimum = aml_read_width(payload + 3U + width,
                                                       width);
                    resource->maximum = aml_read_width(
                        payload + 3U + width * 2U, width);
                    resource->address = resource->minimum;
                    resource->length = aml_read_width(
                        payload + 3U + width * 4U, width);
                }
            }
            cursor += item_length + 3U;
        }
    }
    return end_tag_seen;
}

static int aml_parse_cids(acpi_aml_device_t* device,
                          const aml_value_t* package, const char* scope)
{
    const uint8_t* cursor = package->package_begin;
    for (uint32_t i = 0U; i < package->package_count && cursor < package->package_end; ++i)
    {
        aml_value_t item;
        if (!aml_read_value(&cursor, package->package_end, scope, &item))
            return 0;
        if (device->cid_count >= ACPI_AML_MAX_CIDS)
            continue;
        char* target = device->cids[device->cid_count];
        if (item.kind == AML_VALUE_STRING)
        {
            aml_copy_ascii(target, ACPI_AML_ID_MAX, item.bytes, item.length);
            if (target[0] != '\0')
                ++device->cid_count;
        }
        else if (item.kind == AML_VALUE_INTEGER)
        {
            if (aml_eisa_id(item.integer, target))
                ++device->cid_count;
        }
    }
    return 1;
}

static int aml_name_equals(const char* left, const char* right)
{
    while (*left != '\0' && *right != '\0')
    {
        if (*left != *right)
            return 0;
        ++left;
        ++right;
    }
    return *left == *right;
}

static int aml_string_equals(const aml_value_t* value,
                              const char* expected)
{
    if (value->kind != AML_VALUE_STRING)
        return 0;
    size_t length = strlen(expected);
    if (value->length != length)
        return 0;
    for (size_t i = 0U; i < length; ++i)
        if ((char)value->bytes[i] != expected[i])
            return 0;
    return 1;
}

/* ACPI Device Properties UUID: daffd814-6eba-4d8c-8a91-bc9bbf4aa301. */
static int aml_parse_dsd(acpi_aml_device_t* device,
                         const aml_value_t* package, const char* scope)
{
    static const uint8_t device_properties_uuid[16] = {
        0x14U, 0xD8U, 0xFFU, 0xDAU, 0xBAU, 0x6EU, 0x8CU, 0x4DU,
        0x8AU, 0x91U, 0xBCU, 0x9BU, 0xBFU, 0x4AU, 0xA3U, 0x01U
    };
    if (!device || package->kind != AML_VALUE_PACKAGE ||
        package->package_count != 2U)
        return 1;

    const uint8_t* cursor = package->package_begin;
    aml_value_t uuid, properties;
    if (!aml_read_value(&cursor, package->package_end, scope, &uuid))
        return 0;
    if (uuid.kind != AML_VALUE_BUFFER || uuid.length != sizeof(device_properties_uuid))
        return 1; /* Other _DSD GUIDs are outside this parser's scope. */
    for (uint32_t i = 0U; i < sizeof(device_properties_uuid); ++i)
        if (uuid.bytes[i] != device_properties_uuid[i])
            return 1;

    if (!aml_read_value(&cursor, package->package_end, scope, &properties) ||
        properties.kind != AML_VALUE_PACKAGE || cursor != package->package_end ||
        (properties.package_count & 1U) != 0U)
        return 0;

    cursor = properties.package_begin;
    for (uint32_t i = 0U; i < properties.package_count; i += 2U)
    {
        aml_value_t key, value;
        if (!aml_read_value(&cursor, properties.package_end, scope, &key) ||
            !aml_read_value(&cursor, properties.package_end, scope, &value))
            return 0;
        if (!aml_string_equals(&key, "hid-descr-addr"))
            continue;
        if (value.kind != AML_VALUE_INTEGER || value.integer == 0U ||
            value.integer > UINT16_MAX)
            return 0;
        uint16_t address = (uint16_t)value.integer;
        if (device->hid_descriptor_address_valid &&
            device->hid_descriptor_address != address)
            return 0;
        device->hid_descriptor_address = address;
        device->hid_descriptor_address_valid = 1U;
    }
    return cursor == properties.package_end;
}

static int aml_handle_name(acpi_aml_device_t* device, const char* name,
                           const aml_value_t* value, const char* scope)
{
    if (!device)
        return 1;
    if (aml_name_equals(name, "_HID"))
        aml_store_id(device->hid, value);
    else if (aml_name_equals(name, "_CID") && value->kind == AML_VALUE_PACKAGE)
        return aml_parse_cids(device, value, scope);
    else if (aml_name_equals(name, "_UID"))
    {
        if (value->kind == AML_VALUE_INTEGER)
        {
            device->uid = value->integer;
            device->uid_valid = 1U;
            device->uid_is_string = 0U;
        }
        else if (value->kind == AML_VALUE_STRING)
        {
            aml_copy_ascii(device->uid_string, sizeof(device->uid_string),
                           value->bytes, value->length);
            device->uid_valid = 1U;
            device->uid_is_string = 1U;
        }
    }
    else if (aml_name_equals(name, "_STA") &&
             value->kind == AML_VALUE_INTEGER)
    {
        device->sta = (uint32_t)value->integer;
        device->sta_valid = 1U;
    }
    else if (aml_name_equals(name, "_ADR") &&
             value->kind == AML_VALUE_INTEGER)
    {
        device->adr = value->integer;
        device->adr_valid = 1U;
    }
    else if (aml_name_equals(name, "_BBN") &&
             value->kind == AML_VALUE_INTEGER &&
             value->integer <= UINT8_MAX)
    {
        device->bbn = (uint8_t)value->integer;
        device->bbn_valid = 1U;
    }
    else if (aml_name_equals(name, "_SEG") &&
             value->kind == AML_VALUE_INTEGER &&
             value->integer <= UINT16_MAX)
    {
        device->segment = (uint16_t)value->integer;
        device->segment_valid = 1U;
    }
    else if (aml_name_equals(name, "_CRS") &&
             value->kind == AML_VALUE_BUFFER)
        return aml_parse_resources(device, value->bytes, value->length);
    else if (aml_name_equals(name, "_DSD") &&
             value->kind == AML_VALUE_PACKAGE)
        return aml_parse_dsd(device, value, scope);
    return 1;
}

static int aml_parse_terms(aml_parser_t* parser, const uint8_t* cursor,
                           const uint8_t* end, const char* scope,
                           acpi_aml_device_t* device, uint32_t depth);

static int aml_parse_device(aml_parser_t* parser, const uint8_t** cursor,
                            const uint8_t* end, const char* scope,
                            uint32_t depth)
{
    const uint8_t* body;
    const uint8_t* package_end;
    if (!aml_read_package(cursor, end, &body, &package_end))
        return 0;
    char path[ACPI_AML_PATH_MAX], segment[5];
    if (!aml_read_name(&body, package_end, scope, path, segment))
        return 0;
    acpi_aml_device_t device;
    memset(&device, 0, sizeof(device));
    memcpy(device.path, path, strlen(path) + 1U);
    if (strlen(scope) >= sizeof(device.parent_path))
        return 0;
    memcpy(device.parent_path, scope, strlen(scope) + 1U);
    if (!aml_parse_terms(parser, body, package_end, path, &device, depth + 1U))
        return 0;
    if (parser->devices_found >= AML_MAX_DEVICES)
        return 0;
    ++parser->devices_found;
    if (parser->callback)
        parser->callback(&device, parser->callback_context);
    *cursor = package_end;
    return 1;
}

static int aml_skip_package(const uint8_t** cursor, const uint8_t* end)
{
    const uint8_t* body;
    const uint8_t* package_end;
    if (!aml_read_package(cursor, end, &body, &package_end))
        return 0;
    (void)body;
    *cursor = package_end;
    return 1;
}

static int aml_skip_termarg(const uint8_t** cursor, const uint8_t* end,
                            const char* scope)
{
    aml_value_t value;
    if (aml_read_value(cursor, end, scope, &value))
        return 1;
    return 0;
}

static int aml_parse_terms(aml_parser_t* parser, const uint8_t* cursor,
                           const uint8_t* end, const char* scope,
                           acpi_aml_device_t* device, uint32_t depth)
{
    if (depth > AML_MAX_DEPTH)
        return 0;
    while (cursor < end)
    {
        uint8_t op = *cursor++;
        if (op == 0x08U) /* NameOp */
        {
            char path[ACPI_AML_PATH_MAX], name[5];
            if (!aml_read_name(&cursor, end, scope, path, name))
                return 0;
            aml_value_t value;
            if (!aml_read_value(&cursor, end, scope, &value) ||
                !aml_handle_name(device, name, &value, scope))
                return 0;
        }
        else if (op == 0x10U) /* ScopeOp */
        {
            const uint8_t* body;
            const uint8_t* package_end;
            if (!aml_read_package(&cursor, end, &body, &package_end))
                return 0;
            char path[ACPI_AML_PATH_MAX], name[5];
            if (!aml_read_name(&body, package_end, scope, path, name) ||
                !aml_parse_terms(parser, body, package_end, path,
                                 device, depth + 1U))
                return 0;
            cursor = package_end;
        }
        else if (op == 0x14U) /* MethodOp: record dynamic status, never execute. */
        {
            const uint8_t* body;
            const uint8_t* package_end;
            char path[ACPI_AML_PATH_MAX], name[5];
            if (!aml_read_package(&cursor, end, &body, &package_end) ||
                !aml_read_name(&body, package_end, scope, path, name) ||
                body >= package_end)
                return 0;
            if (device && aml_name_equals(name, "_STA"))
                device->sta_dynamic = 1U;
            cursor = package_end;
        }
        else if (op == 0xA0U || op == 0xA1U ||
                 op == 0xA2U) /* If / Else / While: skip without execution. */
        {
            if (!aml_skip_package(&cursor, end))
                return 0;
        }
        else if (op == 0x15U) /* ExternalOp */
        {
            char path[ACPI_AML_PATH_MAX], name[5];
            if (!aml_read_name(&cursor, end, scope, path, name) ||
                (size_t)(end - cursor) < 2U)
                return 0;
            cursor += 2U;
        }
        else if (op == 0x06U) /* AliasOp */
        {
            char path[ACPI_AML_PATH_MAX], name[5];
            if (!aml_read_name(&cursor, end, scope, path, name) ||
                !aml_read_name(&cursor, end, scope, path, name))
                return 0;
        }
        else if (op == 0x5BU) /* Extended opcode */
        {
            if (cursor >= end)
                return 0;
            uint8_t ext = *cursor++;
            if (ext == 0x82U) /* DeviceOp */
            {
                if (!aml_parse_device(parser, &cursor, end, scope, depth))
                    return 0;
            }
            else if (ext == 0x01U) /* Mutex */
            {
                char path[ACPI_AML_PATH_MAX], name[5];
                if (!aml_read_name(&cursor, end, scope, path, name) ||
                    cursor >= end)
                    return 0;
                ++cursor;
            }
            else if (ext == 0x02U) /* Event */
            {
                char path[ACPI_AML_PATH_MAX], name[5];
                if (!aml_read_name(&cursor, end, scope, path, name))
                    return 0;
            }
            else if (ext == 0x80U) /* OperationRegion */
            {
                char path[ACPI_AML_PATH_MAX], name[5];
                if (!aml_read_name(&cursor, end, scope, path, name) ||
                    cursor >= end)
                    return 0;
                ++cursor; /* RegionSpace */
                if (!aml_skip_termarg(&cursor, end, scope) ||
                    !aml_skip_termarg(&cursor, end, scope))
                    return 0;
            }
            else if (ext == 0x81U || ext == 0x83U || ext == 0x84U ||
                     ext == 0x85U || ext == 0x86U || ext == 0x87U)
            {
                if (!aml_skip_package(&cursor, end))
                    return 0;
            }
            else if (ext == 0x88U) /* DataRegion */
            {
                char path[ACPI_AML_PATH_MAX], name[5];
                if (!aml_read_name(&cursor, end, scope, path, name) ||
                    !aml_skip_termarg(&cursor, end, scope) ||
                    !aml_skip_termarg(&cursor, end, scope) ||
                    !aml_skip_termarg(&cursor, end, scope))
                    return 0;
            }
            else
                return 0;
        }
        else if (op == 0xA3U || op == 0xA5U || op == 0x9FU ||
                 op == 0xCCU) /* Noop, Break, Continue, BreakPoint */
        {
            /* No operands. */
        }
        else if (op == 0xA4U) /* Return */
        {
            if (!aml_skip_termarg(&cursor, end, scope))
                return 0;
        }
        else
            return 0; /* Unknown term: do not scan through opaque AML bytes. */
    }
    return cursor == end;
}

int acpi_aml_parse_table(const uint8_t* aml, uint32_t length,
                         acpi_aml_device_callback_t callback,
                         void* context, uint32_t* devices_found)
{
    if (devices_found)
        *devices_found = 0U;
    if (!aml || length == 0U || length > AML_MAX_TABLE_BYTES)
        return 0;
    aml_parser_t parser;
    memset(&parser, 0, sizeof(parser));
    parser.callback = callback;
    parser.callback_context = context;
    if (!aml_parse_terms(&parser, aml, aml + length, "\\", NULL, 0U))
        return 0;
    if (devices_found)
        *devices_found = parser.devices_found;
    return 1;
}
