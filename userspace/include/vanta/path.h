/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_USER_PATH_H
#define VANTA_USER_PATH_H

#include <stddef.h>

/*
 * Resolve a path against cwd without filesystem access.
 * Repeated separators and "." components are removed; ".." walks upward but
 * never above root. Returns 1 on success, 0 for invalid arguments/overflow.
 */
static inline int vanta_path_resolve(const char* input, const char* cwd,
                                     char* output, size_t capacity)
{
    if (!input || !input[0] || !cwd || cwd[0] != '/' ||
        !output || capacity < 2U)
        return 0;

    size_t cwd_length = 0U;
    while (cwd[cwd_length])
        cwd_length++;
    size_t used = 1U;
    output[0] = '/';
    output[1] = '\0';

    const char* cursor = input;
    if (input[0] != '/')
    {
        if (cwd_length >= capacity)
        {
            output[0] = '\0';
            return 0;
        }
        for (size_t i = 0U; i < cwd_length; ++i)
            output[i] = cwd[i];
        used = cwd_length;
        output[used] = '\0';
    }

    while (*cursor)
    {
        while (*cursor == '/')
            cursor++;
        if (!*cursor)
            break;

        const char* component = cursor;
        while (*cursor && *cursor != '/')
            cursor++;
        size_t length = (size_t)(cursor - component);

        if (length == 1U && component[0] == '.')
            continue;
        if (length == 2U && component[0] == '.' && component[1] == '.')
        {
            if (used > 1U)
            {
                while (used > 1U && output[used - 1U] != '/')
                    used--;
                if (used > 1U)
                    used--;
                output[used] = '\0';
            }
            continue;
        }

        size_t separator = used > 1U ? 1U : 0U;
        if (used + separator + length >= capacity)
        {
            output[0] = '\0';
            return 0;
        }
        if (separator)
            output[used++] = '/';
        for (size_t i = 0U; i < length; ++i)
            output[used++] = component[i];
        output[used] = '\0';
    }
    return 1;
}

#endif
