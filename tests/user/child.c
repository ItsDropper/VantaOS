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

    (void)vanta_write(list_success, sizeof(list_success) - 1U);
    (void)vanta_write(success, sizeof(success) - 1U);
    return 23;
}
