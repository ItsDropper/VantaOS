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

    failures += expect(vanta_design_accent_color(&design) == 0x0014B8A6U,
                       "theme accent resolves through shared tokens");
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

    failures += expect(vanta_design_on_color(0x00D97706U) == 0x00101820U,
                       "bright amber uses dark foreground");
    failures += expect(vanta_design_on_color(0x004B9CFFU) == 0x00101820U,
                       "bright blue accent uses dark foreground");
    failures += expect(vanta_design_on_color(0x00102030U) == 0x00FFFFFFU,
                       "dark surfaces use light foreground");

    size_t before_controls = design.count;
    failures += expect(vanta_design_outline_button(
                       &design, 0, 0, 112, 28, "Cancel",
                       VANTA_DESIGN_STATE_DEFAULT),
                       "outline button appends atomically");
    failures += expect(design.count == before_controls + 3U,
                       "outline button emits border, surface and label");
    failures += expect(commands[before_controls].color ==
                       vanta_design_border_color(&design) &&
                       commands[before_controls + 1U].color ==
                       vanta_design_surface_color(&design),
                       "outline button uses border and neutral fill");
    failures += expect(vanta_design_badge(
                       &design, 0, 0, "Ready",
                       VANTA_DESIGN_STATE_SELECTED),
                       "badge uses theme accent");
    failures += expect(vanta_design_badge(
                       &design, 0, 0, "Connected",
                       VANTA_DESIGN_STATE_SUCCESS),
                       "success badge uses semantic success color");
    failures += expect(commands[design.count - 2U].color ==
                       vanta_design_success_color(),
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
