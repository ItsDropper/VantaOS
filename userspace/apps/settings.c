/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include <vanta/gui.h>
#include <vanta/bluetooth.h>

#define SETTINGS_PATH "/SYSTEM/VANTASET.CFG"
#define INSTALL_CONFIG_PATH "/SYSTEM/SETUP.CFG"
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
        set_status("No USB Bluetooth adapter detected", COLOR_ERROR);
        return;
    }
    long result = vanta_bluetooth_scan(bluetooth_port);
    if (result > 0)
        set_status("Scanning for nearby Classic Bluetooth devices...", COLOR_SUCCESS);
    else
        set_status("Scan could not start; check adapter/controller", COLOR_ERROR);
}

static void bluetooth_cancel_scan(void)
{
    if (bluetooth_port == 0U)
    {
        set_status("No Bluetooth adapter available", COLOR_ERROR);
        return;
    }
    if (vanta_bluetooth_cancel_scan(bluetooth_port) > 0)
        set_status("Scan cancellation requested", COLOR_MUTED);
    else
        set_status("No active scan or cancellation failed", COLOR_ERROR);
}

static void bluetooth_connect_selected(void)
{
    bluetooth_refresh();
    if (bluetooth_port == 0U)
    {
        set_status("Connect a USB Bluetooth adapter first", COLOR_ERROR);
        return;
    }
    usb_bluetooth_inquiry_result_t result = {0};
    if (vanta_bluetooth_discovered_get(
            bluetooth_port, selected_device, &result) <= 0)
    {
        set_status("Select a discovered device first", COLOR_ERROR);
        return;
    }
    uint16_t handle = 0U;
    long state = vanta_bluetooth_peer_state(
        bluetooth_port, result.address, &handle);
    if (state == 1)
    {
        set_status("Device is already connected", COLOR_SUCCESS);
        return;
    }
    if (state == 2)
    {
        set_status("Connection attempt already in progress", COLOR_MUTED);
        return;
    }
    long connected = vanta_bluetooth_connect(bluetooth_port, &result);
    if (connected > 0)
        set_status("Connection requested; waiting for controller", COLOR_SUCCESS);
    else
        set_status("Connect failed; device may be out of range", COLOR_ERROR);
}

static void bluetooth_cancel_selected(void)
{
    if (bluetooth_port == 0U || bluetooth_device_count == 0U)
    {
        set_status("No selected Bluetooth device", COLOR_ERROR);
        return;
    }
    usb_bluetooth_inquiry_result_t result = {0};
    if (vanta_bluetooth_discovered_get(
            bluetooth_port, selected_device, &result) <= 0)
    {
        set_status("Selected device is no longer available", COLOR_ERROR);
        return;
    }
    if (vanta_bluetooth_cancel_connection(bluetooth_port, result.address) > 0)
        set_status("Connection cancellation requested", COLOR_MUTED);
    else
        set_status("No cancellable connection attempt", COLOR_ERROR);
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
    const int nav_y[4] = { 88, 132, 176, 220 };
    const char* nav_labels[4] = { "Appearance", "Accessibility", "About", "Bluetooth" };
    for (uint32_t i = 0U; i < 4U; i++)
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
        add_text(190, 248, "Time zone is saved by Setup but not applied to the clock.", muted);
        add_text(190, 272, "Language and keyboard integration is not implemented yet.", muted);
    }
    else
    {
        add_text(190, 68, "Bluetooth", text);
        add_text(190, 88, "Discover and connect to nearby Classic Bluetooth devices.", muted);
        bluetooth_refresh();
        if (bluetooth_port == 0U)
        {
            add_rect(188, 112, 408, 76, selected);
            add_text(202, 126, "No USB Bluetooth adapter detected", text);
            add_text(202, 150, "Connect a supported USB adapter, then scan.", muted);
        }
        else
        {
            add_text(190, 108, "Adapter detected", COLOR_SUCCESS);
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
                add_rect(188, y, 408, 36,
                         selected_device == device_index ? selected : color_surface());
                char address[18];
                bluetooth_format_address(device.address, address);
                add_text(198, y + 10, address, text);
                uint16_t handle = 0U;
                long peer = vanta_bluetooth_peer_state(
                    bluetooth_port, device.address, &handle);
                const char* state = peer == 1 ? "Connected" :
                    peer == 2 ? "Connecting" : "Found";
                add_text(476, y + 10, state,
                         peer == 1 ? COLOR_SUCCESS :
                         peer == 2 ? COLOR_AMBER : muted);
            }
            if (bluetooth_device_count > 4U)
                add_text(190, 326, "Use Up/Down to select additional results.", muted);
        }
    }

    add_rect(172, 365, 202, 38, accent_value);
    add_text(190, 377, selected_page == 3U ? "Scan devices" : "Save changes",
             0x00FFFFFFU);
    add_rect(384, 365, 224, 38, color_selected());
    add_text(402, 377, selected_page == 3U ? "Connect selected" : "Restore defaults", text);
    add_text(16, 414, status_buffer, status_color);
    add_text(16, 436, selected_page == 3U ? "Select a device; S scans, X cancels scan, C cancels connect, Q closes." : "Click a category or setting; S saves, R resets, B opens Bluetooth, Q closes.", muted);
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