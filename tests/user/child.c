#include <vanta/syscall.h>

static const char success[] = "VANTAOS_USER_CHILD_PROGRAM_OK\n";
static const char list_success[] = "VANTAOS_USER_DIRECTORY_LIST_OK\n";
static const char failure[] = "VANTAOS_USER_CHILD_PROGRAM_FAIL\n";

static int string_contains(const char* text, const char* needle)
{
    if (!text || !needle || !needle[0])
        return 0;
    for (size_t i = 0; text[i]; i++)
    {
        size_t j = 0U;
        while (needle[j] && text[i + j] == needle[j])
            j++;
        if (!needle[j])
            return 1;
    }
    return 0;
}

static int string_equals(const char* left, const char* right)
{
    if (!left || !right)
        return 0;
    while (*left && *right)
    {
        if (*left != *right)
            return 0;
        ++left;
        ++right;
    }
    return *left == *right;
}

int main(int argc, char** argv)
{
    if (argc != 2 || !string_equals(argv[1], "spawned-child"))
    {
        (void)vanta_write(failure, sizeof(failure) - 1U);
        return 1;
    }

    char entries[1024];
    long listed = vanta_listdir("/", entries, sizeof(entries));
    if (listed < 0 || !string_contains(entries, "CHILD.ELF") ||
        !string_contains(entries, "USERDATA.TXT"))
    {
        (void)vanta_write(failure, sizeof(failure) - 1U);
        return 2;
    }


    /* Verify that user-mode file syscalls stream beyond the old 511-byte cap. */
    static const char range_path[] = "/USERIO.BIN";
    char range_write[1024];
    char range_read[1024];
    for (unsigned int i = 0; i < sizeof(range_write); i++)
        range_write[i] = (char)((i * 29U + 7U) & 0xFFU);

    if (vanta_create_file(range_path) < 0)
    {
        (void)vanta_write(failure, sizeof(failure) - 1U);
        return 3;
    }
    long write_fd = vanta_open(range_path,
                               VANTA_OPEN_WRITE | VANTA_OPEN_TRUNCATE);
    if (write_fd < 0 ||
        vanta_file_write((int)write_fd, range_write, sizeof(range_write)) !=
            (long)sizeof(range_write) ||
        vanta_close((int)write_fd) != 0)
    {
        (void)vanta_write(failure, sizeof(failure) - 1U);
        return 4;
    }

    long read_fd = vanta_open(range_path, VANTA_OPEN_READ);
    if (read_fd < 0 ||
        vanta_read((int)read_fd, range_read, sizeof(range_read)) !=
            (long)sizeof(range_read) ||
        vanta_close((int)read_fd) != 0)
    {
        (void)vanta_write(failure, sizeof(failure) - 1U);
        return 5;
    }
    for (unsigned int i = 0; i < sizeof(range_read); i++)
    {
        if (range_read[i] != range_write[i])
        {
            (void)vanta_write(failure, sizeof(failure) - 1U);
            return 6;
        }
    }
    static const char range_success[] = "VANTAOS_USER_FILE_RANGE_IO_OK\n";
    (void)vanta_write(range_success, sizeof(range_success) - 1U);
    (void)vanta_write(list_success, sizeof(list_success) - 1U);
    (void)vanta_write(success, sizeof(success) - 1U);
    return 23;
}
