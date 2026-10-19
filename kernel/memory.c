/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include <stddef.h>
#include <stdint.h>

/* Freestanding C runtime primitives required by BearSSL. Volatile byte
 * accesses prevent compiler loop-pattern optimisations from recursing into
 * these same standard-library symbols. */
void *memcpy(void *destination, const void *source, size_t length)
{
    volatile unsigned char *dst = (volatile unsigned char *)destination;
    const volatile unsigned char *src = (const volatile unsigned char *)source;
    for (size_t i = 0; i < length; ++i) dst[i] = src[i];
    return destination;
}
void *memset(void *destination, int value, size_t length)
{
    volatile unsigned char *dst = (volatile unsigned char *)destination;
    for (size_t i = 0; i < length; ++i) dst[i] = (unsigned char)value;
    return destination;
}
void *memmove(void *destination, const void *source, size_t length)
{
    volatile unsigned char *dst = (volatile unsigned char *)destination;
    const volatile unsigned char *src = (const volatile unsigned char *)source;
    if (dst == src || !length) return destination;
    if ((uintptr_t)dst < (uintptr_t)src ||
        (uintptr_t)dst - (uintptr_t)src >= length) {
        for (size_t i = 0; i < length; ++i) dst[i] = src[i];
    } else {
        for (size_t i = length; i != 0U; --i) dst[i - 1U] = src[i - 1U];
    }
    return destination;
}
int memcmp(const void *left, const void *right, size_t length)
{
    const volatile unsigned char *a = (const volatile unsigned char *)left;
    const volatile unsigned char *b = (const volatile unsigned char *)right;
    for (size_t i = 0; i < length; ++i) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}
size_t strlen(const char *text)
{
    const volatile char *p = (const volatile char *)text;
    size_t length = 0;
    while (p[length]) ++length;
    return length;
}
