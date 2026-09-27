#include "shell_internal.h"

char shell_buffer[SHELL_BUFFER_SIZE];
unsigned int shell_length;
unsigned int shell_cursor;

char shell_history[SHELL_HISTORY_SIZE][SHELL_BUFFER_SIZE];
unsigned int shell_history_count;
unsigned int shell_history_position;

char shell_history_draft[SHELL_BUFFER_SIZE];
unsigned int shell_history_draft_length;

multiboot_info_t* multiboot_info;
int pmm_initialized;
int pci_initialized;
char shell_cwd[FS_PATH_MAX] = "/";

void shell_set_multiboot_info(multiboot_info_t* mbd)
{
    multiboot_info = mbd;
}

void shell_initialize(void)
{
    pmm_initialized = pmm_is_initialized();

    shell_clear_buffer();

    shell_history_count = 0;
    shell_history_position = 0;

    shell_history_draft_length = 0;
    shell_copy_path(shell_cwd, "/");

    for (unsigned int i = 0;
         i < SHELL_HISTORY_SIZE;
         i++)
    {
        shell_history[i][0] = 0;
    }

    shell_history_draft[0] = 0;
}
void shell_handle_event(
    keyboard_event_t event
)
{
    switch (event)
    {
        case KEY_EVENT_UP:
            shell_history_up();
            break;

        case KEY_EVENT_DOWN:
            shell_history_down();
            break;

        case KEY_EVENT_LEFT:
            shell_move_left();
            break;

        case KEY_EVENT_RIGHT:
            shell_move_right();
            break;

        case KEY_EVENT_HOME:
            shell_move_home();
            break;

        case KEY_EVENT_END:
            shell_move_end();
            break;

        case KEY_EVENT_DELETE:
            shell_delete();
            break;

        default:
            break;
    }
}
void shell_handle_char(char c)
{
    if (c == '\b')
    {
        shell_backspace();
        return;
    }

    if (c == '\n')
    {
        shell_execute();
        return;
    }

    if (c >= 32 && c <= 126)
    {
        shell_insert_char(c);
    }
}