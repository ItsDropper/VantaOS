/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "desktop_theme.h"

#include "vfs.h"
#ifdef VANTAOS_SELF_TEST
#include "serial.h"
#endif

#include <stddef.h>
#include <stdint.h>

#define THEME_CONFIG_PATH "/SYSTEM/VANTASET.CFG"
#define THEME_CONFIG_CAPACITY 256U

static uint32_t theme_accent;
static int theme_light;
static int theme_contrast;
static int theme_compact;
static int theme_initialized;

static int theme_contains(const char* text, const char* needle)
{
    if (!text || !needle || !needle[0])
        return 0;
    for (size_t i = 0U; text[i]; i++)
    {
        size_t j = 0U;
        while (needle[j] && text[i + j] == needle[j])
            j++;
        if (!needle[j])
            return 1;
    }
    return 0;
}

static void theme_defaults(void)
{
    theme_accent = 0U;
    theme_light = 0;
    theme_contrast = 0;
    theme_compact = 0;
    theme_initialized = 1;
}

int desktop_theme_reload(void)
{
    theme_defaults();
    if (!vfs_is_initialized())
    {
        theme_initialized = 0;
        return 0;
    }

    char buffer[THEME_CONFIG_CAPACITY];
    int descriptor = vfs_open(THEME_CONFIG_PATH, VFS_OPEN_READ);
    if (descriptor < 0)
        return 0;

    int count = vfs_read(descriptor, buffer, sizeof(buffer) - 1U);
    (void)vfs_close(descriptor);
    if (count <= 0 || (size_t)count >= sizeof(buffer))
        return 0;
    buffer[(size_t)count] = '\0';

    if (!theme_contains(buffer, "VANTA_SETTINGS=1\n") &&
        !theme_contains(buffer, "VANTA_SETTINGS=2\n"))
        return 0;

    if (theme_contains(buffer, "accent=teal\n"))
        theme_accent = 1U;
    else if (theme_contains(buffer, "accent=amber\n"))
        theme_accent = 2U;
    else if (theme_contains(buffer, "accent=violet\n"))
        theme_accent = 3U;
    else if (theme_contains(buffer, "accent=blue\n"))
        theme_accent = 0U;
    else
        return 0;

    theme_light = theme_contains(buffer, "theme=light\n");
    theme_contrast = theme_contains(buffer, "contrast=1\n");
    theme_compact = theme_contains(buffer, "compact=1\n");
#ifdef VANTAOS_SELF_TEST
    serial_write("VANTAOS_DESKTOP_THEME_CONFIG_OK\n");
#endif
    return 1;
}

int desktop_theme_compact_rows(void)
{
    if (!theme_initialized)
        (void)desktop_theme_reload();
    return theme_compact;
}

uint32_t desktop_theme_color(unsigned int role)
{
    if (!theme_initialized)
        (void)desktop_theme_reload();

    uint32_t accent = theme_accent == 1U ? 0x0014B8A6U :
        theme_accent == 2U ? 0x00D97706U :
        theme_accent == 3U ? 0x008B5CF6U : 0x004B9CFFU;

    if (theme_contrast)
    {
        switch (role)
        {
            case VANTA_UI_ROLE_DESKTOP: return theme_light ? 0x00FFFFFFU : 0x00000000U;
            case VANTA_UI_ROLE_TOPBAR:
            case VANTA_UI_ROLE_SURFACE:
            case VANTA_UI_ROLE_TITLEBAR:
            case VANTA_UI_ROLE_TOOLBAR:
            case VANTA_UI_ROLE_ROW:
            case VANTA_UI_ROLE_HOVER:
                return theme_light ? 0x00FFFFFFU : 0x00000000U;
            case VANTA_UI_ROLE_BORDER: return theme_light ? 0x00000000U : 0x00FFFFFFU;
            case VANTA_UI_ROLE_TEXT: return theme_light ? 0x00000000U : 0x00FFFFFFU;
            case VANTA_UI_ROLE_TEXT_SECONDARY: return theme_light ? 0x00000000U : 0x00FFFFFFU;
            case VANTA_UI_ROLE_ACCENT: return accent;
            case VANTA_UI_ROLE_SUCCESS: return 0x0000A000U;
            case VANTA_UI_ROLE_DANGER: return 0x00C00000U;
            default: return 0x00000000U;
        }
    }

    if (theme_light)
    {
        switch (role)
        {
            case VANTA_UI_ROLE_DESKTOP: return 0x00F3F6FAU;
            case VANTA_UI_ROLE_TOPBAR: return 0x00FFFFFFU;
            case VANTA_UI_ROLE_SURFACE: return 0x00FFFFFFU;
            case VANTA_UI_ROLE_TITLEBAR: return 0x00E5EBF2U;
            case VANTA_UI_ROLE_TOOLBAR: return 0x00EEF2F7U;
            case VANTA_UI_ROLE_ROW: return 0x00E8EEF5U;
            case VANTA_UI_ROLE_HOVER: return 0x00DCE6F1U;
            case VANTA_UI_ROLE_BORDER: return 0x00C4CFDCU;
            case VANTA_UI_ROLE_TEXT: return 0x00182735U;
            case VANTA_UI_ROLE_TEXT_SECONDARY: return 0x00556678U;
            case VANTA_UI_ROLE_ACCENT: return accent;
            case VANTA_UI_ROLE_SUCCESS: return 0x002A8C60U;
            case VANTA_UI_ROLE_DANGER: return 0x00B83C49U;
            default: return 0x00FFFFFFU;
        }
    }

    switch (role)
    {
        case VANTA_UI_ROLE_DESKTOP: return 0x000C1119U;
        case VANTA_UI_ROLE_TOPBAR: return 0x00151C26U;
        case VANTA_UI_ROLE_SURFACE: return 0x000F151EU;
        case VANTA_UI_ROLE_TITLEBAR: return 0x001A2430U;
        case VANTA_UI_ROLE_TOOLBAR: return 0x0017202BU;
        case VANTA_UI_ROLE_ROW: return 0x001D2835U;
        case VANTA_UI_ROLE_HOVER: return 0x00232F3DU;
        case VANTA_UI_ROLE_BORDER: return 0x002A3746U;
        case VANTA_UI_ROLE_TEXT: return 0x00E8EDF4U;
        case VANTA_UI_ROLE_TEXT_SECONDARY: return 0x008D9BACU;
        case VANTA_UI_ROLE_ACCENT: return accent;
        case VANTA_UI_ROLE_SUCCESS: return 0x0057B77EU;
        case VANTA_UI_ROLE_DANGER: return 0x00D56A70U;
        default: return 0x000F151EU;
    }
}
