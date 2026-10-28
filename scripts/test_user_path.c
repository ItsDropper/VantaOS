/*
 * Copyright (c) 2026 ItsDropper
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>
#include <vanta/path.h>

static int expect(const char* input, const char* cwd, const char* expected)
{
    char output[256] = {0};
    if (!vanta_path_resolve(input, cwd, output, sizeof(output)) ||
        strcmp(output, expected) != 0)
    {
        fprintf(stderr, "path resolve failed: input='%s' cwd='%s' expected='%s' got='%s'\n",
                input ? input : "(null)", cwd ? cwd : "(null)",
                expected, output);
        return 0;
    }
    return 1;
}

static int expect_reject(const char* input, const char* cwd, size_t capacity)
{
    char output[8] = "dirty";
    if (vanta_path_resolve(input, cwd, output, capacity))
    {
        fprintf(stderr, "path resolver unexpectedly accepted invalid/overflow input\n");
        return 0;
    }
    return 1;
}

int main(void)
{
    if (!expect("/", "/", "/") ||
        !expect("/HOME//USER/./docs/../notes.txt", "/", "/HOME/USER/notes.txt") ||
        !expect("notes.txt", "/HOME/USER", "/HOME/USER/notes.txt") ||
        !expect("./docs//guide.txt", "/HOME/USER", "/HOME/USER/docs/guide.txt") ||
        !expect("../../../SYSTEM/APP.ELF", "/HOME/USER/docs", "/SYSTEM/APP.ELF") ||
        !expect("../../../../", "/HOME/USER", "/") ||
        !expect("a/../b/./c", "/", "/b/c") ||
        !expect_reject("", "/", 8U) ||
        !expect_reject("long-component", "/", 4U) ||
        !expect_reject("file", "/", 1U) ||
        !expect_reject("file", NULL, 8U) ||
        !expect_reject("file", "relative", 8U))
        return 1;

    puts("VANTAOS_USER_PATH_OK");
    return 0;
}
