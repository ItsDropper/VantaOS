/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include <stdio.h>
#include <vanta/design.h>

static int expect(int condition, const char* message)
{
    if (condition)
        return 0;
    fprintf(stderr, "VDS test failed: %s\n", message);
    return 1;
}

int main(void)
{
    vanta_gui_draw_command_t commands[VANTA_GUI_MAX_COMMANDS];
    vanta_theme_t theme = {
        .accent = 1U, .light = 0, .contrast = 0, .compact = 0
    };
    vanta_design_t design;
    vanta_design_init(&design, commands, 32U, &theme);

    int failures = 0;
    failures += expect(design.count == 0U && design.capacity == 32U,
                       "initial draw-list state");
    failures += expect(vanta_design_radius(5, 9, 10U) == 2U,
                       "radius clamps to half the smaller dimension");
    failures += expect(vanta_design_radius(100, 80, 40U) == 32U,
                       "radius has a bounded raster cost");
    failures += expect(vanta_design_radius(4, 4, 10U) == 1U,
                       "radius avoids overlapping corners on tiny surfaces");

    failures += expect(vanta_design_squircle_rect(
        &design, 0, 0, 100, 60, VANTA_DESIGN_RADIUS_SURFACE, 0x00112233U),
        "append squircle command");
    failures += expect(commands[0].type == VANTA_GUI_DRAW_FILL_SQUIRCLE_RECT &&
                       commands[0].scale == VANTA_DESIGN_RADIUS_SURFACE,
                       "squircle ABI encoding");

    failures += expect(vanta_design_panel(&design, 4, 4, 80, 48),
                       "append panel layers");
    failures += expect(vanta_design_button(&design, 8, 56, 80, 24, "Apply",
                       VANTA_DESIGN_STATE_SELECTED),
                       "append selected button");
    failures += expect(commands[design.count - 2U].type ==
                       VANTA_GUI_DRAW_FILL_SQUIRCLE_RECT,
                       "button background uses shared squircle geometry");
    failures += expect(commands[design.count - 1U].type ==
                       VANTA_GUI_DRAW_TEXT,
                       "button label is emitted as text");

    failures += expect(vanta_design_accent_color(&design) == 0x000D9488U,
                       "theme accent resolves through shared tokens");
    failures += expect(vanta_design_text_width("Vanta", 1U) == 40U,
                       "text width follows conservative ABI glyph advance");
    failures += expect(vanta_design_text_width("Vanta", 2U) == 80U &&
                       vanta_design_text_height(2U) == 32U,
                       "scaled text dimensions follow the ABI");
    failures += expect(vanta_design_text_fits("Vanta", 1U, 40, 16),
                       "text fits exact measured client bounds");
    failures += expect(!vanta_design_text_fits("Vanta", 1U, 39, 16),
                       "text rejects one-pixel width overflow");
    failures += expect(!vanta_design_text_fits("Vanta", 2U, 80, 31),
                       "text rejects insufficient scaled height");
    failures += expect(!vanta_design_squircle_rect(
                       &design, 0, 0, 0, 10, 4U, 0U),
                       "reject empty geometry");

    vanta_gui_draw_command_t one_command[1];
    vanta_design_t one;
    vanta_design_init(&one, one_command, 1U, &theme);
    failures += expect(vanta_design_rect(&one, 0, 0, 1, 1, 0U),
                       "capacity permits the final single command");
    failures += expect(!vanta_design_rect(&one, 0, 0, 1, 1, 0U),
                       "draw-list capacity is enforced");

    failures += expect(vanta_design_contrast_ratio_milli(
                       0x00FFFFFFU, 0x00000000U) == 21000U,
                       "black and white achieve a 21:1 contrast ratio");
    failures += expect(vanta_design_contrast_ratio_milli(
                       0x00000000U, 0x00FFFFFFU) == 21000U,
                       "contrast ratio is symmetric");
    failures += expect(vanta_design_on_color(0x00D97706U) == 0x00101820U,
                       "amber accent selects the stronger foreground");
    failures += expect(vanta_design_on_color(0x004B9CFFU) == 0x00101820U,
                       "blue accent selects the stronger foreground");
    failures += expect(vanta_design_on_color(0x00102030U) == 0x00FFFFFFU,
                       "dark surfaces use light foreground");

    {
        vanta_theme_t light_theme = {
            .accent = 0U, .light = 1, .contrast = 0, .compact = 0
        };
        vanta_theme_t high_contrast_theme = {
            .accent = 0U, .light = 1, .contrast = 1, .compact = 0
        };
        design.theme = light_theme;
        uint32_t light_success = vanta_design_success_color(&design);
        uint32_t light_warning = vanta_design_warning_color(&design);
        uint32_t light_danger = vanta_design_danger_color(&design);
        failures += expect(vanta_design_contrast_ratio_milli(
                           vanta_design_on_color(light_success),
                           light_success) >= 4500U,
                           "light success badge has readable foreground");
        failures += expect(vanta_design_contrast_ratio_milli(
                           vanta_design_on_color(light_warning),
                           light_warning) >= 4500U,
                           "light warning badge has readable foreground");
        failures += expect(vanta_design_contrast_ratio_milli(
                           vanta_design_on_color(light_danger),
                           light_danger) >= 4500U,
                           "light danger badge has readable foreground");
        design.theme = high_contrast_theme;
        failures += expect(vanta_design_success_color(&design) !=
                           light_success &&
                           vanta_design_warning_color(&design) !=
                           light_warning &&
                           vanta_design_danger_color(&design) !=
                           light_danger,
                           "high contrast theme changes semantic status colors");
    }
    const uint32_t accent_swatches[] = {
        0x003B82F6U, 0x000D9488U, 0x00C56A05U, 0x00936CF0U,
        0x0022C55EU, 0x00EF4444U
    };
    for (size_t i = 0U; i < sizeof(accent_swatches) /
         sizeof(accent_swatches[0]); ++i)
    {
        uint32_t foreground = vanta_design_on_color(accent_swatches[i]);
        failures += expect(vanta_design_contrast_ratio_milli(
                           foreground, accent_swatches[i]) >= 4500U,
                           "accent foreground reaches at least 4.5:1 contrast");
    {
        vanta_theme_t selection_themes[] = {
            { .accent = 0U, .light = 0, .contrast = 0, .compact = 0 },
            { .accent = 0U, .light = 1, .contrast = 0, .compact = 0 },
            { .accent = 0U, .light = 0, .contrast = 1, .compact = 0 },
            { .accent = 0U, .light = 1, .contrast = 1, .compact = 0 }
        };
        for (size_t i = 0U; i < sizeof(selection_themes) /
             sizeof(selection_themes[0]); ++i)
        {
            design.theme = selection_themes[i];
            uint32_t selection = vanta_theme_selection_color(&design.theme);
            uint32_t foreground = vanta_design_on_color(selection);
            failures += expect(vanta_design_contrast_ratio_milli(
                               foreground, selection) >= 4500U,
                               "selection text meets contrast in every theme");
        }
    }
        uint32_t dark_ratio = vanta_design_contrast_ratio_milli(
            0x00101820U, accent_swatches[i]);
        uint32_t light_ratio = vanta_design_contrast_ratio_milli(
            0x00FFFFFFU, accent_swatches[i]);
        failures += expect(
            vanta_design_contrast_ratio_milli(foreground,
                accent_swatches[i]) ==
                (dark_ratio > light_ratio ? dark_ratio : light_ratio),
            "foreground helper selects the stronger real contrast ratio");
    }

    size_t before_controls = design.count;
    failures += expect(vanta_design_outline_button(
                       &design, 0, 0, 112, 28, "Cancel",
                       VANTA_DESIGN_STATE_DEFAULT),
                       "outline button appends atomically");
    size_t before_narrow = design.count;
    failures += expect(!vanta_design_button(
                       &design, 0, 0, 72, 24, "ABCDEFGHIJ",
                       VANTA_DESIGN_STATE_DEFAULT),
                       "button rejects label outside the ABI text bound");
    failures += expect(design.count == before_narrow,
                       "rejected button leaves draw list unchanged");
    failures += expect(design.count == before_controls + 3U,
                       "outline button emits border, surface and label");
    size_t outlined_start = design.count;
    failures += expect(vanta_design_outline_button(
                       &design, 0, 0, 112, 28, "Selected",
                       VANTA_DESIGN_STATE_SELECTED),
                       "selected outline button renders");
    failures += expect(commands[outlined_start].color ==
                       vanta_design_accent_color(&design) &&
                       commands[outlined_start + 2U].color ==
                       vanta_design_text_color(&design),
                       "selected outline uses accent border and readable text");
    outlined_start = design.count;
    failures += expect(vanta_design_outline_button(
                       &design, 0, 0, 112, 28, "Delete",
                       VANTA_DESIGN_STATE_DANGER),
                       "danger outline button renders");
    failures += expect(commands[outlined_start].color ==
                       vanta_design_danger_color(&design) &&
                       commands[outlined_start + 2U].color ==
                       vanta_design_text_color(&design),
                       "danger outline preserves readable text color");
    failures += expect(commands[before_controls].color ==
                       vanta_design_border_color(&design) &&
                       commands[before_controls + 1U].color ==
                       vanta_design_surface_color(&design),
                       "outline button uses border and neutral fill");
    size_t badge_start = design.count;
    failures += expect(vanta_design_badge(
                       &design, 0, 0, "Ready",
                       VANTA_DESIGN_STATE_SELECTED),
                       "badge uses theme accent");
    failures += expect(commands[badge_start].width >= 8 * 5 + 16 &&
                       commands[badge_start + 1U].x + 8 * 5 <=
                       commands[badge_start].width,
                       "badge text fits conservative GUI ABI bounds");
    failures += expect(vanta_design_badge(
                       &design, 0, 0, "Connected",
                       VANTA_DESIGN_STATE_SUCCESS),
                       "success badge uses semantic success color");
    failures += expect(commands[design.count - 2U].color ==
                       vanta_design_success_color(&design),
                       "success badge color token");
    failures += expect(vanta_design_badge(
                       &design, 0, 0, "Pending",
                       VANTA_DESIGN_STATE_WARNING),
                       "warning badge uses semantic warning color");
    failures += expect(vanta_design_switch(
                       &design, 0, 0, 42, 22, 1,
                       VANTA_DESIGN_STATE_DEFAULT),
                       "switch emits track and thumb");
    failures += expect(vanta_design_text_field(
                       &design, 0, 0, 160, 28, "Vanta", 1, 0),
                       "text field emits focus outline and value");
    failures += expect(vanta_design_separator(&design, 0, 0, 40),
                       "separator uses semantic border color");

    vanta_gui_draw_command_t tiny_commands[1];
    vanta_design_t tiny;
    vanta_design_init(&tiny, tiny_commands, 1U, &theme);
    failures += expect(!vanta_design_button(
                       &tiny, 0, 0, 100, 24, "OK",
                       VANTA_DESIGN_STATE_DEFAULT),
                       "compound button refuses partial draw-list writes");
    failures += expect(tiny.count == 0U,
                       "failed compound control leaves draw list unchanged");
    failures += expect(!vanta_design_panel(&tiny, 0, 0, 40, 20),
                       "panel refuses insufficient command capacity");
    failures += expect(tiny.count == 0U,
                       "failed panel leaves draw list unchanged");
    if (failures)
        return 1;
    puts("VANTAOS_DESIGN_SYSTEM_OK");
    return 0;
}
