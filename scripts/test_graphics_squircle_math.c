/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include <stdio.h>
#include <graphics_squircle.h>

static int expect(int condition, const char* message)
{
    if (condition)
        return 0;
    fprintf(stderr, "Squircle geometry test failed: %s\n", message);
    return 1;
}

int main(void)
{
    int failures = 0;
    failures += expect(graphics_squircle_extent(4, 0) == 4,
                       "center row reaches full radius");
    failures += expect(graphics_squircle_extent(4, 4) == 0,
                       "corner row reaches only its center pixel");
    failures += expect(graphics_squircle_extent(4, 3) == 3,
                       "fourth-power boundary differs from circular corners");
    failures += expect(graphics_squircle_extent(4, 5) == -1,
                       "reject vertical offsets outside the radius");
    failures += expect(graphics_squircle_extent(33, 0) == -1,
                       "reject radii above the kernel work bound");

    for (int radius = 1; radius <= 32; ++radius)
    {
        int radius_squared = radius * radius;
        int radius_fourth = radius_squared * radius_squared;
        for (int y = 0; y <= radius; ++y)
        {
            int extent = graphics_squircle_extent(radius, y);
            failures += expect(extent >= 0 && extent <= radius,
                               "extent remains inside radius");
            if (extent < 0)
                continue;
            int x_squared = extent * extent;
            int y_squared = y * y;
            failures += expect(x_squared * x_squared +
                               y_squared * y_squared <= radius_fourth,
                               "returned point is inside the superellipse");
            if (extent < radius)
            {
                int next = extent + 1;
                int next_squared = next * next;
                failures += expect(next_squared * next_squared +
                                   y_squared * y_squared > radius_fourth,
                                   "next pixel lies outside the superellipse");
            }
        }
    }

    if (failures)
        return 1;
    puts("VANTAOS_SQUIRCLE_GEOMETRY_OK");
    return 0;
}
