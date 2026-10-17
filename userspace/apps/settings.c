#include <vanta/gui.h>

#define SETTINGS_PATH "/SYSTEM/VANTASET.CFG"
#define CONFIG_CAPACITY 256U
#define WINDOW_WIDTH 620U
#define WINDOW_HEIGHT 470U
#define SETTING_COUNT 5U

#define COLOR_TEXT 0x00F1F5F9U
#define COLOR_MUTED 0x0094A3B8U
#define COLOR_SURFACE 0x00212D3BU
#define COLOR_SURFACE_HI 0x00313F50U
#define COLOR_BLUE 0x003B82F6U
#define COLOR_TEAL 0x0014B8A6U
#define COLOR_AMBER 0x00D97706U
#define COLOR_VIOLET 0x008B5CF6U
#define COLOR_SUCCESS 0x0022C55EU
#define COLOR_ERROR 0x00F87171U
#define COLOR_LIGHT_TEXT 0x00182735U
#define COLOR_LIGHT_MUTED 0x00556678U
#define COLOR_LIGHT_SURFACE 0x00E8EEF5U
#define COLOR_LIGHT_SELECTED 0x00D8E4F2U
#define COLOR_LIGHT_BACKGROUND 0x00F7F9FCU

static vanta_gui_draw_command_t commands[VANTA_GUI_MAX_COMMANDS];
static uint32_t command_count;
static uint32_t window_id;
static uint32_t accent;
static int light_theme;
static int compact_rows;
static int text_scale;
static int high_contrast;
static uint32_t selected_setting;
static uint32_t selected_page;
static int running = 1;
static char status_buffer[64] = "Defaults loaded; save to keep changes";
static uint32_t status_color;

static size_t text_length(const char* text)
{
    size_t length = 0U;
    while (text && text[length])
        length++;
    return length;
}

static int text_equals(const char* left, const char* right)
{
    if (!left || !right)
        return 0;
    while (*left && *right && *left == *right)
    {
        left++;
        right++;
    }
    return *left == *right;
}

static int text_contains(const char* text, const char* needle)
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

static void set_status(const char* status, uint32_t color)
{
    size_t length = text_length(status);
    if (length >= sizeof(status_buffer))
        length = sizeof(status_buffer) - 1U;
    for (size_t i = 0U; i < length; i++)
        status_buffer[i] = status[i];
    status_buffer[length] = '\0';
    status_color = color;
}

static uint32_t color_text(void)
{
    if (high_contrast)
        return light_theme ? 0x00000000U : 0x00FFFFFFU;
    return light_theme ? COLOR_LIGHT_TEXT : COLOR_TEXT;
}

static uint32_t color_muted(void)
{
    if (high_contrast)
        return color_text();
    return light_theme ? COLOR_LIGHT_MUTED : COLOR_MUTED;
}

static uint32_t color_surface(void)
{
    return light_theme ? COLOR_LIGHT_SURFACE : COLOR_SURFACE;
}

static uint32_t color_selected(void)
{
    return light_theme ? COLOR_LIGHT_SELECTED : COLOR_SURFACE_HI;
}

static uint32_t accent_color(void)
{
    if (accent == 1U) return COLOR_TEAL;
    if (accent == 2U) return COLOR_AMBER;
    if (accent == 3U) return COLOR_VIOLET;
    return COLOR_BLUE;
}

static const char* accent_name(void)
{
    if (accent == 1U) return "Teal";
    if (accent == 2U) return "Amber";
    if (accent == 3U) return "Violet";
    return "Blue";
}

static void add_rect(int32_t x, int32_t y, int32_t width,
                     int32_t height, uint32_t color)
{
    if (command_count >= VANTA_GUI_MAX_COMMANDS)
        return;
    vanta_gui_draw_command_t* command = &commands[command_count++];
    *command = (vanta_gui_draw_command_t){
        .type = VANTA_GUI_DRAW_FILL_RECT,
        .x = x, .y = y, .width = width, .height = height,
        .color = color, .scale = 1U
    };
}

static void add_text_scaled(int32_t x, int32_t y, const char* text,
                            uint32_t color, uint32_t scale)
{
    size_t length = text_length(text);
    if (command_count >= VANTA_GUI_MAX_COMMANDS || length == 0U ||
        length > VANTA_GUI_TEXT_MAX)
        return;
    vanta_gui_draw_command_t* command = &commands[command_count++];
    *command = (vanta_gui_draw_command_t){
        .type = VANTA_GUI_DRAW_TEXT,
        .x = x, .y = y, .color = color,
        .scale = scale
    };
    for (size_t i = 0U; i <= length; i++)
        command->text[i] = text[i];
}

static void add_text(int32_t x, int32_t y, const char* text, uint32_t color)
{
    add_text_scaled(x, y, text, color, 1U);
}

static void reset_defaults(void)
{
    accent = 0U;
    light_theme = 0;
    compact_rows = 0;
    text_scale = 1;
    high_contrast = 0;
}

static int load_config(void)
{
    char buffer[CONFIG_CAPACITY];
    long descriptor = vanta_open(SETTINGS_PATH, VANTA_OPEN_READ);
    if (descriptor < 0)
        return 0;

    long count = vanta_read((int)descriptor, buffer, sizeof(buffer) - 1U);
    (void)vanta_close((int)descriptor);
    if (count < 0 || (size_t)count >= sizeof(buffer))
        return 0;
    buffer[(size_t)count] = '\0';

    /* Accept v1 files so existing user preferences are not discarded. */
    if (!text_contains(buffer, "VANTA_SETTINGS=1\n") &&
        !text_contains(buffer, "VANTA_SETTINGS=2\n"))
        return 0;

    if (text_contains(buffer, "accent=blue\n")) accent = 0U;
    else if (text_contains(buffer, "accent=teal\n")) accent = 1U;
    else if (text_contains(buffer, "accent=amber\n")) accent = 2U;
    else if (text_contains(buffer, "accent=violet\n")) accent = 3U;
    else return 0;

    if (text_contains(buffer, "theme=light\n")) light_theme = 1;
    else if (text_contains(buffer, "theme=dark\n")) light_theme = 0;
    else light_theme = 0;

    if (text_contains(buffer, "compact=1\n")) compact_rows = 1;
    else if (text_contains(buffer, "compact=0\n")) compact_rows = 0;
    else return 0;

    if (text_contains(buffer, "text_scale=2\n")) text_scale = 2;
    else if (text_contains(buffer, "text_scale=1\n")) text_scale = 1;
    else text_scale = 1;

    if (text_contains(buffer, "contrast=1\n")) high_contrast = 1;
    else if (text_contains(buffer, "contrast=0\n")) high_contrast = 0;
    else high_contrast = 0;
    return 1;
}

static long open_config_for_write(void)
{
    long descriptor = vanta_open(
        SETTINGS_PATH, VANTA_OPEN_WRITE | VANTA_OPEN_TRUNCATE);
    if (descriptor >= 0)
        return descriptor;
    if (vanta_create_file(SETTINGS_PATH) < 0)
        return -1;
    return vanta_open(
        SETTINGS_PATH, VANTA_OPEN_WRITE | VANTA_OPEN_TRUNCATE);
}

static int save_config(void)
{
    char buffer[160];
    const char* accent_value = accent == 1U ? "teal" :
        accent == 2U ? "amber" : accent == 3U ? "violet" : "blue";
    const char* theme_value = light_theme ? "light" : "dark";
    size_t used = 0U;
    const char* parts[] = {
        "VANTA_SETTINGS=2\naccent=", accent_value,
        "\ntheme=", theme_value,
        "\ncompact=", compact_rows ? "1" : "0",
        "\ntext_scale=", text_scale == 2 ? "2" : "1",
        "\ncontrast=", high_contrast ? "1\n" : "0\n"
    };
    for (size_t part = 0U; part < sizeof(parts) / sizeof(parts[0]); part++)
    {
        size_t length = text_length(parts[part]);
        if (used + length >= sizeof(buffer))
            return 0;
        for (size_t i = 0U; i < length; i++)
            buffer[used++] = parts[part][i];
    }

    long descriptor = open_config_for_write();
    if (descriptor < 0)
        return 0;
    long written = vanta_file_write((int)descriptor, buffer, used);
    long closed = vanta_close((int)descriptor);
    if (written != (long)used || closed != 0)
        return 0;
    return vanta_settings_apply() == 0;
}

static int run_storage_self_test(void)
{
    static const char expected[] =
        "VANTA_SETTINGS=2\naccent=teal\ntheme=dark\ncompact=1\n"
        "text_scale=1\ncontrast=0\n";
    char actual[sizeof(expected)];
    int previously_persisted = 0;
    long existing = vanta_open(SETTINGS_PATH, VANTA_OPEN_READ);
    if (existing >= 0)
    {
        long existing_count = vanta_read(
            (int)existing, actual, sizeof(actual) - 1U);
        (void)vanta_close((int)existing);
        if (existing_count == (long)(sizeof(expected) - 1U))
        {
            actual[(size_t)existing_count] = '\0';
            previously_persisted = text_equals(actual, expected);
        }
    }

    long descriptor = open_config_for_write();
    if (descriptor < 0)
        return 2;
    long written = vanta_file_write(
        (int)descriptor, expected, sizeof(expected) - 1U);
    long closed = vanta_close((int)descriptor);
    if (written != (long)(sizeof(expected) - 1U) || closed != 0)
        return 3;
    if (vanta_settings_apply() != 0)
        return 7;

    descriptor = vanta_open(SETTINGS_PATH, VANTA_OPEN_READ);
    if (descriptor < 0)
        return 4;
    long read_count = vanta_read((int)descriptor, actual, sizeof(actual));
    closed = vanta_close((int)descriptor);
    if (read_count != (long)(sizeof(expected) - 1U) || closed != 0)
        return 5;
    actual[(size_t)read_count] = '\0';
    if (!text_equals(actual, expected))
        return 6;

    static const char marker[] = "VANTAOS_SETTINGS_STORAGE_OK\n";
    (void)vanta_write(marker, sizeof(marker) - 1U);
    if (previously_persisted)
    {
        static const char persist_marker[] = "VANTAOS_SETTINGS_PERSIST_OK\n";
        (void)vanta_write(persist_marker, sizeof(persist_marker) - 1U);
    }
    return 0;
}

static void render(void)
{
    command_count = 0U;
    uint32_t text = color_text();
    uint32_t muted = color_muted();
    uint32_t surface = color_surface();
    uint32_t selected = color_selected();
    uint32_t accent_value = accent_color();

    add_text(16, 10, "Settings", text);
    add_text(16, 30, "Personalize how VantaOS looks and feels.", muted);

    /* Persistent navigation keeps the three settings areas easy to scan. */
    add_rect(12, 56, 148, 294, surface);
    add_text(24, 66, "CATEGORIES", muted);
    const int nav_y[3] = { 88, 132, 176 };
    const char* nav_labels[3] = { "Appearance", "Accessibility", "About" };
    for (uint32_t i = 0U; i < 3U; i++)
    {
        if (selected_page == i)
            add_rect(18, nav_y[i], 136, 36, selected);
        if (selected_page == i)
            add_rect(18, nav_y[i], 3, 36, accent_value);
        add_text(28, nav_y[i] + 10, nav_labels[i],
                 selected_page == i ? text : muted);
    }

    add_rect(172, 56, 436, 294, surface);
    if (selected_page == 0U)
    {
        add_text(190, 68, "Appearance", text);
        add_text(190, 88, "Set your desktop's visual defaults.", muted);
        const int row_y[3] = { 116, 172, 228 };
        const char* labels[3] = { "Accent color", "Color theme", "Explorer row density" };
        const char* values[3] = {
            accent_name(), light_theme ? "Light" : "Dark",
            compact_rows ? "Compact" : "Comfortable"
        };
        for (uint32_t i = 0U; i < 3U; i++)
        {
            uint32_t setting = i;
            add_rect(184, row_y[i], 412, 44,
                     selected_setting == setting ? selected : color_surface());
            add_text(196, row_y[i] + 7, labels[i], text);
            add_text(196, row_y[i] + 25, "Select to cycle option", muted);
            add_text(464, row_y[i] + 15, values[i],
                     setting == 0U ? accent_value : text);
        }
    }
    else if (selected_page == 1U)
    {
        add_text(190, 68, "Accessibility", text);
        add_text(190, 88, "Improve legibility in Settings.", muted);
        const int row_y[2] = { 116, 172 };
        const char* labels[2] = { "Settings text size", "High contrast" };
        const char* values[2] = {
            text_scale == 2 ? "Large" : "Normal",
            high_contrast ? "On" : "Off"
        };
        for (uint32_t i = 0U; i < 2U; i++)
        {
            uint32_t setting = i + 3U;
            add_rect(184, row_y[i], 412, 44,
                     selected_setting == setting ? selected : color_surface());
            add_text(196, row_y[i] + 7, labels[i], text);
            add_text(196, row_y[i] + 25, "Select to toggle option", muted);
            add_text(500, row_y[i] + 15, values[i], text);
        }
        add_text(190, 236, "Text size currently affects Settings only.", muted);
    }
    else
    {
        add_text(190, 68, "About Settings", text);
        add_text(190, 98, "VantaOS Settings", accent_value);
        add_text(190, 124, "Standalone ring-3 application", text);
        add_text(190, 150, "Preferences are stored in:", muted);
        add_text(190, 172, "/SYSTEM/VANTASET.CFG", text);
        add_text(190, 206, "Theme changes are applied when saved.", muted);
        add_text(190, 230, "System time and timezone are not configurable yet.", muted);
        add_text(190, 254, "This build does not claim unsupported features.", muted);
    }

    add_rect(172, 365, 202, 38, accent_value);
    add_text(190, 377, "Save changes", 0x00FFFFFFU);
    add_rect(384, 365, 224, 38, color_selected());
    add_text(402, 377, "Restore defaults", text);
    add_text(16, 414, status_buffer, status_color);
    add_text(16, 436, "Click a category or setting; S saves, R resets, Q closes.", muted);
    (void)vanta_gui_draw(window_id, commands, command_count);
}

static void close_app(void)
{
    if (window_id != 0U)
        (void)vanta_gui_close_window(window_id);
    vanta_exit(0);
}

static void cycle_setting(uint32_t setting)
{
    if (setting >= SETTING_COUNT)
        return;
    selected_setting = setting;
    selected_page = setting < 3U ? 0U : 1U;
    if (setting == 0U)
        accent = (accent + 1U) % 4U;
    else if (setting == 1U)
        light_theme = !light_theme;
    else if (setting == 2U)
        compact_rows = !compact_rows;
    else if (setting == 3U)
        text_scale = text_scale == 1 ? 2 : 1;
    else if (setting == 4U)
        high_contrast = !high_contrast;
    set_status("Unsaved changes", color_muted());
}

static void save_and_report(void)
{
    if (save_config())
        set_status("Saved /SYSTEM/VANTASET.CFG", COLOR_SUCCESS);
    else
        set_status("Save failed: check disk space and filesystem", COLOR_ERROR);
}

int main(int argc, char** argv)
{
    reset_defaults();
    status_color = COLOR_MUTED;
    if (argc == 2 && text_equals(argv[1], "--self-test"))
        vanta_exit(run_storage_self_test());
    if (argc != 1)
        vanta_exit(2);

    if (load_config())
        set_status("Loaded saved preferences", COLOR_SUCCESS);

    vanta_gui_window_request_t request = {
        .width = WINDOW_WIDTH, .height = WINDOW_HEIGHT, .title = "Settings"
    };
    long created = vanta_gui_create_window(&request);
    if (created <= 0)
        vanta_exit(3);
    window_id = (uint32_t)created;
    render();

    while (running)
    {
        vanta_gui_event_t event;
        long result = vanta_gui_poll_event(window_id, &event);
        if (result < 0)
            close_app();
        if (result == 0)
        {
            (void)vanta_yield();
            continue;
        }
        if (event.type == VANTA_GUI_EVENT_CLOSE_REQUEST)
            close_app();
        if (event.type == VANTA_GUI_EVENT_THEME_CHANGED)
        {
            render();
            continue;
        }

        if (event.type == VANTA_GUI_EVENT_KEY)
        {
            uint32_t key = event.value;
            if (key == 'q' || key == 'Q')
                close_app();
            else if (key >= '1' && key <= '5')
                cycle_setting(key - '1');
            else if (key == 'a' || key == 'A')
                selected_page = 0U;
            else if (key == 'c' || key == 'C')
                selected_page = 1U;
            else if (key == 'i' || key == 'I')
                selected_page = 2U;
            else if (key == 's' || key == 'S')
                save_and_report();
            else if (key == 'r' || key == 'R')
            {
                reset_defaults();
                set_status("Defaults restored; save to keep them", color_muted());
            }
            else if (key == VANTA_GUI_KEY_UP || key == VANTA_GUI_KEY_DOWN)
            {
                if (selected_page == 0U)
                {
                    if (key == VANTA_GUI_KEY_UP)
                        selected_setting = selected_setting == 0U ||
                            selected_setting > 2U ? 2U : selected_setting - 1U;
                    else
                        selected_setting = selected_setting >= 2U ? 0U :
                            selected_setting + 1U;
                }
                else if (selected_page == 1U)
                {
                    if (key == VANTA_GUI_KEY_UP)
                        selected_setting = selected_setting <= 3U ? 4U : 3U;
                    else
                        selected_setting = selected_setting >= 4U ||
                            selected_setting < 3U ? 3U : 4U;
                }
            }
            else if (key == '\r' || key == '\n' || key == ' ')
            {
                if (selected_page == 2U)
                    selected_page = 0U;
                else if ((selected_page == 0U && selected_setting < 3U) ||
                         (selected_page == 1U && selected_setting >= 3U))
                    cycle_setting(selected_setting);
                else
                    selected_setting = selected_page == 0U ? 0U : 3U;
            }
            render();
        }
        else if (event.type == VANTA_GUI_EVENT_POINTER_DOWN)
        {
            if (event.x >= 18 && event.x < 154)
            {
                if (event.y >= 88 && event.y < 124)
                    selected_page = 0U;
                else if (event.y >= 132 && event.y < 168)
                    selected_page = 1U;
                else if (event.y >= 176 && event.y < 212)
                    selected_page = 2U;
            }

            if (event.x >= 184 && event.x < 596 && selected_page == 0U)
            {
                const int row_y[3] = { 116, 172, 228 };
                for (uint32_t i = 0U; i < 3U; i++)
                    if (event.y >= row_y[i] && event.y < row_y[i] + 44)
                    {
                        cycle_setting(i);
                        break;
                    }
            }
            else if (event.x >= 184 && event.x < 596 && selected_page == 1U)
            {
                const int row_y[2] = { 116, 172 };
                for (uint32_t i = 0U; i < 2U; i++)
                    if (event.y >= row_y[i] && event.y < row_y[i] + 44)
                    {
                        cycle_setting(i + 3U);
                        break;
                    }
            }

            if (event.x >= 172 && event.x < 374 &&
                event.y >= 365 && event.y < 403)
                save_and_report();
            else if (event.x >= 384 && event.x < 608 &&
                     event.y >= 365 && event.y < 403)
            {
                reset_defaults();
                set_status("Defaults restored; save to keep them", color_muted());
            }
            render();
        }
    }
    close_app();
}