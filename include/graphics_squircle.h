/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef VANTA_GRAPHICS_SQUIRCLE_H
#define VANTA_GRAPHICS_SQUIRCLE_H

/*
 * Return the largest horizontal offset inside an exponent-4 superellipse at
 * an absolute vertical offset. The kernel caps radius at 32, so signed
 * integer fourth powers remain safely within 32-bit range.
 */
static inline int graphics_squircle_extent(int radius, int absolute_y)
{
    if (radius < 0 || radius > 32 || absolute_y < 0 ||
        absolute_y > radius)
        return -1;

    int radius_squared = radius * radius;
    int radius_fourth = radius_squared * radius_squared;
    int y_squared = absolute_y * absolute_y;
    int y_fourth = y_squared * y_squared;

    for (int x = radius; x >= 0; --x)
    {
        int x_squared = x * x;
        if (x_squared * x_squared + y_fourth <= radius_fourth)
            return x;
    }
    return -1;
}

#endif
