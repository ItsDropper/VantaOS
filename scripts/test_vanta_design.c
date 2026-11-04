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
    vanta_design_init(&design, commands, 6U, &theme);

    int failures = 0;
    failures += expect(design.count == 0U && design.capacity == 6U,
                       "initial draw-list state");
    failures += expect(vanta_design_radius(5, 9, 10U) == 2U,
                       "radius clamps to half the smaller dimension");
    failures += expect(vanta_design_radius(100, 80, 40U) == 32U,
                       "radius has a bounded raster cost");

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

    failures += expect(vanta_design_accent_color(&design) == 0x0014B8A6U,
                       "theme accent resolves through shared tokens");
    failures += expect(!vanta_design_squircle_rect(
                       &design, 0, 0, 0, 10, 4U, 0U),
                       "reject empty geometry");

    failures += expect(vanta_design_rect(&design, 0, 0, 1, 1, 0U),
                       "capacity permits final command");
    failures += expect(!vanta_design_rect(&design, 0, 0, 1, 1, 0U),
                       "draw-list capacity is enforced");

    if (failures)
        return 1;
    puts("VANTAOS_DESIGN_SYSTEM_OK");
    return 0;
}
