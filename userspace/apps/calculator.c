/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include <vanta/gui.h>
#include <vanta/theme.h>
#include <stdint.h>
#include <stddef.h>

#define INPUT_CAPACITY 64U
#define COLOR_ERROR 0x00F87171U
#define COLOR_SUCCESS 0x0022C55EU

static char expression[INPUT_CAPACITY];
static size_t expression_length;
static char result_text[96] = "Type an integer expression and press Enter";
static char status_text[96] = "Supports +  -  *  /  %  and parentheses";
static uint32_t status_color;
static uint32_t window_id;
static uint32_t command_count;
static vanta_gui_draw_command_t commands[VANTA_GUI_MAX_COMMANDS];
static vanta_theme_t theme;
static const char *parse_cursor;
static int parse_failed;

static size_t text_length(const char *text)
{
    size_t n = 0U;
    while (text && text[n]) ++n;
    return n;
}

static void copy_text(char *destination, size_t capacity, const char *source)
{
    if (!destination || capacity == 0U) return;
    size_t i = 0U;
    while (source && source[i] && i + 1U < capacity)
    {
        destination[i] = source[i];
        ++i;
    }
    destination[i] = '\0';
}

static void add_rect(int32_t x, int32_t y, int32_t width, int32_t height,
                     uint32_t color)
{
    if (command_count >= VANTA_GUI_MAX_COMMANDS || width <= 0 || height <= 0)
        return;
    commands[command_count++] = (vanta_gui_draw_command_t){
        .type = VANTA_GUI_DRAW_FILL_RECT, .x = x, .y = y,
        .width = width, .height = height, .color = color, .scale = 1U
    };
}

static void add_text(int32_t x, int32_t y, const char *text, uint32_t color)
{
    size_t length = text_length(text);
    if (command_count >= VANTA_GUI_MAX_COMMANDS || length == 0U ||
        length > VANTA_GUI_TEXT_MAX)
        return;
    vanta_gui_draw_command_t *command = &commands[command_count++];
    *command = (vanta_gui_draw_command_t){
        .type = VANTA_GUI_DRAW_TEXT, .x = x, .y = y,
        .color = color, .scale = 1U
    };
    for (size_t i = 0U; i <= length; ++i) command->text[i] = text[i];
}

static void set_status(const char *text, uint32_t color)
{
    copy_text(status_text, sizeof(status_text), text);
    status_color = color;
}

static void append_result(int64_t value)
{
    char reverse[32];
    size_t length = 0U;
    uint64_t magnitude = (uint64_t)value;
    int negative = value < 0;
    if (negative) magnitude = 0U - magnitude;
    do
    {
        reverse[length++] = (char)('0' + magnitude % 10U);
        magnitude /= 10U;
    } while (magnitude != 0U && length < sizeof(reverse));
    size_t out = 0U;
    if (negative) result_text[out++] = '-';
    while (length > 0U && out + 1U < sizeof(result_text))
        result_text[out++] = reverse[--length];
    result_text[out] = '\0';
}

static void skip_spaces(void)
{
    while (*parse_cursor == ' ' || *parse_cursor == '\t') ++parse_cursor;
}

static int64_t parse_sum(void);

static int64_t parse_primary(void)
{
    skip_spaces();
    if (*parse_cursor == '(')
    {
        ++parse_cursor;
        int64_t value = parse_sum();
        skip_spaces();
        if (*parse_cursor != ')') parse_failed = 1;
        else ++parse_cursor;
        return value;
    }
    if (*parse_cursor == '+')
    {
        ++parse_cursor;
        return parse_primary();
    }
    if (*parse_cursor == '-')
    {
        ++parse_cursor;
        int64_t value = parse_primary();
        if (value == INT64_MIN)
        {
            parse_failed = 1;
            return 0;
        }
        return -value;
    }
    if (*parse_cursor < '0' || *parse_cursor > '9')
    {
        parse_failed = 1;
        return 0;
    }
    uint64_t value = 0U;
    while (*parse_cursor >= '0' && *parse_cursor <= '9')
    {
        uint32_t digit = (uint32_t)(*parse_cursor - '0');
        if (value > ((uint64_t)INT64_MAX - digit) / 10U)
        {
            parse_failed = 1;
            return 0;
        }
        value = value * 10U + digit;
        ++parse_cursor;
    }
    return (int64_t)value;
}

static int64_t parse_product(void)
{
    int64_t value = parse_primary();
    for (;;)
    {
        skip_spaces();
        char op = *parse_cursor;
        if (op != '*' && op != '/' && op != '%') return value;
        ++parse_cursor;
        int64_t rhs = parse_primary();
        if (parse_failed) return 0;
        if (op == '*')
        {
            int64_t next;
            if (__builtin_mul_overflow(value, rhs, &next))
            {
                parse_failed = 1;
                return 0;
            }
            value = next;
        }
        else
        {
            if (rhs == 0 || (value == INT64_MIN && rhs == -1))
            {
                parse_failed = 1;
                return 0;
            }
            value = op == '/' ? value / rhs : value % rhs;
        }
    }
}

static int64_t parse_sum(void)
{
    int64_t value = parse_product();
    for (;;)
    {
        skip_spaces();
        char op = *parse_cursor;
        if (op != '+' && op != '-') return value;
        ++parse_cursor;
        int64_t rhs = parse_product();
        if (parse_failed) return 0;
        int64_t next;
        int overflow = op == '+' ?
            __builtin_add_overflow(value, rhs, &next) :
            __builtin_sub_overflow(value, rhs, &next);
        if (overflow)
        {
            parse_failed = 1;
            return 0;
        }
        value = next;
    }
}

static void calculate(void)
{
    if (expression_length == 0U)
    {
        set_status("Enter an expression first", COLOR_ERROR);
        return;
    }
    parse_cursor = expression;
    parse_failed = 0;
    int64_t value = parse_sum();
    skip_spaces();
    if (parse_failed || *parse_cursor != '\0')
    {
        copy_text(result_text, sizeof(result_text), "Invalid expression or integer overflow");
        set_status("Check the expression and try again", COLOR_ERROR);
        return;
    }
    append_result(value);
    set_status("Calculated successfully", COLOR_SUCCESS);
}

static void render(void)
{
    command_count = 0U;
    add_rect(8, 8, 520, 256, vanta_theme_surface_color(&theme));
    add_rect(8, 8, 520, 2, vanta_theme_accent_color(&theme));
    add_text(20, 20, "Calculator", vanta_theme_text_color(&theme));
    add_text(20, 42, "INTEGER MODE  |  PRECEDENCE AND PARENTHESES",
             vanta_theme_muted_color(&theme));
    add_rect(16, 66, 504, 44, vanta_theme_surface_color(&theme));
    add_rect(16, 66, 504, 1, vanta_theme_border_color(&theme));
    add_text(26, 82, expression_length ? expression : "0",
             vanta_theme_text_color(&theme));
    add_text(20, 124, "RESULT", vanta_theme_muted_color(&theme));
    add_text(20, 146, result_text, vanta_theme_text_color(&theme));
    add_rect(16, 178, 504, 1, vanta_theme_border_color(&theme));
    add_text(20, 188, status_text, status_color ? status_color :
             vanta_theme_muted_color(&theme));
    add_text(20, 212, "Enter: calculate   Backspace: edit   Esc: clear",
             vanta_theme_muted_color(&theme));
    add_text(20, 232, "Keys: + - * / % ( ) and digits | Ctrl+A clears",
             vanta_theme_muted_color(&theme));
    (void)vanta_gui_draw(window_id, commands, command_count);
}

static void close_app(void)
{
    if (window_id) (void)vanta_gui_close_window(window_id);
    vanta_exit(0);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    theme = vanta_theme_load();
    vanta_gui_window_request_t request = {
        .width = 536U, .height = 280U, .title = "Calculator"
    };
    long created = vanta_gui_create_window(&request);
    if (created <= 0) vanta_exit(3);
    window_id = (uint32_t)created;
    render();

    for (;;)
    {
        vanta_gui_event_t event;
        long polled = vanta_gui_poll_event(window_id, &event);
        if (polled < 0) close_app();
        if (polled == 0)
        {
            (void)vanta_yield();
            continue;
        }
        if (event.type == VANTA_GUI_EVENT_CLOSE_REQUEST) close_app();
        if (event.type == VANTA_GUI_EVENT_THEME_CHANGED)
        {
            theme = vanta_theme_load();
            render();
            continue;
        }
        if (event.type != VANTA_GUI_EVENT_KEY) continue;
        uint32_t key = event.value;
        if ((event.reserved & VANTA_GUI_MOD_CTRL) != 0U &&
            (key == 'a' || key == 'A' || key == 1U))
        {
            expression_length = 0U;
            expression[0] = '\0';
            set_status("Expression cleared", 0U);
            render();
            continue;
        }
        if (key == '\n' || key == '\r')
            calculate();
        else if (key == 8U || key == 127U)
        {
            if (expression_length > 0U)
                expression[--expression_length] = '\0';
        }
        else if (key == 27U)
        {
            expression_length = 0U;
            expression[0] = '\0';
            copy_text(result_text, sizeof(result_text), "Type an integer expression and press Enter");
            set_status("Cleared", 0U);
        }
        else if ((key >= '0' && key <= '9') || key == '+' || key == '-' ||
                 key == '*' || key == '/' || key == '%' || key == '(' ||
                 key == ')' || key == ' ')
        {
            if (expression_length + 1U < sizeof(expression))
            {
                expression[expression_length++] = (char)key;
                expression[expression_length] = '\0';
                set_status("Ready to calculate", 0U);
            }
            else set_status("Expression limit reached", COLOR_ERROR);
        }
        else continue;
        render();
    }
}
