#include <vanta/syscall.h>

static const char success[] = "VANTAOS_USER_CHILD_PROGRAM_OK\n";
static const char failure[] = "VANTAOS_USER_CHILD_PROGRAM_FAIL\n";

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

    (void)vanta_write(success, sizeof(success) - 1U);
    return 23;
}
