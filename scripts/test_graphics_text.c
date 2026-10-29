/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "graphics_internal.h"

#define TEST_WIDTH 40U
#define TEST_HEIGHT 24U
#define TEST_PITCH (TEST_WIDTH * sizeof(uint32_t))
#define DAMAGE_CAPACITY 32U

typedef struct
{
    int x;
    int y;
    int width;
    int height;
} damage_rect_t;

static uint32_t test_pixels[TEST_WIDTH * TEST_HEIGHT];
static damage_rect_t damage_rects[DAMAGE_CAPACITY];
static size_t damage_count;

uint8_t* framebuffer;
uint32_t framebuffer_pitch;
uint32_t framebuffer_width;
uint32_t framebuffer_height;
int initialized;

uint32_t graphics_pack_color(uint32_t color)
{
    return color;
}

void graphics_mark_damage(int x, int y, int width, int height)
{
    assert(width > 0);
    assert(height > 0);
    assert(damage_count < DAMAGE_CAPACITY);
    damage_rects[damage_count++] = (damage_rect_t){ x, y, width, height };
}

static uint32_t pixel_at(unsigned int x, unsigned int y)
{
    assert(x < TEST_WIDTH && y < TEST_HEIGHT);
    return test_pixels[y * TEST_WIDTH + x];
}

static void reset_damage(void)
{
    memset(damage_rects, 0, sizeof(damage_rects));
    damage_count = 0;
}

int main(void)
{
    framebuffer = (uint8_t*)test_pixels;
    framebuffer_pitch = TEST_PITCH;
    framebuffer_width = TEST_WIDTH;
    framebuffer_height = TEST_HEIGHT;
    initialized = 1;
    memset(test_pixels, 0, sizeof(test_pixels));

    /* The 5x7 capital A must retain its exact bitmap at scale one. */
    graphics_draw_text(2, 3, "A", 0x12345678U, 1U);
    assert(pixel_at(3, 3) == 0x12345678U);
    assert(pixel_at(4, 3) == 0x12345678U);
    assert(pixel_at(5, 3) == 0x12345678U);
    assert(pixel_at(2, 3) == 0U);
    assert(pixel_at(2, 4) == 0x12345678U);
    assert(pixel_at(6, 4) == 0x12345678U);
    assert(pixel_at(4, 6) == 0x12345678U);
    assert(damage_count == 1U);
    assert(damage_rects[0].x == 2);
    assert(damage_rects[0].y == 3);
    assert(damage_rects[0].width == 5);
    assert(damage_rects[0].height == 7);

    /* Repainting identical text must not create needless display damage. */
    graphics_draw_text(2, 3, "A", 0x12345678U, 1U);
    assert(damage_count == 1U);

    /* Scaled glyphs expand each set font bit into a 2x2 pixel block. */
    reset_damage();
    graphics_draw_text(8, 2, "A", 0xAABBCCDDU, 2U);
    assert(pixel_at(10, 2) == 0xAABBCCDDU);
    assert(pixel_at(11, 3) == 0xAABBCCDDU);
    assert(pixel_at(8, 4) == 0xAABBCCDDU);
    assert(pixel_at(9, 5) == 0xAABBCCDDU);
    assert(damage_count == 1U);
    assert(damage_rects[0].x == 8);
    assert(damage_rects[0].y == 2);
    assert(damage_rects[0].width == 10);
    assert(damage_rects[0].height == 14);

    /* Off-screen glyph pixels are clipped without out-of-bounds writes. */
    reset_damage();
    graphics_draw_text(-2, 17, "A", 0x55667788U, 1U);
    assert(pixel_at(0, 17) == 0x55667788U);
    assert(pixel_at(1, 17) == 0x55667788U);
    assert(damage_count == 1U);
    assert(damage_rects[0].x == 0);
    assert(damage_rects[0].y == 17);
    assert(damage_rects[0].width == 3);
    assert(damage_rects[0].height == 7);

    reset_damage();
    graphics_draw_text(100, 100, "VantaOS", 0xFFFFFFFFU, 1U);
    assert(damage_count == 0U);

    puts("direct shadow-buffer text rasterization tests passed");
    return 0;
}
