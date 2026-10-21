/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "graphics_damage.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void expect(int32_t x, int32_t y, int32_t width, int32_t height,
                   int valid, uint32_t first_x, uint32_t first_y,
                   uint32_t count_x, uint32_t count_y)
{
    graphics_damage_tiles_t result = { 99U, 99U, 99U, 99U };
    int actual = graphics_damage_compute_tiles(
        x, y, width, height, 100U, 70U, 16U, &result);
    assert(actual == valid);
    if (valid)
    {
        assert(result.first_x == first_x);
        assert(result.first_y == first_y);
        assert(result.count_x == count_x);
        assert(result.count_y == count_y);
    }
}

int main(void)
{
    expect(0, 0, 1, 1, 1, 0, 0, 1, 1);
    expect(16, 16, 1, 1, 1, 1, 1, 1, 1);
    expect(15, 15, 2, 2, 1, 0, 0, 2, 2);
    expect(-4, -5, 8, 9, 1, 0, 0, 1, 1);
    expect(96, 64, 4, 6, 1, 6, 4, 1, 1);
    expect(100, 10, 1, 1, 0, 0, 0, 0, 0);
    expect(10, 70, 1, 1, 0, 0, 0, 0, 0);
    expect(-20, 0, 5, 5, 0, 0, 0, 0, 0);
    expect(0, 0, 0, 10, 0, 0, 0, 0, 0);
    expect(0, 0, 10, -1, 0, 0, 0, 0, 0);
    expect(INT32_MAX - 8, 0, INT32_MAX, 10, 0, 0, 0, 0, 0);
    expect(90, 60, INT32_MAX, INT32_MAX, 1, 5, 3, 2, 2);

    graphics_damage_tiles_t untouched = { 1U, 2U, 3U, 4U };
    assert(!graphics_damage_compute_tiles(0, 0, 1, 1, 100U, 70U, 0U,
                                          &untouched));
    assert(!graphics_damage_compute_tiles(0, 0, 1, 1, 100U, 70U, 16U,
                                          0));
    assert(untouched.first_x == 1U && untouched.count_y == 4U);

    puts("graphics damage geometry tests passed");
    return 0;
}
