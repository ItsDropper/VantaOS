/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "panic_format.h"
#include <assert.h>
#include <string.h>

int main(void)
{
    char hex[19];
    panic_format_hex64(hex, UINT64_C(0));
    assert(strcmp(hex, "0x0000000000000000") == 0);
    panic_format_hex64(hex, UINT64_C(0xFEDCBA9876543210));
    assert(strcmp(hex, "0xFEDCBA9876543210") == 0);
    panic_format_hex64(hex, UINT64_MAX);
    assert(strcmp(hex, "0xFFFFFFFFFFFFFFFF") == 0);

    assert(strcmp(panic_format_exception_name(0U), "Divide Error") == 0);
    assert(strcmp(panic_format_exception_name(14U), "Page Fault") == 0);
    assert(strcmp(panic_format_exception_name(31U), "Unknown") == 0);
    assert(strcmp(panic_format_exception_name(255U), "Unknown") == 0);
    return 0;
}
