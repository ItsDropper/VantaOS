#include "graphics_internal.h"
#include "desktop_theme.h"
#include "filesystem.h"
#include "process.h"

/* Native, software-rendered desktop design system. */
#define UI_INK 0x000A111B
#define UI_INK_SOFT 0x000E1723
#define UI_SURFACE_0 0x00131D29
#define UI_SURFACE_1 0x00192533
#define UI_SURFACE_2 0x00202F3F
#define UI_EDGE 0x00334455
#define UI_EDGE_SOFT 0x00233140
#define UI_WHITE 0x00F3F6FA
#define UI_MUTED 0x009AAABD
#define UI_QUIET 0x006E8296
#define UI_BLUE 0x005DA9FF
#define UI_BLUE_BRIGHT 0x0078B8FF
#define UI_BLUE_DARK 0x00234162
#define UI_MINT 0x0069D6AE
#define UI_AMBER 0x00E6BD78

static int ui_min(int a, int b) { return a < b ? a : b; }
static int ui_max(int a, int b) { return a > b ? a : b; }
static int ui_clamp(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}
static void ui_line(int x, int y, int w, uint32_t c)
{
    if (w > 0) graphics_fill_rect(x, y, w, 1, c);
}
static void ui_vline(int x, int y, int h, uint32_t c)
{
    if (h > 0) graphics_fill_rect(x, y, 1, h, c);
}
static void ui_surface(int x, int y, int w, int h, int r,
                       uint32_t fill, uint32_t edge)
{
    if (w <= 0 || h <= 0) return;
    graphics_fill_rounded_rect(x, y, w, h, r, fill);
    if (edge != 0)
    {
        ui_line(x + r, y, w - r * 2, edge);
        ui_line(x + r, y + h - 1, w - r * 2, edge);
        graphics_fill_rect(x, y + r, 1, h - r * 2, edge);
        graphics_fill_rect(x + w - 1, y + r, 1, h - r * 2, edge);
    }
}
static void ui_label(int x, int y, const char* s)
{
    graphics_draw_text(x, y, s, UI_QUIET, 1);
}
static void ui_accent(int x, int y, int w, uint32_t c)
{
    graphics_fill_rounded_rect(x, y, w, 3, 1, c);
}
static void ui_dot(int x, int y, uint32_t c)
{
    graphics_fill_rounded_rect(x, y, 6, 6, 3, c);
}
static void ui_brand(int x, int y, int size)
{
    int bar = ui_max(3, size / 7);
    int left = x + size / 3;
    int right = x + size / 2 + bar;
    int bottom = y + size - bar * 2;
    graphics_fill_rounded_rect(left, y, bar, size, bar / 2, UI_BLUE);
    graphics_fill_rounded_rect(right, y, bar, size, bar / 2, UI_BLUE_BRIGHT);
    graphics_fill_rounded_rect(left, bottom, right - left + bar, bar * 2,
                               bar / 2, UI_MINT);
}
static void ui_icon_terminal(int x, int y, int s, uint32_t tint)
{
    ui_surface(x, y, s, s, 7, UI_SURFACE_2, UI_EDGE);
    graphics_draw_text(x + ui_max(3, s / 6), y + s / 2 - 3, ">_", tint, 1);
}
static void ui_icon_folder(int x, int y, int s, uint32_t tint)
{
    ui_surface(x, y, s, s, 7, UI_SURFACE_2, UI_EDGE);
    graphics_fill_rounded_rect(x + 7, y + s / 2 - 3, s - 14, s / 3, 3, tint);
    graphics_fill_rounded_rect(x + 8, y + s / 2 - 7, s / 3, 6, 2, tint);
}
static void ui_icon_system(int x, int y, int s, uint32_t tint)
{
    ui_surface(x, y, s, s, 7, UI_SURFACE_2, UI_EDGE);
    graphics_fill_rounded_rect(x + s / 3, y + s / 3, s / 3, s / 3, 4, tint);
    ui_vline(x + s / 2, y + 5, 3, tint);
    ui_vline(x + s / 2, y + s - 8, 3, tint);
    ui_line(x + 5, y + s / 2, 3, tint);
    ui_line(x + s - 8, y + s / 2, 3, tint);
}
static void ui_icon_settings(int x, int y, int s, uint32_t tint)
{
    ui_surface(x, y, s, s, 7, UI_SURFACE_2, UI_EDGE);
    graphics_fill_rounded_rect(x + s / 3, y + s / 3, s / 3, s / 3, 3, tint);
    ui_vline(x + s / 2, y + 5, 3, tint);
    ui_vline(x + s / 2, y + s - 8, 3, tint);
    ui_line(x + 5, y + s / 2, 3, tint);
    ui_line(x + s - 8, y + s / 2, 3, tint);
}
static void ui_icon_chip(int x, int y, int s, uint32_t tint)
{
    ui_surface(x, y, s, s, 7, UI_SURFACE_2, UI_EDGE);
    graphics_fill_rounded_rect(x + 9, y + 9, s - 18, s - 18, 3, tint);
    for (int i = 0; i < 3; ++i)
    {
        int o = 8 + i * 7;
        graphics_fill_rect(x + o, y + 4, 2, 5, tint);
        graphics_fill_rect(x + o, y + s - 9, 2, 5, tint);
        graphics_fill_rect(x + 4, y + o, 5, 2, tint);
        graphics_fill_rect(x + s - 9, y + o, 5, 2, tint);
    }
}
static void ui_icon_memory(int x, int y, int s, uint32_t tint)
{
    ui_surface(x, y, s, s, 7, UI_SURFACE_2, UI_EDGE);
    graphics_fill_rounded_rect(x + 7, y + s / 3, s - 14, s / 3, 3, tint);
    for (int i = 0; i < 3; ++i)
        graphics_fill_rect(x + 11 + i * 7, y + s / 3 + 3, 3,
                           s / 3 - 6, UI_SURFACE_2);
    graphics_fill_rect(x + 10, y + s / 3 - 4, 2, 4, tint);
    graphics_fill_rect(x + s - 12, y + s / 3 - 4, 2, 4, tint);
}
static void ui_metric(int x, int y, int w, int h, const char* eyebrow,
                      const char* value, const char* detail,
                      uint32_t accent, int icon)
{
    ui_surface(x, y, w, h, 10, UI_SURFACE_0, UI_EDGE_SOFT);
    ui_accent(x + 14, y + 13, 28, accent);
    ui_label(x + 14, y + 27, eyebrow);
    if (w >= 175)
    {
        if (icon == 0) ui_icon_system(x + w - 46, y + 12, 32, accent);
        else if (icon == 1) ui_icon_folder(x + w - 46, y + 12, 32, accent);
        else if (icon == 2) ui_icon_terminal(x + w - 46, y + 12, 32, accent);
        else if (icon == 3) ui_icon_chip(x + w - 46, y + 12, 32, accent);
        else ui_icon_memory(x + w - 46, y + 12, 32, accent);
    }
    graphics_draw_text(x + 14, y + 49, value, UI_WHITE, 1);
    graphics_draw_text(x + 14, y + h - 22, detail, UI_MUTED, 1);
}
static void ui_detail_row(int x, int y, int w, const char* label,
                          const char* value, uint32_t status)
{
    ui_line(x, y + 24, w, UI_EDGE_SOFT);
    ui_dot(x + 1, y + 9, status);
    graphics_draw_text(x + 15, y + 8, label, UI_MUTED, 1);
    graphics_draw_text(x + w - 170, y + 8, value, UI_WHITE, 1);
}
static void ui_app_tile(int x, int y, int w, int h, const char* name,
                        const char* desc, int kind, uint32_t tint)
{
    ui_surface(x, y, w, h, 9, UI_SURFACE_0, UI_EDGE_SOFT);
    if (kind == 0) ui_icon_terminal(x + 12, y + 12, 36, tint);
    else if (kind == 1) ui_icon_folder(x + 12, y + 12, 36, tint);
    else if (kind == 2) ui_icon_system(x + 12, y + 12, 36, tint);
    else ui_icon_settings(x + 12, y + 12, 36, tint);
    graphics_draw_text(x + 58, y + 15, name, UI_WHITE, 1);
    graphics_draw_text(x + 58, y + 33, desc, UI_MUTED, 1);
}

/* Layered, opaque shapes work on the current renderer without pretending to
 * support alpha compositing, blur, or GPU acceleration. */
static void ui_wallpaper(int w, int h)
{
    graphics_fill_rect(0, 0, w, h, UI_INK);
    graphics_fill_rounded_rect(w / 2 - 30, 84, w / 2 + 350,
                               h - 190, 90, UI_INK_SOFT);
    graphics_fill_rounded_rect(w / 2 + 70, 142, w / 2 + 180,
                               h - 300, 90, 0x00101D2B);
    graphics_fill_rounded_rect(w / 2 + 170, 200, w / 2 + 40,
                               h - 420, 80, 0x00132232);
    for (int gx = 350; gx < w; gx += 32)
        ui_vline(gx, 70, h - 150, 0x00111D29);
    for (int gy = 88; gy < h - 76; gy += 32)
        ui_line(320, gy, w - 340, 0x00111D29);
    int mark_size = ui_min(220, h / 3);
    if (mark_size > 80)
    {
        int mx = w - mark_size - 60;
        int my = h / 2 - mark_size / 2 - 10;
        graphics_fill_rounded_rect(mx - 28, my - 28, mark_size + 56,
                                   mark_size + 56, 24, 0x00111F2E);
        ui_brand(mx + mark_size / 4, my, mark_size / 2);
    }
    ui_line(0, 42, w, 0x00273545);
    ui_vline(0, 43, h - 107, 0x00121D29);
    graphics_fill_rect(0, h - 64, w, 64, 0x00080D13);
}
static void ui_topbar(int w)
{
    graphics_fill_rect(0, 0, w, 42, 0x00111A25);
    ui_line(0, 41, w, UI_EDGE_SOFT);
    ui_surface(14, 8, 26, 26, 7, UI_SURFACE_2, UI_EDGE_SOFT);
    ui_brand(21, 12, 18);
    graphics_draw_text(50, 16, "VantaOS", UI_WHITE, 1);
    ui_vline(113, 13, 16, UI_EDGE);
    graphics_draw_text(127, 16, "Workspace", UI_MUTED, 1);
    ui_surface(w - 151, 10, 131, 22, 7, UI_SURFACE_0, UI_EDGE_SOFT);
    ui_dot(w - 139, 18, filesystem_is_initialized() ? UI_MINT : UI_AMBER);
    graphics_draw_text(w - 127, 17,
        filesystem_is_initialized() ? "Filesystem ready" : "Booting services",
        UI_MUTED, 1);
}
static void ui_shortcuts(void)
{
    /* These bounds intentionally stay in sync with graphics_input.c. */
    ui_surface(24, 58, 88, 72, 10,
        active_panel == 1 ? UI_SURFACE_2 : UI_SURFACE_0, UI_EDGE_SOFT);
    ui_icon_system(49, 70, 38, UI_BLUE);
    graphics_draw_text(43, 114, "System", UI_WHITE, 1);
    ui_surface(120, 58, 88, 72, 10,
        active_panel == 2 ? UI_SURFACE_2 : UI_SURFACE_0, UI_EDGE_SOFT);
    ui_icon_folder(145, 70, 38, UI_MINT);
    graphics_draw_text(150, 114, "Files", UI_WHITE, 1);
    ui_surface(216, 58, 88, 72, 10,
        active_panel == 3 ? UI_SURFACE_2 : UI_SURFACE_0, UI_EDGE_SOFT);
    ui_icon_terminal(241, 70, 38, UI_BLUE_BRIGHT);
    graphics_draw_text(228, 114, "Terminal", UI_WHITE, 1);
}
static void ui_welcome(int x, int y, int width)
{
    ui_surface(x, y, width, 126, 14, UI_SURFACE_0, UI_EDGE_SOFT);
    ui_accent(x + 22, y + 20, 38, UI_BLUE_BRIGHT);
    ui_label(x + 22, y + 34, "VANTAOS  /  LOCAL WORKSPACE");
    graphics_draw_text(x + 22, y + 56, "A workspace built", UI_WHITE, 1);
    graphics_draw_text(x + 22, y + 73, "around your system.", UI_WHITE, 1);
    graphics_draw_text(x + 22, y + 99,
        "Tools, files and system state in one place.", UI_MUTED, 1);
    int cx = x + width - 74;
    int cy = y + 62;
    graphics_fill_rounded_rect(cx - 34, cy - 34, 68, 68, 34, UI_SURFACE_1);
    graphics_fill_rounded_rect(cx - 25, cy - 25, 50, 50, 25, UI_SURFACE_2);
    ui_brand(cx - 10, cy - 18, 36);
}
static void ui_overview(int x, int y, int width)
{
    int gap = 10;
    int card_w = (width - gap * 2) / 3;
    ui_label(x, y, "SYSTEM OVERVIEW");
    ui_metric(x, y + 18, card_w, 88, "ARCHITECTURE",
        "x86_64", "64-bit kernel", UI_BLUE, 0);
    ui_metric(x + card_w + gap, y + 18, card_w, 88, "FILESYSTEM",
        filesystem_is_initialized() ? "Ready" : "Starting",
        filesystem_is_initialized() ? "VFS initialized" : "Not initialized",
        filesystem_is_initialized() ? UI_MINT : UI_AMBER, 1);
    ui_metric(x + (card_w + gap) * 2, y + 18, card_w, 88, "PROCESSES",
        process_is_initialized() ? "Online" : "Starting",
        process_is_initialized() ? "Manager initialized" : "Not initialized",
        process_is_initialized() ? UI_MINT : UI_AMBER, 2);
}
static void ui_quick_access(int x, int y, int width)
{
    int gap = 10;
    int tile_w = (width - gap) / 2;
    ui_label(x, y, "QUICK ACCESS");
    ui_app_tile(x, y + 18, tile_w, 62,
        "File Explorer", "Browse system paths", 1, UI_MINT);
    ui_app_tile(x + tile_w + gap, y + 18, tile_w, 62,
        "Terminal", "Run shell commands", 0, UI_BLUE_BRIGHT);
    ui_app_tile(x, y + 88, tile_w, 62,
        "System", "Inspect kernel services", 2, UI_BLUE);
    ui_app_tile(x + tile_w + gap, y + 88, tile_w, 62,
        "Settings", "Display configuration", 3, UI_AMBER);
}
static void ui_system_summary(int x, int y, int width, int height)
{
    ui_surface(x, y, width, height, 10, UI_SURFACE_0, UI_EDGE_SOFT);
    graphics_draw_text(x + 16, y + 15, "SYSTEM STATUS", UI_WHITE, 1);
    ui_label(x + width - 110, y + 15, "LIVE STATE");
    ui_detail_row(x + 16, y + 42, width - 32, "Virtual filesystem",
        filesystem_is_initialized() ? "Initialized" : "Unavailable",
        filesystem_is_initialized() ? UI_MINT : UI_AMBER);
    ui_detail_row(x + 16, y + 68, width - 32, "Process manager",
        process_is_initialized() ? "Initialized" : "Unavailable",
        process_is_initialized() ? UI_MINT : UI_AMBER);
    ui_detail_row(x + 16, y + 94, width - 32, "Execution target",
        "x86_64", UI_BLUE);
    ui_detail_row(x + 16, y + 120, width - 32, "Session", "Local", UI_MINT);
}
static void ui_footer(int x, int y, int width)
{
    ui_line(x, y, width, UI_EDGE_SOFT);
    graphics_draw_text(x, y + 12, "VANTAOS", UI_QUIET, 1);
    graphics_draw_text(x + 66, y + 12, "EXPERIMENTAL KERNEL WORKSPACE",
                       UI_QUIET, 1);
    graphics_draw_text(x + width - 115, y + 12, "NO TELEMETRY", UI_QUIET, 1);
}
void graphics_draw_desktop_backdrop(int width, int height)
{
    if (width <= 0 || height <= 0) return;
    ui_wallpaper(width, height);
    ui_topbar(width);
    ui_shortcuts();
}

void graphics_draw_desktop_dashboard(int width, int height)
{
    if (width < 600 || height < 460) return;
    int x = 330;
    int available = width - x - 24;
    int top = 62;
    int footer_y = height - 94;
    if (available < 260) return;
    ui_welcome(x, top, available);
    ui_overview(x, top + 140, available);
    int quick_y = top + 264;
    int quick_h = 168;
    if (quick_y + quick_h > footer_y - 8)
        quick_h = footer_y - quick_y - 8;
    if (quick_h >= 140) ui_quick_access(x, quick_y, available);
    int summary_y = quick_y + 164;
    int summary_h = footer_y - summary_y - 8;
    if (summary_h >= 72)
        ui_system_summary(x, summary_y, available, summary_h);
    ui_footer(24, height - 48, width - 48);
}
static void ui_window_heading(int x, int y, int width, const char* eyebrow,
                              const char* title, const char* description)
{
    ui_label(x, y, eyebrow);
    graphics_draw_text(x, y + 19, title, UI_WHITE, 1);
    graphics_draw_text(x, y + 39, description, UI_MUTED, 1);
    ui_line(x, y + 60, width, UI_EDGE_SOFT);
}
void graphics_draw_system_page_detail(int x, int y, int w, int h)
{
    if (w < 500 || h < 350) return;
    int inner_w = w - 48;
    ui_window_heading(x + 24, y + 66, inner_w, "DEVICE INFORMATION",
        "System overview", "Kernel and service readiness from live subsystem state.");
    int gap = 12;
    int col_w = (inner_w - gap) / 2;
    ui_metric(x + 24, y + 140, col_w, 94, "ARCHITECTURE",
        "x86_64", "64-bit execution target", UI_BLUE, 3);
    ui_metric(x + 24 + col_w + gap, y + 140, col_w, 94, "DISPLAY",
        "Framebuffer", "Software-rendered UI", UI_MINT, 0);
    ui_surface(x + 24, y + 248, inner_w, h - 276, 10,
        UI_SURFACE_0, UI_EDGE_SOFT);
    graphics_draw_text(x + 40, y + 264, "SERVICE HEALTH", UI_WHITE, 1);
    ui_detail_row(x + 40, y + 290, inner_w - 32, "Virtual filesystem",
        filesystem_is_initialized() ? "Initialized" : "Unavailable",
        filesystem_is_initialized() ? UI_MINT : UI_AMBER);
    ui_detail_row(x + 40, y + 316, inner_w - 32, "Process manager",
        process_is_initialized() ? "Initialized" : "Unavailable",
        process_is_initialized() ? UI_MINT : UI_AMBER);
    ui_detail_row(x + 40, y + 342, inner_w - 32, "Architecture",
        "x86_64", UI_BLUE);
}
void graphics_draw_settings_page_detail(int x, int y, int w, int h)
{
    if (w < 500 || h < 350) return;
    int inner_w = w - 48;
    ui_window_heading(x + 24, y + 66, inner_w, "PERSONALIZATION",
        "Display settings", "Changes apply only when the graphics backend supports them.");
    ui_surface(x + 24, y + 128, inner_w, 104, 10, UI_SURFACE_0, UI_EDGE_SOFT);
    graphics_draw_text(x + 42, y + 137, "Resolution", UI_WHITE, 1);
    graphics_draw_text(x + 42, y + 151, "Select a framebuffer mode", UI_MUTED, 1);
    const char* resolutions[3] = {"800 x 600", "1024 x 768", "1280 x 720"};
    int gap = 10;
    int bw = (w - 56 - gap * 2) / 3;
    for (int i = 0; i < 3; ++i)
    {
        int bx = x + 28 + i * (bw + gap);
        int by = y + 156;
        int selected = settings_resolution_index == i;
        ui_surface(bx, by, bw, 50, 8, selected ? UI_BLUE_DARK : UI_SURFACE_1,
                   selected ? UI_BLUE : UI_EDGE_SOFT);
        graphics_draw_text(bx + 10, by + 21, resolutions[i],
                           selected ? UI_WHITE : UI_MUTED, 1);
    }
    ui_surface(x + 24, y + 266, inner_w, h - 292, 10,
        UI_SURFACE_0, UI_EDGE_SOFT);
    graphics_draw_text(x + 42, y + 284, "Appearance", UI_WHITE, 1);
    graphics_draw_text(x + 42, y + 307, "Dark workspace", UI_MUTED, 1);
    ui_surface(x + inner_w - 70, y + 282, 26, 16, 8, UI_BLUE_DARK, UI_BLUE);
    graphics_fill_rounded_rect(x + inner_w - 57, y + 284, 12, 12, 6, UI_BLUE);
    ui_line(x + 42, y + 334, inner_w - 36, UI_EDGE_SOFT);
    graphics_draw_text(x + 42, y + 350,
        "Native framebuffer rendering; no GPU compositing yet.", UI_MUTED, 1);
    graphics_draw_text(x + 42, y + 369,
        "Visual effects remain intentionally honest about current limits.", UI_QUIET, 1);
}

/* Reusable primitives for follow-up native pages. */
void graphics_ui_draw_status_pill(int x, int y, const char* label, int ready)
{
    ui_surface(x, y, 118, 24, 8, UI_SURFACE_1, UI_EDGE_SOFT);
    ui_dot(x + 9, y + 9, ready ? UI_MINT : UI_AMBER);
    graphics_draw_text(x + 22, y + 8, label, UI_MUTED, 1);
}
void graphics_ui_draw_empty_state(int x, int y, int w, int h,
                                  const char* title, const char* message)
{
    int ix = x + w / 2 - 20;
    int iy = y + h / 2 - 48;
    ui_surface(ix, iy, 40, 40, 8, UI_SURFACE_1, UI_EDGE_SOFT);
    graphics_draw_text(ix + 11, iy + 15, "...", UI_QUIET, 1);
    graphics_draw_text(x + w / 2 - 70, iy + 54, title, UI_WHITE, 1);
    graphics_draw_text(x + w / 2 - 130, iy + 74, message, UI_MUTED, 1);
}
void graphics_ui_draw_divider(int x, int y, int width)
{
    ui_line(x, y, width, UI_EDGE_SOFT);
}
void graphics_ui_draw_badge(int x, int y, const char* text, uint32_t tint)
{
    int width = 16;
    for (const char* p = text; *p; ++p) width += 6;
    width = ui_clamp(width, 34, 180);
    ui_surface(x, y, width, 22, 7, UI_SURFACE_1, UI_EDGE_SOFT);
    ui_dot(x + 7, y + 8, tint);
    graphics_draw_text(x + 18, y + 7, text, UI_MUTED, 1);
}
void graphics_ui_draw_page_header(int x, int y, const char* section,
                                  const char* title, const char* subtitle)
{
    ui_label(x, y, section);
    graphics_draw_text(x, y + 19, title, UI_WHITE, 1);
    graphics_draw_text(x, y + 40, subtitle, UI_MUTED, 1);
}
void graphics_ui_draw_property(int x, int y, int width,
                               const char* key, const char* value)
{
    ui_line(x, y + 24, width, UI_EDGE_SOFT);
    graphics_draw_text(x, y + 7, key, UI_MUTED, 1);
    graphics_draw_text(x + width - 170, y + 7, value, UI_WHITE, 1);
}
void graphics_ui_draw_card(int x, int y, int width, int height,
                           const char* title, const char* subtitle,
                           uint32_t accent)
{
    ui_surface(x, y, width, height, 9, UI_SURFACE_0, UI_EDGE_SOFT);
    ui_accent(x + 14, y + 13, 24, accent);
    graphics_draw_text(x + 14, y + 28, title, UI_WHITE, 1);
    graphics_draw_text(x + 14, y + 47, subtitle, UI_MUTED, 1);
}
void graphics_ui_draw_icon_terminal(int x, int y, int size)
{
    ui_icon_terminal(x, y, size, UI_BLUE_BRIGHT);
}
void graphics_ui_draw_icon_folder(int x, int y, int size)
{
    ui_icon_folder(x, y, size, UI_MINT);
}
void graphics_ui_draw_icon_system(int x, int y, int size)
{
    ui_icon_system(x, y, size, UI_BLUE);
}
void graphics_ui_draw_icon_settings(int x, int y, int size)
{
    ui_icon_settings(x, y, size, UI_AMBER);
}


/* Launcher composition: rows keep the exact hit regions owned by the input
 * dispatcher. Search is deliberately omitted until it can actually filter. */
void graphics_draw_start_menu_detail(int width, int height)
{
    int mw = width < 492 ? width - 32 : 460;
    int mh = height < 532 ? height - 32 : 500;
    int mx = width / 2 - mw / 2;
    int my = height - mh - 8;
    if (mw < 280 || mh < 360) return;

    graphics_fill_rounded_rect(mx + 8, my + 10, mw, mh, 16, 0x0006090F);
    ui_surface(mx, my, mw, mh, 14, UI_SURFACE_0, UI_EDGE);
    graphics_fill_rounded_rect(mx + 1, my + 1, mw - 2, 54, 13,
                               UI_SURFACE_1);
    ui_line(mx + 1, my + 53, mw - 2, UI_EDGE_SOFT);

    ui_surface(mx + 18, my + 13, 30, 30, 8, UI_SURFACE_2, UI_EDGE_SOFT);
    ui_brand(mx + 26, my + 17, 22);
    graphics_draw_text(mx + 60, my + 18, "VantaOS", UI_WHITE, 1);
    graphics_draw_text(mx + 60, my + 34, "LOCAL WORKSPACE", UI_QUIET, 1);
    ui_surface(mx + mw - 42, my + 15, 24, 24, 12, UI_SURFACE_2, UI_EDGE_SOFT);
    graphics_draw_text(mx + mw - 35, my + 23, "OS", UI_MUTED, 1);

    ui_label(mx + 24, my + 76, "PINNED APPLICATIONS");
    ui_label(mx + mw - 100, my + 76, "4 APPS");

    ui_surface(mx + 24, my + 110, mw - 48, 54, 9,
               0x00202F3F, UI_EDGE_SOFT);
    ui_icon_terminal(mx + 36, my + 122, 30, UI_BLUE_BRIGHT);
    graphics_draw_text(mx + 78, my + 123, "Terminal", UI_WHITE, 1);
    graphics_draw_text(mx + 78, my + 141, "Run shell commands", UI_MUTED, 1);
    ui_dot(mx + mw - 42, my + 134, UI_MINT);

    ui_surface(mx + 24, my + 164, mw - 48, 54, 9,
               0x001A2633, UI_EDGE_SOFT);
    ui_icon_folder(mx + 36, my + 176, 30, UI_MINT);
    graphics_draw_text(mx + 78, my + 177, "Files", UI_WHITE, 1);
    graphics_draw_text(mx + 78, my + 195, "Browse system paths", UI_MUTED, 1);

    ui_surface(mx + 24, my + 218, mw - 48, 54, 9,
               0x001A2633, UI_EDGE_SOFT);
    ui_icon_system(mx + 36, my + 230, 30, UI_BLUE);
    graphics_draw_text(mx + 78, my + 231, "System", UI_WHITE, 1);
    graphics_draw_text(mx + 78, my + 249, "Kernel and service status", UI_MUTED, 1);

    ui_surface(mx + 24, my + 272, mw - 48, 54, 9,
               0x001A2633, UI_EDGE_SOFT);
    ui_icon_settings(mx + 36, my + 284, 30, UI_AMBER);
    graphics_draw_text(mx + 78, my + 285, "Settings", UI_WHITE, 1);
    graphics_draw_text(mx + 78, my + 303, "Display configuration", UI_MUTED, 1);

    ui_line(mx + 24, my + mh - 64, mw - 48, UI_EDGE_SOFT);
    ui_dot(mx + 26, my + mh - 43,
        filesystem_is_initialized() ? UI_MINT : UI_AMBER);
    graphics_draw_text(mx + 39, my + mh - 44,
        filesystem_is_initialized() ? "Filesystem ready" : "Filesystem unavailable",
        UI_MUTED, 1);
    graphics_draw_text(mx + mw - 108, my + mh - 44, "VantaOS", UI_QUIET, 1);
}

/* Shared Explorer parts. These are presentation-only; the Explorer remains
 * the sole owner of directory state and all input hit-testing. */
void graphics_ui_explorer_sidebar_heading(int x, int y, const char* title)
{
    ui_label(x, y, title);
    ui_line(x, y + 15, 140, UI_EDGE_SOFT);
}
void graphics_ui_explorer_column_header(int x, int y, int width)
{
    graphics_fill_rect(x, y, width, 32, UI_SURFACE_1);
    ui_line(x, y, width, UI_EDGE_SOFT);
    graphics_draw_text(x + 18, y + 11, "NAME", UI_QUIET, 1);
    graphics_draw_text(x + width - 150, y + 11, "TYPE", UI_QUIET, 1);
    graphics_draw_text(x + width - 66, y + 11, "SIZE", UI_QUIET, 1);
    ui_line(x + 18, y + 30, width - 36, UI_EDGE_SOFT);
}
void graphics_ui_explorer_row(int x, int y, int width, int height,
                              int selected, int alternate)
{
    uint32_t fill = selected ? UI_BLUE_DARK :
                    alternate ? 0x0017222F : UI_SURFACE_0;
    graphics_fill_rect(x, y, width, height, fill);
    if (selected)
        graphics_fill_rect(x, y + 3, 2, height - 6, UI_BLUE_BRIGHT);
    ui_line(x + 18, y + height - 1, width - 36, UI_EDGE_SOFT);
}
void graphics_ui_explorer_status(int x, int y, int width,
                                 const char* count_label,
                                 const char* location_label)
{
    graphics_fill_rect(x, y, width, 26, UI_SURFACE_1);
    ui_line(x, y, width, UI_EDGE_SOFT);
    graphics_draw_text(x + 14, y + 9, count_label, UI_MUTED, 1);
    graphics_draw_text(x + width - 150, y + 9, location_label, UI_QUIET, 1);
}
void graphics_ui_explorer_location(int x, int y, int width,
                                   const char* location)
{
    ui_surface(x, y, width, 30, 7, UI_SURFACE_0, UI_EDGE_SOFT);
    ui_dot(x + 10, y + 12, UI_BLUE);
    graphics_draw_text(x + 23, y + 11, location, UI_WHITE, 1);
}
void graphics_ui_explorer_toolbar_button(int x, int y, const char* glyph,
                                         int enabled)
{
    ui_surface(x, y, 32, 30, 6,
        enabled ? UI_SURFACE_2 : UI_SURFACE_0, UI_EDGE_SOFT);
    graphics_draw_text(x + 10, y + 10, glyph,
        enabled ? UI_WHITE : UI_QUIET, 1);
}

/* Small structural components used by future kernel-rendered applications. */
void graphics_ui_draw_section(int x, int y, int width, const char* title,
                              const char* detail)
{
    graphics_draw_text(x, y, title, UI_WHITE, 1);
    graphics_draw_text(x, y + 18, detail, UI_MUTED, 1);
    ui_line(x, y + 36, width, UI_EDGE_SOFT);
}
void graphics_ui_draw_callout(int x, int y, int width, int height,
                              const char* title, const char* body,
                              uint32_t accent)
{
    ui_surface(x, y, width, height, 9, UI_SURFACE_0, UI_EDGE_SOFT);
    graphics_fill_rounded_rect(x, y + 10, 3, height - 20, 1, accent);
    graphics_draw_text(x + 16, y + 14, title, UI_WHITE, 1);
    graphics_draw_text(x + 16, y + 34, body, UI_MUTED, 1);
}
void graphics_ui_draw_key_value(int x, int y, int width, const char* key,
                                const char* value, uint32_t value_color)
{
    ui_line(x, y + 25, width, UI_EDGE_SOFT);
    graphics_draw_text(x, y + 8, key, UI_MUTED, 1);
    graphics_draw_text(x + width - 170, y + 8, value, value_color, 1);
}
void graphics_ui_draw_progress(int x, int y, int width, int progress,
                               uint32_t tint)
{
    ui_surface(x, y, width, 6, 3, UI_SURFACE_2, 0);
    int fill = ui_clamp(progress, 0, 100) * width / 100;
    if (fill > 0)
        graphics_fill_rounded_rect(x, y, fill, 6, 3, tint);
}
void graphics_ui_draw_app_icon(int x, int y, int size, int app_kind)
{
    if (app_kind == 0) ui_icon_terminal(x, y, size, UI_BLUE_BRIGHT);
    else if (app_kind == 1) ui_icon_folder(x, y, size, UI_MINT);
    else if (app_kind == 2) ui_icon_system(x, y, size, UI_BLUE);
    else if (app_kind == 3) ui_icon_settings(x, y, size, UI_AMBER);
    else ui_icon_chip(x, y, size, UI_MINT);
}
void graphics_ui_draw_window_section(int x, int y, int width,
                                     const char* title)
{
    graphics_draw_text(x, y, title, UI_WHITE, 1);
    ui_line(x, y + 22, width, UI_EDGE_SOFT);
}
void graphics_ui_draw_table_row(int x, int y, int width, int height,
                                const char* first, const char* second,
                                const char* third, int alternate)
{
    graphics_fill_rect(x, y, width, height,
        alternate ? 0x0017222F : UI_SURFACE_0);
    graphics_draw_text(x + 16, y + height / 2 - 3, first, UI_WHITE, 1);
    graphics_draw_text(x + width - 180, y + height / 2 - 3, second,
                       UI_MUTED, 1);
    graphics_draw_text(x + width - 70, y + height / 2 - 3, third,
                       UI_MUTED, 1);
    ui_line(x + 16, y + height - 1, width - 32, UI_EDGE_SOFT);
}
void graphics_ui_draw_navigation_item(int x, int y, int width,
                                      const char* label, int selected,
                                      int app_kind)
{
    if (selected)
        ui_surface(x, y, width, 34, 7, UI_SURFACE_2, UI_EDGE_SOFT);
    graphics_ui_draw_app_icon(x + 8, y + 6, 22, app_kind);
    graphics_draw_text(x + 38, y + 12, label,
                       selected ? UI_WHITE : UI_MUTED, 1);
}
void graphics_ui_draw_stat_card(int x, int y, int width, int height,
                                const char* label, const char* value,
                                const char* caption, uint32_t tint)
{
    ui_surface(x, y, width, height, 9, UI_SURFACE_0, UI_EDGE_SOFT);
    ui_dot(x + 14, y + 15, tint);
    graphics_draw_text(x + 27, y + 14, label, UI_QUIET, 1);
    graphics_draw_text(x + 14, y + 37, value, UI_WHITE, 1);
    graphics_draw_text(x + 14, y + height - 19, caption, UI_MUTED, 1);
}
void graphics_ui_draw_settings_row(int x, int y, int width,
                                   const char* title, const char* subtitle,
                                   const char* value)
{
    ui_line(x, y + 48, width, UI_EDGE_SOFT);
    graphics_draw_text(x, y + 7, title, UI_WHITE, 1);
    graphics_draw_text(x, y + 25, subtitle, UI_MUTED, 1);
    graphics_draw_text(x + width - 150, y + 16, value, UI_BLUE_BRIGHT, 1);
}
void graphics_ui_draw_separator_label(int x, int y, int width,
                                      const char* label)
{
    int label_width = 12;
    for (const char* p = label; *p; ++p) label_width += 6;
    ui_line(x, y + 5, width, UI_EDGE_SOFT);
    graphics_fill_rect(x, y, ui_min(label_width, width), 12, UI_SURFACE_0);
    graphics_draw_text(x + 6, y + 1, label, UI_QUIET, 1);
}
void graphics_ui_draw_notice(int x, int y, int width, const char* message,
                             uint32_t tint)
{
    ui_surface(x, y, width, 34, 7, UI_SURFACE_1, UI_EDGE_SOFT);
    graphics_fill_rounded_rect(x + 10, y + 10, 3, 14, 1, tint);
    graphics_draw_text(x + 22, y + 12, message, UI_MUTED, 1);
}


/* Native widget primitives. These draw visual states only; the owning app
 * must supply hit-testing and state transitions before presenting controls
 * as interactive. */

void graphics_ui_draw_button(int x, int y, int width, int height,
                             const char* label, int selected, int enabled)
{
    uint32_t fill = selected ? UI_BLUE_DARK :
                    enabled ? UI_SURFACE_1 : UI_INK_SOFT;
    uint32_t edge = selected ? UI_BLUE : UI_EDGE_SOFT;
    uint32_t ink = enabled ? UI_WHITE : UI_QUIET;
    ui_surface(x, y, width, height, 7, fill, edge);
    graphics_draw_text(x + 12, y + height / 2 - 3, label, ink, 1);
    if (selected)
        graphics_fill_rounded_rect(x + 8, y + height - 4,
                                   width - 16, 2, 1, UI_BLUE_BRIGHT);
}

void graphics_ui_draw_toggle(int x, int y, int enabled)
{
    uint32_t track = enabled ? UI_BLUE_DARK : UI_SURFACE_2;
    uint32_t edge = enabled ? UI_BLUE : UI_EDGE;
    ui_surface(x, y, 38, 22, 11, track, edge);
    graphics_fill_rounded_rect(x + (enabled ? 19 : 3), y + 3,
                               16, 16, 8,
                               enabled ? UI_BLUE_BRIGHT : UI_MUTED);
}

void graphics_ui_draw_tab(int x, int y, int width, const char* label,
                          int active)
{
    if (active)
    {
        ui_surface(x, y, width, 30, 6, UI_SURFACE_2, UI_EDGE_SOFT);
        graphics_fill_rounded_rect(x + 8, y + 26, width - 16, 2, 1,
                                   UI_BLUE_BRIGHT);
    }
    graphics_draw_text(x + 12, y + 11, label,
                       active ? UI_WHITE : UI_MUTED, 1);
}

void graphics_ui_draw_breadcrumb(int x, int y, int width,
                                 const char* path)
{
    ui_surface(x, y, width, 30, 7, UI_SURFACE_0, UI_EDGE_SOFT);
    ui_dot(x + 10, y + 12, UI_BLUE);
    graphics_draw_text(x + 23, y + 11, path, UI_WHITE, 1);
}

void graphics_ui_draw_toolbar(int x, int y, int width,
                              const char* section, const char* action)
{
    graphics_fill_rect(x, y, width, 42, UI_SURFACE_1);
    ui_line(x, y + 41, width, UI_EDGE_SOFT);
    graphics_draw_text(x + 16, y + 17, section, UI_WHITE, 1);
    int action_width = 18;
    for (const char* p = action; *p; ++p) action_width += 6;
    action_width = ui_clamp(action_width, 64, 180);
    ui_surface(x + width - action_width - 12, y + 6,
               action_width, 30, 7, UI_SURFACE_2, UI_EDGE_SOFT);
    graphics_draw_text(x + width - action_width, y + 17, action,
                       UI_MUTED, 1);
}

void graphics_ui_draw_icon_badge(int x, int y, int size,
                                 const char* glyph, uint32_t tint)
{
    ui_surface(x, y, size, size, 7, UI_SURFACE_2, UI_EDGE_SOFT);
    graphics_draw_text(x + size / 2 - 3, y + size / 2 - 3,
                       glyph, tint, 1);
}

void graphics_ui_draw_status_line(int x, int y, int width,
                                   const char* title, const char* state,
                                   int ready)
{
    ui_line(x, y + 27, width, UI_EDGE_SOFT);
    ui_dot(x + 2, y + 10, ready ? UI_MINT : UI_AMBER);
    graphics_draw_text(x + 16, y + 9, title, UI_MUTED, 1);
    graphics_draw_text(x + width - 150, y + 9, state,
                       ready ? UI_WHITE : UI_AMBER, 1);
}

void graphics_ui_draw_info_card(int x, int y, int width, int height,
                                const char* title, const char* value,
                                const char* description, uint32_t tint)
{
    ui_surface(x, y, width, height, 10, UI_SURFACE_0, UI_EDGE_SOFT);
    ui_surface(x + 14, y + 14, 26, 26, 7, UI_SURFACE_2, UI_EDGE_SOFT);
    ui_dot(x + 24, y + 24, tint);
    graphics_draw_text(x + 50, y + 16, title, UI_MUTED, 1);
    graphics_draw_text(x + 14, y + 49, value, UI_WHITE, 1);
    graphics_draw_text(x + 14, y + height - 20, description, UI_QUIET, 1);
}

void graphics_ui_draw_description_row(int x, int y, int width,
                                      const char* title, const char* body)
{
    ui_line(x, y + 42, width, UI_EDGE_SOFT);
    graphics_draw_text(x, y + 6, title, UI_WHITE, 1);
    graphics_draw_text(x, y + 23, body, UI_MUTED, 1);
}

void graphics_ui_draw_property_group(int x, int y, int width,
                                     const char* heading,
                                     const char* description)
{
    ui_label(x, y, heading);
    graphics_draw_text(x, y + 17, description, UI_MUTED, 1);
    ui_line(x, y + 36, width, UI_EDGE_SOFT);
}

void graphics_ui_draw_section_card(int x, int y, int width, int height,
                                   const char* heading, uint32_t accent)
{
    ui_surface(x, y, width, height, 10, UI_SURFACE_0, UI_EDGE_SOFT);
    graphics_fill_rounded_rect(x + 14, y + 14, 3, height - 28, 1, accent);
    graphics_draw_text(x + 28, y + 15, heading, UI_WHITE, 1);
}

void graphics_ui_draw_keycap(int x, int y, const char* key)
{
    int width = 14;
    for (const char* p = key; *p; ++p) width += 6;
    width = ui_clamp(width, 24, 72);
    ui_surface(x, y, width, 22, 5, UI_SURFACE_2, UI_EDGE);
    graphics_draw_text(x + 7, y + 7, key, UI_WHITE, 1);
}

void graphics_ui_draw_shortcut_hint(int x, int y, const char* description,
                                    const char* key)
{
    graphics_draw_text(x, y + 6, description, UI_MUTED, 1);
    graphics_ui_draw_keycap(x + 210, y, key);
}

void graphics_ui_draw_progress_label(int x, int y, int width,
                                     const char* label, int percent,
                                     uint32_t tint)
{
    char text[5];
    int value = ui_clamp(percent, 0, 100);
    text[0] = (char)('0' + (value / 100) % 10);
    text[1] = (char)('0' + (value / 10) % 10);
    text[2] = (char)('0' + value % 10);
    text[3] = '%';
    text[4] = 0;
    graphics_draw_text(x, y, label, UI_MUTED, 1);
    graphics_draw_text(x + width - 30, y, text, UI_WHITE, 1);
    graphics_ui_draw_progress(x, y + 17, width, value, tint);
}

void graphics_ui_draw_divided_panel(int x, int y, int width, int height,
                                    int divider_x)
{
    ui_surface(x, y, width, height, 10, UI_SURFACE_0, UI_EDGE_SOFT);
    if (divider_x > 0 && divider_x < width)
        ui_vline(x + divider_x, y + 12, height - 24, UI_EDGE_SOFT);
}

void graphics_ui_draw_illustration_frame(int x, int y, int width, int height)
{
    ui_surface(x, y, width, height, 12, UI_SURFACE_1, UI_EDGE_SOFT);
    ui_surface(x + 12, y + 12, width - 24, height - 24,
               8, UI_SURFACE_0, UI_EDGE_SOFT);
    ui_line(x + 24, y + 30, width - 48, UI_EDGE_SOFT);
    ui_dot(x + 23, y + 22, UI_RED);
    ui_dot(x + 34, y + 22, UI_AMBER);
    ui_dot(x + 45, y + 22, UI_MINT);
}

void graphics_ui_draw_file_type_chip(int x, int y, const char* extension,
                                     uint32_t tint)
{
    int width = 18;
    for (const char* p = extension; *p; ++p) width += 6;
    width = ui_clamp(width, 34, 68);
    ui_surface(x, y, width, 20, 5, UI_SURFACE_2, UI_EDGE_SOFT);
    graphics_draw_text(x + 7, y + 6, extension, tint, 1);
}

void graphics_ui_draw_list_empty(int x, int y, int width, int height,
                                 const char* title, const char* detail,
                                 const char* hint)
{
    int center_x = x + width / 2;
    int center_y = y + height / 2;
    ui_surface(center_x - 24, center_y - 60, 48, 48, 12,
               UI_SURFACE_1, UI_EDGE_SOFT);
    graphics_draw_text(center_x - 8, center_y - 43, "...", UI_QUIET, 1);
    graphics_draw_text(center_x - 64, center_y - 3, title, UI_WHITE, 1);
    graphics_draw_text(center_x - 140, center_y + 18, detail, UI_MUTED, 1);
    graphics_draw_text(center_x - 130, center_y + 43, hint, UI_QUIET, 1);
}

void graphics_ui_draw_call_to_action(int x, int y, int width,
                                     const char* label, uint32_t tint)
{
    ui_surface(x, y, width, 34, 7, UI_SURFACE_2, tint);
    graphics_draw_text(x + 12, y + 12, label, UI_WHITE, 1);
    graphics_draw_text(x + width - 18, y + 12, ">", tint, 1);
}

void graphics_ui_draw_status_footer(int x, int y, int width,
                                    const char* left, const char* right)
{
    graphics_fill_rect(x, y, width, 26, UI_SURFACE_1);
    ui_line(x, y, width, UI_EDGE_SOFT);
    graphics_draw_text(x + 14, y + 9, left, UI_MUTED, 1);
    graphics_draw_text(x + width - 170, y + 9, right, UI_QUIET, 1);
}

void graphics_ui_draw_split_heading(int x, int y, int width,
                                    const char* title, const char* action)
{
    graphics_draw_text(x, y, title, UI_WHITE, 1);
    graphics_draw_text(x + width - 100, y, action, UI_BLUE_BRIGHT, 1);
    ui_line(x, y + 22, width, UI_EDGE_SOFT);
}

void graphics_ui_draw_notice_card(int x, int y, int width, int height,
                                  const char* eyebrow, const char* title,
                                  const char* body, uint32_t tint)
{
    ui_surface(x, y, width, height, 10, UI_SURFACE_0, UI_EDGE_SOFT);
    ui_accent(x + 14, y + 14, 28, tint);
    graphics_draw_text(x + 14, y + 27, eyebrow, UI_QUIET, 1);
    graphics_draw_text(x + 14, y + 45, title, UI_WHITE, 1);
    graphics_draw_text(x + 14, y + 64, body, UI_MUTED, 1);
}

void graphics_ui_draw_table_header(int x, int y, int width,
                                   const char* first, const char* second,
                                   const char* third)
{
    graphics_fill_rect(x, y, width, 30, UI_SURFACE_1);
    graphics_draw_text(x + 16, y + 10, first, UI_QUIET, 1);
    graphics_draw_text(x + width - 180, y + 10, second, UI_QUIET, 1);
    graphics_draw_text(x + width - 70, y + 10, third, UI_QUIET, 1);
    ui_line(x, y + 29, width, UI_EDGE_SOFT);
}

void graphics_ui_draw_inline_status(int x, int y, const char* label,
                                    int ready)
{
    ui_dot(x, y + 3, ready ? UI_MINT : UI_AMBER);
    graphics_draw_text(x + 11, y, label, UI_MUTED, 1);
}
