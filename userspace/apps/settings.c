/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include <vanta/gui.h>
#include <vanta/design.h>
#include <vanta/bluetooth.h>

#define SETTINGS_PATH "/SYSTEM/VANTASET.CFG"
#define INSTALL_CONFIG_PATH "/SYSTEM/SETUP.CFG"
#define CONFIG_CAPACITY 256U
#define WINDOW_WIDTH 620U
#define WINDOW_HEIGHT 510U
#define SETTING_COUNT 5U


static vanta_gui_draw_command_t commands[VANTA_GUI_MAX_COMMANDS];
static vanta_design_t design;
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
static char installed_device_name[33] = "Not configured";
static uint32_t bluetooth_port;
static uint32_t selected_device;
static uint32_t bluetooth_device_count;
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

static vanta_theme_t settings_theme(void)
{
    return (vanta_theme_t){
        .accent = accent,
        .light = light_theme,
        .contrast = high_contrast,
        .compact = compact_rows
    };
}

static uint32_t color_text(void)
{
    vanta_theme_t theme = settings_theme();
    return vanta_theme_text_color(&theme);
}

static uint32_t color_muted(void)
{
    vanta_theme_t theme = settings_theme();
    return vanta_theme_muted_color(&theme);
}

static uint32_t color_surface(void)
{
    vanta_theme_t theme = settings_theme();
    return vanta_theme_surface_color(&theme);
}

static uint32_t color_selected(void)
{
    vanta_theme_t theme = settings_theme();
    return vanta_theme_selection_color(&theme);
}

static uint32_t accent_color(void)
{
    vanta_theme_t theme = settings_theme();
    return vanta_theme_accent_color(&theme);
}

static uint32_t color_success(void)
{
    vanta_theme_t theme = settings_theme();
    return vanta_theme_success_color(&theme);
}

static uint32_t color_error(void)
{
    vanta_theme_t theme = settings_theme();
    return vanta_theme_danger_color(&theme);
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
    design.count = command_count;
    if (vanta_design_rect(&design, x, y, width, height, color))
        command_count = (uint32_t)design.count;
}

static void add_squircle(int32_t x, int32_t y, int32_t width,
                         int32_t height, uint32_t radius, uint32_t color)
{
    design.count = command_count;
    if (vanta_design_squircle_rect(&design, x, y, width, height, radius, color))
        command_count = (uint32_t)design.count;
}

static void add_panel(int32_t x, int32_t y, int32_t width, int32_t height)
{
    design.count = command_count;
    if (vanta_design_panel(&design, x, y, width, height))
        command_count = (uint32_t)design.count;
}

static void add_button(int32_t x, int32_t y, int32_t width, int32_t height,
                       const char* label, uint32_t state, int outlined)
{
    design.count = command_count;
    int added = outlined ?
        vanta_design_outline_button(&design, x, y, width, height, label, state) :
        vanta_design_button(&design, x, y, width, height, label, state);
    if (added)
        command_count = (uint32_t)design.count;
}

static void add_switch(int32_t x, int32_t y, int32_t width, int32_t height,
                       int enabled)
{
    design.count = command_count;
    if (vanta_design_switch(&design, x, y, width, height, enabled,
                            VANTA_DESIGN_STATE_DEFAULT))
        command_count = (uint32_t)design.count;
}

static void add_badge(int32_t x, int32_t y, const char* label, uint32_t state)
{
    design.count = command_count;
    if (vanta_design_badge(&design, x, y, label, state))
        command_count = (uint32_t)design.count;
}

static void add_text_scaled(int32_t x, int32_t y, const char* text,
                            uint32_t color, uint32_t scale)
{
    design.count = command_count;
    if (vanta_design_text(&design, x, y, text, color, scale))
        command_count = (uint32_t)design.count;
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

static void load_installed_device_name(void)
{
    char buffer[192];
    long descriptor = vanta_open(INSTALL_CONFIG_PATH, VANTA_OPEN_READ);
    if (descriptor < 0)
        return;
    long count = vanta_read((int)descriptor, buffer, sizeof(buffer) - 1U);
    (void)vanta_close((int)descriptor);
    if (count <= 0 || (size_t)count >= sizeof(buffer))
        return;
    buffer[(size_t)count] = '\0';

    const char key[] = "device_name=";
    size_t key_length = sizeof(key) - 1U;
    for (size_t i = 0U; i + key_length < (size_t)count; ++i) {
        if (i != 0U && buffer[i - 1U] != '\n')
            continue;
        size_t j = 0U;
        while (j < key_length && buffer[i + j] == key[j])
            ++j;
        if (j != key_length)
            continue;
        size_t start = i + key_length;
        size_t end = start;
        while (end < (size_t)count && buffer[end] != '\n')
            ++end;
        size_t length = end - start;
        if (length == 0U || length >= sizeof(installed_device_name))
            return;
        for (size_t n = 0U; n < length; ++n) {
            char ch = buffer[start + n];
            int alphanumeric = (ch >= 'a' && ch <= 'z') ||
                               (ch >= 'A' && ch <= 'Z') ||
                               (ch >= '0' && ch <= '9');
            if (!alphanumeric && !(ch == '-' && n > 0U && n + 1U < length))
                return;
            installed_device_name[n] = ch;
        }
        installed_device_name[length] = '\0';
        return;
    }
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

static char bluetooth_hex_digit(uint8_t value)
{
    value &= 0x0FU;
    if (value < 10U)
        return (char)('0' + (int)value);
    return (char)('A' + (int)(value - 10U));
}

static void bluetooth_format_address(const uint8_t address[6], char output[18])
{
    size_t used = 0U;
    for (uint32_t i = 0U; i < 6U; ++i)
    {
        uint8_t byte = address[5U - i]; /* HCI sends BD_ADDR least-significant byte first. */
        output[used++] = bluetooth_hex_digit((uint8_t)(byte >> 4));
        output[used++] = bluetooth_hex_digit(byte);
        if (i != 5U)
            output[used++] = ':';
    }
    output[used] = '\0';
}

static void bluetooth_refresh(void)
{
    long adapters = vanta_bluetooth_adapter_count();
    bluetooth_port = 0U;
    if (adapters > 0)
    {
        uint32_t port = 0U;
        if (vanta_bluetooth_adapter_port(0U, &port) > 0)
            bluetooth_port = port;
    }
    long count = bluetooth_port != 0U ?
        vanta_bluetooth_discovered_count(bluetooth_port) : 0;
    bluetooth_device_count = count > 0 ? (uint32_t)count : 0U;
    if (bluetooth_device_count == 0U)
        selected_device = 0U;
    else if (selected_device >= bluetooth_device_count)
        selected_device = bluetooth_device_count - 1U;
}

static void bluetooth_scan(void)
{
    bluetooth_refresh();
    if (bluetooth_port == 0U)
    {
        set_status("No USB Bluetooth adapter detected", color_error());
        return;
    }
    long result = vanta_bluetooth_scan(bluetooth_port);
    if (result > 0)
        set_status("Scan started; list updates as devices appear", color_success());
    else
        set_status("Scan could not start; check adapter/controller", color_error());
}

static void bluetooth_cancel_scan(void)
{
    if (bluetooth_port == 0U)
    {
        set_status("No Bluetooth adapter available", color_error());
        return;
    }
    if (vanta_bluetooth_cancel_scan(bluetooth_port) > 0)
        set_status("Scan cancellation requested", color_muted());
    else
        set_status("No active scan or cancellation failed", color_error());
}

static void bluetooth_connect_selected(void)
{
    bluetooth_refresh();
    if (bluetooth_port == 0U)
    {
        set_status("Connect a USB Bluetooth adapter first", color_error());
        return;
    }
    usb_bluetooth_inquiry_result_t result = {0};
    if (vanta_bluetooth_discovered_get(
            bluetooth_port, selected_device, &result) <= 0)
    {
        set_status("Select a discovered device first", color_error());
        return;
    }
    uint16_t handle = 0U;
    long state = vanta_bluetooth_peer_state(
        bluetooth_port, result.address, &handle);
    if (state == 1)
    {
        set_status("Device is already connected", color_success());
        return;
    }
    if (state == 2)
    {
        set_status("Connection attempt already in progress", color_muted());
        return;
    }
    long connected = vanta_bluetooth_connect(bluetooth_port, &result);
    if (connected > 0)
        set_status("Connection requested; waiting for controller", color_success());
    else
        set_status("Connect failed; device may be out of range", color_error());
}

static void bluetooth_cancel_selected(void)
{
    if (bluetooth_port == 0U || bluetooth_device_count == 0U)
    {
        set_status("No selected Bluetooth device", color_error());
        return;
    }
    usb_bluetooth_inquiry_result_t result = {0};
    if (vanta_bluetooth_discovered_get(
            bluetooth_port, selected_device, &result) <= 0)
    {
        set_status("Selected device is no longer available", color_error());
        return;
    }
    if (vanta_bluetooth_cancel_connection(bluetooth_port, result.address) > 0)
        set_status("Connection cancellation requested", color_muted());
    else
        set_status("No cancellable connection attempt", color_error());
}

static void render(void)
{
    command_count = 0U;
    vanta_theme_t active_theme = settings_theme();
    vanta_design_init(&design, commands, VANTA_GUI_MAX_COMMANDS, &active_theme);
    uint32_t text = color_text();
    uint32_t muted = color_muted();
    uint32_t selected = color_selected();
    uint32_t accent_value = accent_color();

    add_text(16, 10, "Settings", text);
    add_text(16, 30, "Personalize how VantaOS looks and feels.", muted);

    /* Persistent navigation keeps the three settings areas easy to scan. */
    add_panel(12, 56, 148, 294);
    add_text(24, 66, "CATEGORIES", muted);
    const int nav_y[4] = { 88, 132, 176, 220 };
    const char* nav_labels[4] = { "Appearance", "Accessibility", "About", "Bluetooth" };
    for (uint32_t i = 0U; i < 4U; i++)
    {
        /*
         * Navigation uses the same semantic control as every other VDS app:
         * selected = accent-filled, idle = neutral outlined. This avoids a
         * Settings-only button treatment drifting from the shared system.
         */
        add_button(18, nav_y[i], 136, 36, nav_labels[i],
                   selected_page == i ? VANTA_DESIGN_STATE_SELECTED :
                                         VANTA_DESIGN_STATE_DEFAULT,
                   selected_page != i);
    }

    add_panel(172, 56, 432, 294);
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
            add_squircle(184, row_y[i], 412, 44, VANTA_DESIGN_RADIUS_CONTROL,
                         selected_setting == setting ? selected : color_surface());
            add_text(196, row_y[i] + 7, labels[i], text);
            add_text(196, row_y[i] + 25, "Select to cycle option", muted);
            if (setting == 1U)
                add_switch(548, row_y[i] + 11, 36, 22, light_theme);
            else if (setting == 2U)
                add_switch(548, row_y[i] + 11, 36, 22, compact_rows);
            else
                add_text(464, row_y[i] + 15, values[i], accent_value);
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
            if (setting == 4U)
                add_switch(548, row_y[i] + 11, 36, 22, high_contrast);
            else
                add_text(500, row_y[i] + 15, values[i], text);
        }
        add_text(190, 236, "Text size currently affects Settings only.", muted);
    }
    else if (selected_page == 2U)
    {
        add_text(190, 68, "About Settings", text);
        add_text(190, 98, "VantaOS Settings", accent_value);
        add_text(190, 124, "Standalone ring-3 application", text);
        add_text(190, 150, "Device name:", muted);
        add_text(300, 150, installed_device_name, text);
        add_text(190, 174, "Preferences are stored in:", muted);
        add_text(190, 196, "/SYSTEM/VANTASET.CFG", text);
        add_text(190, 224, "Theme changes are applied when saved.", muted);
        add_text(190, 248, "Time zone is saved by Setup, but", muted);
        add_text(190, 268, "clock integration is not implemented.", muted);
        add_text(190, 292, "Language and keyboard setup are not", muted);
        add_text(190, 312, "implemented yet.", muted);
    }
    else
    {
        add_text(190, 68, "Bluetooth", text);
        add_text(190, 88, "Discover nearby Classic Bluetooth devices.", muted);
        bluetooth_refresh();
        if (bluetooth_port == 0U)
        {
            add_squircle(188, 112, 408, 76, VANTA_DESIGN_RADIUS_SURFACE, selected);
            add_text(202, 126, "No USB Bluetooth adapter detected", text);
            add_text(202, 150, "Connect a supported USB adapter, then scan.", muted);
        }
        else
        {
            add_badge(190, 104, "Adapter detected",
                      VANTA_DESIGN_STATE_SUCCESS);
            add_text(330, 108, "USB port", muted);
            char port_text[12];
            uint32_t value = bluetooth_port;
            uint32_t digits = 0U;
            do {
                port_text[digits++] = (char)('0' + (value % 10U));
                value /= 10U;
            } while (value != 0U && digits < sizeof(port_text) - 1U);
            for (uint32_t i = 0U; i < digits / 2U; ++i) {
                char tmp = port_text[i];
                port_text[i] = port_text[digits - 1U - i];
                port_text[digits - 1U - i] = tmp;
            }
            port_text[digits] = '\0';
            add_text(390, 108, port_text, text);
            add_text(190, 132, "Nearby devices", text);
            if (bluetooth_device_count == 0U)
                add_text(202, 160, "No results yet. Select Scan devices.", muted);
            uint32_t visible = bluetooth_device_count < 4U ?
                bluetooth_device_count : 4U;
            uint32_t first_visible = selected_device >= 4U ?
                selected_device - 3U : 0U;
            for (uint32_t i = 0U; i < visible; ++i)
            {
                uint32_t device_index = first_visible + i;
                usb_bluetooth_inquiry_result_t device = {0};
                if (vanta_bluetooth_discovered_get(
                        bluetooth_port, device_index, &device) <= 0)
                    continue;
                int y = 154 + (int)i * 42;
                add_squircle(188, y, 408, 36, VANTA_DESIGN_RADIUS_CONTROL,
                             selected_device == device_index ? selected : color_surface());
                char address[18];
                bluetooth_format_address(device.address, address);
                add_text(198, y + 10, address, text);
                uint16_t handle = 0U;
                long peer = vanta_bluetooth_peer_state(
                    bluetooth_port, device.address, &handle);
                const char* state = peer == 1 ? "Connected" :
                    peer == 2 ? "Connecting" : "Found";
                add_badge(468, y + 7, state,
                          peer == 1 ? VANTA_DESIGN_STATE_SUCCESS :
                          peer == 2 ? VANTA_DESIGN_STATE_WARNING :
                                      VANTA_DESIGN_STATE_DEFAULT);
            }
            if (bluetooth_device_count > 4U)
                add_text(190, 326, "Use Up/Down to select additional results.", muted);
        }
    }

    add_button(172, 365, 202, 38,
               selected_page == 3U ? "Scan devices" : "Save changes",
               VANTA_DESIGN_STATE_SELECTED, 0);
    add_button(384, 365, 220, 38,
               selected_page == 3U ? "Connect selected" : "Restore defaults",
               VANTA_DESIGN_STATE_DEFAULT, 1);
    add_text(16, 414, status_buffer, status_color);
    add_text(16, 436, selected_page == 3U ? "Select device; S scan, X cancel scan, C cancel connect, Q close." : "Click a category or setting; S saves, R resets, B opens Bluetooth, Q closes.", muted);
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
        set_status("Saved /SYSTEM/VANTASET.CFG", color_success());
    else
        set_status("Save failed: check disk space and filesystem", color_error());
}

int main(int argc, char** argv)
{
    reset_defaults();
    status_color = color_muted();
    if (argc == 2 && text_equals(argv[1], "--self-test"))
        vanta_exit(run_storage_self_test());
    if (argc != 1)
        vanta_exit(2);

    if (load_config())
        set_status("Loaded saved preferences", color_success());
    load_installed_device_name();

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
            if (selected_page == 3U)
                render();
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
            else if (key >= '1' && key <= '5' && selected_page != 3U)
                cycle_setting(key - '1');
            else if (key == 'a' || key == 'A')
                selected_page = 0U;
            else if ((key == 'c' || key == 'C') && selected_page != 3U)
                selected_page = 1U;
            else if (key == 'i' || key == 'I')
                selected_page = 2U;
            else if ((key == 's' || key == 'S') && selected_page == 3U)
                bluetooth_scan();
            else if (key == 'x' || key == 'X')
                bluetooth_cancel_scan();
            else if ((key == 'c' || key == 'C') && selected_page == 3U)
                bluetooth_cancel_selected();
            else if (key == 'b' || key == 'B')
                selected_page = 3U;
            else if ((key == 's' || key == 'S') && selected_page != 3U)
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
                else if (selected_page == 3U && bluetooth_device_count > 0U)
                {
                    if (key == VANTA_GUI_KEY_UP)
                        selected_device = selected_device == 0U ?
                            bluetooth_device_count - 1U : selected_device - 1U;
                    else
                        selected_device = selected_device + 1U >= bluetooth_device_count ?
                            0U : selected_device + 1U;
                }
            }
            else if (key == '\r' || key == '\n' || key == ' ')
            {
                if (selected_page == 2U)
                    selected_page = 0U;
                else if (selected_page == 3U)
                    bluetooth_connect_selected();
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
                else if (event.y >= 220 && event.y < 256)
                    selected_page = 3U;
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

            if (event.x >= 184 && event.x < 596 && selected_page == 3U &&
                event.y >= 154 && event.y < 322)
            {
                uint32_t row = (uint32_t)(event.y - 154) / 42U;
                uint32_t first_visible = selected_device >= 4U ?
                    selected_device - 3U : 0U;
                if (row < bluetooth_device_count && row < 4U &&
                    first_visible + row < bluetooth_device_count)
                    selected_device = first_visible + row;
            }

            if (event.x >= 172 && event.x < 374 &&
                event.y >= 365 && event.y < 403)
            {
                if (selected_page == 3U)
                    bluetooth_scan();
                else
                    save_and_report();
            }
            else if (event.x >= 384 && event.x < 608 &&
                     event.y >= 365 && event.y < 403)
            {
                if (selected_page == 3U)
                    bluetooth_connect_selected();
                else
                {
                    reset_defaults();
                    set_status("Defaults restored; save to keep them", color_muted());
                }
            }
            render();
        }
    }
    close_app();
}