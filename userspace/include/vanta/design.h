/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

/*
 * Vanta Design System (VDS)
 *
 * Header-only, freestanding component helpers for ring-3 GUI applications.
 * Coordinates are client-local. The kernel remains responsible for clipping,
 * validation, window ownership and framebuffer access.
 */
#ifndef VANTA_USER_DESIGN_H
#define VANTA_USER_DESIGN_H

#include <stddef.h>
#include <stdint.h>
#include <vanta/gui.h>
#include <vanta/theme.h>

#define VANTA_DESIGN_SPACE_1 4
#define VANTA_DESIGN_SPACE_2 8
#define VANTA_DESIGN_SPACE_3 12
#define VANTA_DESIGN_SPACE_4 16
#define VANTA_DESIGN_SPACE_5 24
#define VANTA_DESIGN_SPACE_6 32

#define VANTA_DESIGN_RADIUS_SMALL 4U
#define VANTA_DESIGN_RADIUS_CONTROL 6U
#define VANTA_DESIGN_RADIUS_SURFACE 10U
#define VANTA_DESIGN_RADIUS_DIALOG 14U

#define VANTA_DESIGN_STATE_DEFAULT 0U
#define VANTA_DESIGN_STATE_HOVER 1U
#define VANTA_DESIGN_STATE_SELECTED 2U
#define VANTA_DESIGN_STATE_DISABLED 3U
#define VANTA_DESIGN_STATE_DANGER 4U

typedef struct
{
    vanta_gui_draw_command_t* commands;
    size_t count;
    size_t capacity;
    vanta_theme_t theme;
} vanta_design_t;

static inline void vanta_design_init(vanta_design_t* design,
                                     vanta_gui_draw_command_t* commands,
                                     size_t capacity,
                                     const vanta_theme_t* theme)
{
    if (!design)
        return;
    design->commands = commands;
    design->count = 0U;
    design->capacity = capacity < VANTA_GUI_MAX_COMMANDS ?
        capacity : VANTA_GUI_MAX_COMMANDS;
    design->theme = theme ? *theme : vanta_theme_load();
}

static inline int vanta_design_push(vanta_design_t* design,
                                    const vanta_gui_draw_command_t* command)
{
    if (!design || !design->commands || !command ||
        design->count >= design->capacity ||
        design->count >= VANTA_GUI_MAX_COMMANDS)
        return 0;
    design->commands[design->count++] = *command;
    return 1;
}

static inline int vanta_design_rect(vanta_design_t* design,
                                    int32_t x, int32_t y,
                                    int32_t width, int32_t height,
                                    uint32_t color)
{
    if (width <= 0 || height <= 0)
        return 0;
    vanta_gui_draw_command_t command = {
        .type = VANTA_GUI_DRAW_FILL_RECT,
        .x = x, .y = y, .width = width, .height = height,
        .color = color, .scale = 1U
    };
    return vanta_design_push(design, &command);
}

/* Radius follows the shared size tiers; the kernel uses an exponent-4
 * superellipse boundary for squircle-style corner transitions. */
static inline uint32_t vanta_design_radius(int32_t width, int32_t height,
                                           uint32_t preferred)
{
    if (width <= 0 || height <= 0)
        return 0U;
    uint32_t limit = (uint32_t)(width < height ? width : height) / 2U;
    if (limit > 32U)
        limit = 32U;
    return preferred < limit ? preferred : limit;
}

static inline int vanta_design_squircle_rect(vanta_design_t* design,
                                          int32_t x, int32_t y,
                                          int32_t width, int32_t height,
                                          uint32_t radius, uint32_t color)
{
    if (width <= 0 || height <= 0)
        return 0;
    radius = vanta_design_radius(width, height, radius);
    vanta_gui_draw_command_t command = {
        .type = VANTA_GUI_DRAW_FILL_SQUIRCLE_RECT,
        .x = x, .y = y, .width = width, .height = height,
        .color = color, .scale = radius
    };
    return vanta_design_push(design, &command);
}

static inline int vanta_design_text(vanta_design_t* design,
                                    int32_t x, int32_t y,
                                    const char* text, uint32_t color,
                                    uint32_t scale)
{
    if (!text || scale < 1U || scale > 2U)
        return 0;
    size_t length = 0U;
    while (text[length] && length <= VANTA_GUI_TEXT_MAX)
        length++;
    if (length == 0U || length > VANTA_GUI_TEXT_MAX)
        return 0;
    vanta_gui_draw_command_t command = {
        .type = VANTA_GUI_DRAW_TEXT,
        .x = x, .y = y, .color = color, .scale = scale
    };
    for (size_t i = 0U; i <= length; i++)
        command.text[i] = text[i];
    return vanta_design_push(design, &command);
}

static inline uint32_t vanta_design_text_color(const vanta_design_t* design)
{
    return vanta_theme_text_color(design ? &design->theme : 0);
}

static inline uint32_t vanta_design_muted_color(const vanta_design_t* design)
{
    return vanta_theme_muted_color(design ? &design->theme : 0);
}

static inline uint32_t vanta_design_surface_color(const vanta_design_t* design)
{
    return vanta_theme_surface_color(design ? &design->theme : 0);
}

static inline uint32_t vanta_design_border_color(const vanta_design_t* design)
{
    return vanta_theme_border_color(design ? &design->theme : 0);
}

static inline uint32_t vanta_design_accent_color(const vanta_design_t* design)
{
    return vanta_theme_accent_color(design ? &design->theme : 0);
}

static inline int vanta_design_panel(vanta_design_t* design,
                                     int32_t x, int32_t y,
                                     int32_t width, int32_t height)
{
    if (!design || !design->commands || width <= 0 || height <= 0 ||
        design->count > design->capacity ||
        design->capacity - design->count < 2U ||
        design->count > VANTA_GUI_MAX_COMMANDS - 2U)
        return 0;
    uint32_t border = vanta_design_border_color(design);
    uint32_t surface = vanta_design_surface_color(design);
    if (!vanta_design_squircle_rect(design, x, y, width, height,
                                 VANTA_DESIGN_RADIUS_SURFACE, border))
        return 0;
    if (width <= 2 || height <= 2)
        return 1;
    return vanta_design_squircle_rect(design, x + 1, y + 1, width - 2,
                                   height - 2,
                                   VANTA_DESIGN_RADIUS_SURFACE - 1U,
                                   surface);
}

static inline uint32_t vanta_design_success_color(void)
{
    return 0x0022C55EU;
}

static inline uint32_t vanta_design_warning_color(void)
{
    return 0x00D97706U;
}

static inline uint32_t vanta_design_danger_color(void)
{
    return 0x00EF4444U;
}

/*
 * Fast YIQ foreground selection for filled controls. This is a readability
 * heuristic, not a WCAG contrast-ratio certification.
 */
static inline uint32_t vanta_design_on_color(uint32_t background)
{
    uint32_t red = (background >> 16) & 0xFFU;
    uint32_t green = (background >> 8) & 0xFFU;
    uint32_t blue = background & 0xFFU;
    uint32_t luma = (299U * red + 587U * green + 114U * blue) / 1000U;
    return luma >= 128U ? 0x00101820U : 0x00FFFFFFU;
}

static inline int vanta_design_has_capacity(const vanta_design_t* design,
                                            size_t required)
{
    return design && design->commands && required <= design->capacity &&
        design->count <= design->capacity - required &&
        design->count <= VANTA_GUI_MAX_COMMANDS - required;
}

static inline int vanta_design_button_impl(vanta_design_t* design,
                                           int32_t x, int32_t y,
                                           int32_t width, int32_t height,
                                           const char* label, uint32_t state,
                                           int outlined)
{
    if (!design || !label || width <= 0 || height < 16)
        return 0;

    size_t length = 0U;
    while (label[length] && length <= VANTA_GUI_TEXT_MAX)
        length++;
    if (length == 0U || length > VANTA_GUI_TEXT_MAX ||
        (int64_t)length * 6 + 12 > width ||
        !vanta_design_has_capacity(design, 3U))
        return 0;

    uint32_t fill = vanta_design_surface_color(design);
    uint32_t foreground = vanta_design_text_color(design);
    uint32_t border = vanta_design_border_color(design);

    if (outlined)
    {
        if (state == VANTA_DESIGN_STATE_HOVER)
            fill = vanta_theme_row_color(&design->theme);
        else if (state == VANTA_DESIGN_STATE_SELECTED)
        {
            border = vanta_design_accent_color(design);
            foreground = border;
        }
        else if (state == VANTA_DESIGN_STATE_DISABLED)
            foreground = vanta_design_muted_color(design);
        else if (state == VANTA_DESIGN_STATE_DANGER)
        {
            border = vanta_design_danger_color();
            foreground = border;
        }
        else if (state != VANTA_DESIGN_STATE_DEFAULT)
            return 0;
    }
    else
    {
        if (state == VANTA_DESIGN_STATE_HOVER)
            fill = vanta_theme_row_color(&design->theme);
        else if (state == VANTA_DESIGN_STATE_SELECTED)
        {
            fill = vanta_design_accent_color(design);
            foreground = vanta_design_on_color(fill);
        }
        else if (state == VANTA_DESIGN_STATE_DISABLED)
            foreground = vanta_design_muted_color(design);
        else if (state == VANTA_DESIGN_STATE_DANGER)
        {
            fill = vanta_design_danger_color();
            foreground = 0x00FFFFFFU;
        }
        else if (state != VANTA_DESIGN_STATE_DEFAULT)
            return 0;
    }

    uint32_t radius = vanta_design_radius(width, height,
                                          VANTA_DESIGN_RADIUS_CONTROL);
    if (!vanta_design_squircle_rect(design, x, y, width, height,
                                    radius, border))
        return 0;
    if (!vanta_design_squircle_rect(design, x + 1, y + 1,
                                    width - 2, height - 2,
                                    radius > 0U ? radius - 1U : 0U,
                                    fill))
        return 0;

    int32_t text_x = x + (width - (int32_t)(length * 6U)) / 2;
    int32_t text_y = y + (height - 16) / 2;
    return vanta_design_text(design, text_x, text_y, label, foreground, 1U);
}

static inline int vanta_design_button(vanta_design_t* design,
                                      int32_t x, int32_t y,
                                      int32_t width, int32_t height,
                                      const char* label, uint32_t state)
{
    return vanta_design_button_impl(design, x, y, width, height,
                                    label, state, 0);
}

static inline int vanta_design_outline_button(vanta_design_t* design,
                                              int32_t x, int32_t y,
                                              int32_t width, int32_t height,
                                              const char* label,
                                              uint32_t state)
{
    return vanta_design_button_impl(design, x, y, width, height,
                                    label, state, 1);
}

static inline int vanta_design_separator(vanta_design_t* design,
                                         int32_t x, int32_t y,
                                         int32_t width)
{
    if (width <= 0)
        return 0;
    return vanta_design_rect(design, x, y, width, 1,
                             vanta_design_border_color(design));
}

static inline int vanta_design_badge(vanta_design_t* design,
                                     int32_t x, int32_t y,
                                     const char* label, uint32_t state)
{
    if (!design || !label)
        return 0;
    size_t length = 0U;
    while (label[length] && length <= VANTA_GUI_TEXT_MAX)
        length++;
    if (length == 0U || length > VANTA_GUI_TEXT_MAX ||
        !vanta_design_has_capacity(design, 2U))
        return 0;

    uint32_t fill = vanta_theme_row_color(&design->theme);
    uint32_t foreground = vanta_design_muted_color(design);
    if (state == VANTA_DESIGN_STATE_SELECTED)
    {
        fill = vanta_design_accent_color(design);
        foreground = vanta_design_on_color(fill);
    }
    else if (state == VANTA_DESIGN_STATE_DANGER)
    {
        fill = vanta_design_danger_color();
        foreground = 0x00FFFFFFU;
    }
    else if (state == VANTA_DESIGN_STATE_DISABLED)
        foreground = vanta_design_muted_color(design);
    else if (state != VANTA_DESIGN_STATE_DEFAULT &&
             state != VANTA_DESIGN_STATE_HOVER)
        return 0;

    int32_t width = (int32_t)(length * 6U) + 16;
    const int32_t height = 20;
    if (!vanta_design_squircle_rect(design, x, y, width, height,
                                    height / 2U, fill))
        return 0;
    return vanta_design_text(design, x + 8, y + 2, label, foreground, 1U);
}

static inline int vanta_design_switch(vanta_design_t* design,
                                      int32_t x, int32_t y,
                                      int32_t width, int32_t height,
                                      int enabled, uint32_t state)
{
    if (!design || width < 36 || height < 20 ||
        !vanta_design_has_capacity(design, 2U) ||
        (state != VANTA_DESIGN_STATE_DEFAULT &&
         state != VANTA_DESIGN_STATE_DISABLED))
        return 0;
    uint32_t track = enabled ? vanta_design_accent_color(design) :
                               vanta_design_border_color(design);
    if (state == VANTA_DESIGN_STATE_DISABLED)
        track = vanta_theme_row_color(&design->theme);
    uint32_t radius = (uint32_t)height / 2U;
    if (!vanta_design_squircle_rect(design, x, y, width, height,
                                    radius, track))
        return 0;
    int32_t inset = 3;
    int32_t diameter = height - inset * 2;
    int32_t thumb_x = enabled ? x + width - diameter - inset : x + inset;
    return vanta_design_squircle_rect(design, thumb_x, y + inset,
                                      diameter, diameter,
                                      (uint32_t)diameter / 2U,
                                      state == VANTA_DESIGN_STATE_DISABLED ?
                                      vanta_design_muted_color(design) :
                                      vanta_design_on_color(track));
}

static inline int vanta_design_text_field(vanta_design_t* design,
                                          int32_t x, int32_t y,
                                          int32_t width, int32_t height,
                                          const char* value, int focused,
                                          int disabled)
{
    if (!design || width < 24 || height < 20 ||
        !vanta_design_has_capacity(design, 3U))
        return 0;
    size_t length = 0U;
    if (value)
        while (value[length] && length <= VANTA_GUI_TEXT_MAX)
            length++;
    if (length > VANTA_GUI_TEXT_MAX ||
        (int64_t)length * 8 + 16 > width)
        return 0;

    uint32_t border = focused ? vanta_design_accent_color(design) :
                                vanta_design_border_color(design);
    uint32_t surface = disabled ? vanta_theme_row_color(&design->theme) :
                                  vanta_design_surface_color(design);
    if (!vanta_design_squircle_rect(design, x, y, width, height,
                                    VANTA_DESIGN_RADIUS_CONTROL, border) ||
        !vanta_design_squircle_rect(design, x + 1, y + 1, width - 2,
                                    height - 2,
                                    VANTA_DESIGN_RADIUS_CONTROL - 1U,
                                    surface))
        return 0;
    if (length == 0U)
        return 1;
    return vanta_design_text(design, x + 8, y + (height - 16) / 2,
                             value,
                             disabled ? vanta_design_muted_color(design) :
                                        vanta_design_text_color(design),
                             1U);
}

#endif
