/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef GRAPHICS_DAMAGE_H
#define GRAPHICS_DAMAGE_H

#include <stdint.h>

/* Tile coordinates are half-open: [first, first + count). */
/* Pixel-space rectangles use half-open bounds: [x, x + width). */
typedef struct
{
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} graphics_damage_rect_t;

/* Build a bounded union without overflowing signed coordinates. */
static inline int graphics_damage_rect_union(
    const graphics_damage_rect_t* a,
    const graphics_damage_rect_t* b,
    graphics_damage_rect_t* out)
{
    if (!a || !b || !out || a->width <= 0 || a->height <= 0 ||
        b->width <= 0 || b->height <= 0)
        return 0;

    int64_t left = a->x < b->x ? a->x : b->x;
    int64_t top = a->y < b->y ? a->y : b->y;
    int64_t ar = (int64_t)a->x + a->width;
    int64_t br = (int64_t)b->x + b->width;
    int64_t ab = (int64_t)a->y + a->height;
    int64_t bb = (int64_t)b->y + b->height;
    int64_t right = ar > br ? ar : br;
    int64_t bottom = ab > bb ? ab : bb;

    if (left < INT32_MIN || top < INT32_MIN ||
        right > INT32_MAX || bottom > INT32_MAX ||
        right - left > INT32_MAX || bottom - top > INT32_MAX)
        return 0;

    out->x = (int32_t)left;
    out->y = (int32_t)top;
    out->width = (int32_t)(right - left);
    out->height = (int32_t)(bottom - top);
    return 1;
}

/* Return the visible intersection of two half-open pixel rectangles. */
static inline int graphics_damage_rect_intersection(
    const graphics_damage_rect_t* a,
    const graphics_damage_rect_t* b,
    graphics_damage_rect_t* out)
{
    if (!a || !b || !out || a->width <= 0 || a->height <= 0 ||
        b->width <= 0 || b->height <= 0)
        return 0;

    int64_t left = a->x > b->x ? a->x : b->x;
    int64_t top = a->y > b->y ? a->y : b->y;
    int64_t ar = (int64_t)a->x + a->width;
    int64_t br = (int64_t)b->x + b->width;
    int64_t ab = (int64_t)a->y + a->height;
    int64_t bb = (int64_t)b->y + b->height;
    int64_t right = ar < br ? ar : br;
    int64_t bottom = ab < bb ? ab : bb;

    if (left >= right || top >= bottom)
        return 0;
    out->x = (int32_t)left;
    out->y = (int32_t)top;
    out->width = (int32_t)(right - left);
    out->height = (int32_t)(bottom - top);
    return 1;
}

typedef struct
{
    uint32_t first_x;
    uint32_t first_y;
    uint32_t count_x;
    uint32_t count_y;
} graphics_damage_tiles_t;

/*
 * Convert a signed pixel rectangle into a clipped tile range. Widen before
 * adding coordinates and dimensions so malformed rectangles cannot overflow
 * into the visible surface.
 */
static inline int graphics_damage_compute_tiles(
    int32_t x, int32_t y, int32_t width, int32_t height,
    uint32_t framebuffer_width, uint32_t framebuffer_height,
    uint32_t tile_size, graphics_damage_tiles_t* out)
{
    if (!out || width <= 0 || height <= 0 ||
        framebuffer_width == 0U || framebuffer_height == 0U ||
        tile_size == 0U)
        return 0;

    int64_t left = x;
    int64_t top = y;
    int64_t right = left + (int64_t)width;
    int64_t bottom = top + (int64_t)height;

    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > (int64_t)framebuffer_width)
        right = framebuffer_width;
    if (bottom > (int64_t)framebuffer_height)
        bottom = framebuffer_height;

    if (left >= right || top >= bottom)
        return 0;

    uint32_t x0 = (uint32_t)left / tile_size;
    uint32_t y0 = (uint32_t)top / tile_size;
    uint32_t x1 = ((uint32_t)right - 1U) / tile_size + 1U;
    uint32_t y1 = ((uint32_t)bottom - 1U) / tile_size + 1U;

    out->first_x = x0;
    out->first_y = y0;
    out->count_x = x1 - x0;
    out->count_y = y1 - y0;
    return 1;
}

#endif
