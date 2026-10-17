#ifndef VANTA_USER_THEME_H
#define VANTA_USER_THEME_H

#include <stddef.h>
#include <stdint.h>
#include <vanta/syscall.h>

#define VANTA_THEME_CONFIG_PATH "/SYSTEM/VANTASET.CFG"
#define VANTA_THEME_CONFIG_CAPACITY 256U

typedef struct
{
    uint32_t accent;
    int light;
    int contrast;
    int compact;
} vanta_theme_t;

static inline int vanta_theme_contains(const char* text, const char* needle)
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

/* Read the same versioned preferences consumed by the kernel desktop theme. */
static inline vanta_theme_t vanta_theme_load(void)
{
    vanta_theme_t theme = { .accent = 0U, .light = 0, .contrast = 0,
                            .compact = 0 };
    char buffer[VANTA_THEME_CONFIG_CAPACITY];
    long descriptor = vanta_open(VANTA_THEME_CONFIG_PATH, VANTA_OPEN_READ);
    if (descriptor < 0)
        return theme;

    long count = vanta_read((int)descriptor, buffer, sizeof(buffer) - 1U);
    (void)vanta_close((int)descriptor);
    if (count <= 0 || (size_t)count >= sizeof(buffer))
        return theme;
    buffer[(size_t)count] = '\0';
    if (!vanta_theme_contains(buffer, "VANTA_SETTINGS=1\n") &&
        !vanta_theme_contains(buffer, "VANTA_SETTINGS=2\n"))
        return theme;

    if (vanta_theme_contains(buffer, "accent=teal\n")) theme.accent = 1U;
    else if (vanta_theme_contains(buffer, "accent=amber\n")) theme.accent = 2U;
    else if (vanta_theme_contains(buffer, "accent=violet\n")) theme.accent = 3U;
    else if (vanta_theme_contains(buffer, "accent=blue\n")) theme.accent = 0U;
    theme.light = vanta_theme_contains(buffer, "theme=light\n");
    theme.contrast = vanta_theme_contains(buffer, "contrast=1\n");
    theme.compact = vanta_theme_contains(buffer, "compact=1\n");
    return theme;
}

static inline uint32_t vanta_theme_accent_color(const vanta_theme_t* theme)
{
    if (theme && theme->accent == 1U) return 0x0014B8A6U;
    if (theme && theme->accent == 2U) return 0x00D97706U;
    if (theme && theme->accent == 3U) return 0x008B5CF6U;
    return 0x004B9CFFU;
}

static inline uint32_t vanta_theme_text_color(const vanta_theme_t* theme)
{
    if (theme && theme->contrast)
        return theme->light ? 0x00000000U : 0x00FFFFFFU;
    return theme && theme->light ? 0x00182735U : 0x00E8EDF4U;
}

static inline uint32_t vanta_theme_muted_color(const vanta_theme_t* theme)
{
    if (theme && theme->contrast)
        return vanta_theme_text_color(theme);
    return theme && theme->light ? 0x00556678U : 0x008D9BACU;
}

static inline uint32_t vanta_theme_surface_color(const vanta_theme_t* theme)
{
    if (theme && theme->contrast)
        return theme->light ? 0x00FFFFFFU : 0x00000000U;
    return theme && theme->light ? 0x00F7F9FCU : 0x000C1119U;
}

static inline uint32_t vanta_theme_row_color(const vanta_theme_t* theme)
{
    if (theme && theme->contrast)
        return vanta_theme_surface_color(theme);
    return theme && theme->light ? 0x00E8EEF5U : 0x001D2835U;
}

static inline uint32_t vanta_theme_border_color(const vanta_theme_t* theme)
{
    if (theme && theme->contrast)
        return theme->light ? 0x00000000U : 0x00FFFFFFU;
    return theme && theme->light ? 0x00C4CFDCU : 0x002A3746U;
}

#endif
