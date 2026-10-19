/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef VANTA_DESKTOP_THEME_H
#define VANTA_DESKTOP_THEME_H

#include <stdint.h>

/* Theme roles are resolved at runtime from the persisted Settings config. */
#define VANTA_UI_ROLE_DESKTOP        0U
#define VANTA_UI_ROLE_TOPBAR         1U
#define VANTA_UI_ROLE_SURFACE        2U
#define VANTA_UI_ROLE_TITLEBAR       3U
#define VANTA_UI_ROLE_TOOLBAR        4U
#define VANTA_UI_ROLE_ROW            5U
#define VANTA_UI_ROLE_HOVER          6U
#define VANTA_UI_ROLE_BORDER         7U
#define VANTA_UI_ROLE_TEXT           8U
#define VANTA_UI_ROLE_TEXT_SECONDARY 9U
#define VANTA_UI_ROLE_ACCENT         10U
#define VANTA_UI_ROLE_SUCCESS        11U
#define VANTA_UI_ROLE_DANGER         12U

uint32_t desktop_theme_color(unsigned int role);
int desktop_theme_compact_rows(void);
int desktop_theme_reload(void);

#define VANTA_UI_DESKTOP        desktop_theme_color(VANTA_UI_ROLE_DESKTOP)
#define VANTA_UI_TOPBAR         desktop_theme_color(VANTA_UI_ROLE_TOPBAR)
#define VANTA_UI_SURFACE        desktop_theme_color(VANTA_UI_ROLE_SURFACE)
#define VANTA_UI_TITLEBAR       desktop_theme_color(VANTA_UI_ROLE_TITLEBAR)
#define VANTA_UI_TOOLBAR        desktop_theme_color(VANTA_UI_ROLE_TOOLBAR)
#define VANTA_UI_ROW            desktop_theme_color(VANTA_UI_ROLE_ROW)
#define VANTA_UI_HOVER          desktop_theme_color(VANTA_UI_ROLE_HOVER)
#define VANTA_UI_BORDER         desktop_theme_color(VANTA_UI_ROLE_BORDER)
#define VANTA_UI_TEXT           desktop_theme_color(VANTA_UI_ROLE_TEXT)
#define VANTA_UI_TEXT_SECONDARY desktop_theme_color(VANTA_UI_ROLE_TEXT_SECONDARY)
#define VANTA_UI_ACCENT         desktop_theme_color(VANTA_UI_ROLE_ACCENT)
#define VANTA_UI_SUCCESS        desktop_theme_color(VANTA_UI_ROLE_SUCCESS)
#define VANTA_UI_DANGER         desktop_theme_color(VANTA_UI_ROLE_DANGER)

#endif
