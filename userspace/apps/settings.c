#include <vanta/gui.h>

#define SETTINGS_PATH "/SYSTEM/VANTASET.CFG"
#define CONFIG_CAPACITY 128U
#define WINDOW_WIDTH 560U
#define WINDOW_HEIGHT 420U

#define COLOR_TEXT 0x00F1F5F9U
#define COLOR_MUTED 0x0094A3B8U
#define COLOR_SURFACE 0x00212D3BU
#define COLOR_SURFACE_HI 0x00313F50U
#define COLOR_BLUE 0x003B82F6U
#define COLOR_TEAL 0x0014B8A6U
#define COLOR_SUCCESS 0x0022C55EU
#define COLOR_ERROR 0x00F87171U

static vanta_gui_draw_command_t commands[VANTA_GUI_MAX_COMMANDS];
static uint32_t command_count;
static uint32_t window_id;
static int accent_teal;
static int compact_rows;
static int running = 1;

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

static void add_text(int32_t x, int32_t y, const char* text, uint32_t color)
{
    size_t length = text_length(text);
    if (command_count >= VANTA_GUI_MAX_COMMANDS || length == 0U ||
        length > VANTA_GUI_TEXT_MAX)
        return;
    vanta_gui_draw_command_t* command = &commands[command_count++];
    *command = (vanta_gui_draw_command_t){
        .type = VANTA_GUI_DRAW_TEXT,
        .x = x, .y = y, .color = color, .scale = 1U
    };
    for (size_t i = 0U; i <= length; i++)
        command->text[i] = text[i];
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

    if (!text_contains(buffer, "VANTA_SETTINGS=1\n"))
        return 0;
    if (text_contains(buffer, "accent=teal\n"))
        accent_teal = 1;
    else if (text_contains(buffer, "accent=blue\n"))
        accent_teal = 0;
    else
        return 0;

    if (text_contains(buffer, "compact=1\n"))
        compact_rows = 1;
    else if (text_contains(buffer, "compact=0\n"))
        compact_rows = 0;
    else
        return 0;
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
    const char* config = accent_teal
        ? "VANTA_SETTINGS=1\naccent=teal\ncompact="
        : "VANTA_SETTINGS=1\naccent=blue\ncompact=";
    const char* ending = compact_rows ? "1\n" : "0\n";
    char buffer[64];
    size_t used = 0U;
    size_t prefix_length = text_length(config);
    size_t ending_length = text_length(ending);
    for (size_t i = 0U; i < prefix_length; i++)
        buffer[used++] = config[i];
    for (size_t i = 0U; i < ending_length; i++)
        buffer[used++] = ending[i];

    long descriptor = open_config_for_write();
    if (descriptor < 0)
        return 0;
    long written = vanta_file_write((int)descriptor, buffer, used);
    long closed = vanta_close((int)descriptor);
    return written == (long)used && closed == 0;
}

static int run_storage_self_test(void)
{
    static const char expected[] =
        "VANTA_SETTINGS=1\naccent=teal\ncompact=1\n";
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

static void render(const char* status, uint32_t status_color)
{
    command_count = 0U;
    uint32_t accent = accent_teal ? COLOR_TEAL : COLOR_BLUE;
    add_text(16, 16, "Personalize your workspace", COLOR_TEXT);
    add_text(16, 36, "Changes are local to this VantaOS disk.", COLOR_MUTED);

    add_rect(12, 64, 520, 66, COLOR_SURFACE);
    add_text(24, 76, "Accent color", COLOR_TEXT);
    add_text(24, 100, accent_teal ? "Teal" : "Blue", accent);
    add_rect(450, 82, 56, 28, accent);
    add_rect(456, 88, 44, 16, COLOR_SURFACE_HI);

    uint32_t row_color = compact_rows ? COLOR_SURFACE_HI : COLOR_SURFACE;
    add_rect(12, 144, 520, 66, row_color);
    add_text(24, 156, "Compact rows", COLOR_TEXT);
    add_text(24, 180, compact_rows ? "Enabled" : "Disabled", COLOR_MUTED);
    add_rect(450, 162, 56, 28, compact_rows ? accent : COLOR_SURFACE_HI);
    add_rect( compact_rows ? 480 : 456, 168, 20, 16, COLOR_TEXT);

    add_rect(12, 232, 180, 40, accent);
    add_text(28, 244, "Save changes", COLOR_TEXT);
    add_text(16, 292, status, status_color);
    add_text(16, 324, "Click a row to change it. Press 1, 2, or S.", COLOR_MUTED);

    (void)vanta_gui_draw(window_id, commands, command_count);
}

static void close_app(void)
{
    if (window_id != 0U)
        (void)vanta_gui_close_window(window_id);
    vanta_exit(0);
}

int main(int argc, char** argv)
{
    if (argc == 2 && text_equals(argv[1], "--self-test"))
        vanta_exit(run_storage_self_test());

    if (argc != 1)
        vanta_exit(2);

    int config_loaded = load_config();
    const char* status = config_loaded
        ? "Loaded /SYSTEM/VANTASET.CFG"
        : "Defaults active; save to create config";
    uint32_t status_color = config_loaded ? COLOR_SUCCESS : COLOR_MUTED;

    vanta_gui_window_request_t request = {
        .width = WINDOW_WIDTH,
        .height = WINDOW_HEIGHT,
        .title = "Settings"
    };
    long created = vanta_gui_create_window(&request);
    if (created <= 0)
        vanta_exit(3);
    window_id = (uint32_t)created;
    render(status, status_color);

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

        if (event.type == VANTA_GUI_EVENT_KEY)
        {
            uint32_t key = event.value;
            if (key == 'q' || key == 'Q')
                close_app();
            if (key == '1')
            {
                accent_teal = !accent_teal;
                status = "Unsaved changes";
                status_color = COLOR_MUTED;
            }
            else if (key == '2')
            {
                compact_rows = !compact_rows;
                status = "Unsaved changes";
                status_color = COLOR_MUTED;
            }
            else if (key == 's' || key == 'S')
            {
                if (save_config())
                {
                    status = "Saved to /VANTASET.CFG";
                    status_color = COLOR_SUCCESS;
                }
                else
                {
                    status = "Save failed; disk write unavailable";
                    status_color = COLOR_ERROR;
                }
            }
            render(status, status_color);
        }
        else if (event.type == VANTA_GUI_EVENT_POINTER_DOWN)
        {
            if (event.x >= 12 && event.x < 532 &&
                event.y >= 64 && event.y < 130)
            {
                accent_teal = !accent_teal;
                status = "Unsaved changes";
                status_color = COLOR_MUTED;
            }
            else if (event.x >= 12 && event.x < 532 &&
                     event.y >= 144 && event.y < 210)
            {
                compact_rows = !compact_rows;
                status = "Unsaved changes";
                status_color = COLOR_MUTED;
            }
            else if (event.x >= 12 && event.x < 192 &&
                     event.y >= 232 && event.y < 272)
            {
                if (save_config())
                {
                    status = "Saved to /VANTASET.CFG";
                    status_color = COLOR_SUCCESS;
                }
                else
                {
                    status = "Save failed; disk write unavailable";
                    status_color = COLOR_ERROR;
                }
            }
            render(status, status_color);
        }
    }
    close_app();
}
