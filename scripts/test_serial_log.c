/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "serial_log.h"
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
int main(void)
{
    char out[32];
    serial_log_append("abc", 3U);
    assert(serial_log_snapshot(out, sizeof(out)) == 3U);
    assert(memcmp(out, "abc", 3U) == 0);
    char *input = (char *)malloc(SERIAL_LOG_CAPACITY + 17U);
    assert(input != 0);
    for (size_t i = 0U; i < SERIAL_LOG_CAPACITY + 17U; ++i)
        input[i] = (char)('A' + (i % 26U));
    serial_log_append(input, SERIAL_LOG_CAPACITY + 17U);
    char *snapshot = (char *)malloc(SERIAL_LOG_CAPACITY);
    assert(snapshot != 0);
    assert(serial_log_snapshot(snapshot, SERIAL_LOG_CAPACITY) == SERIAL_LOG_CAPACITY);
    assert(memcmp(snapshot, input + 17U, SERIAL_LOG_CAPACITY) == 0);
    assert(serial_log_snapshot(out, 8U) == 8U);
    assert(memcmp(out, input + SERIAL_LOG_CAPACITY + 9U, 8U) == 0);
    assert(serial_log_snapshot(0, sizeof(out)) == 0U);
    assert(serial_log_snapshot(out, 0U) == 0U);
    free(snapshot);
    free(input);
    return 0;
}
