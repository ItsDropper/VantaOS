/*
 * Copyright (c) 2026 ItsDropper
 * Licensed under the MIT License.
 *
 * Host regression tests for the exact parser used by the ring-3 Calculator.
 * Dead GUI/syscall sections are discarded by the linker; no OS boot is needed.
 */
#include <stdio.h>
#include <string.h>

#define main calculator_app_entry
#include "../userspace/apps/calculator.c"
#undef main

static int expect_result(const char *input, const char *expected)
{
    size_t length = 0U;
    while (input[length] && length + 1U < sizeof(expression))
    {
        expression[length] = input[length];
        ++length;
    }
    expression[length] = '\0';
    expression_length = length;
    calculate();
    if (strcmp(result_text, expected) != 0)
    {
        fprintf(stderr, "calculator: %s -> %s (expected %s)\n",
                input, result_text, expected);
        return 0;
    }
    return 1;
}

static int expect_error(const char *input)
{
    size_t length = 0U;
    while (input[length] && length + 1U < sizeof(expression))
    {
        expression[length] = input[length];
        ++length;
    }
    expression[length] = '\0';
    expression_length = length;
    calculate();
    if (strcmp(result_text, "Invalid expression or integer overflow") != 0)
    {
        fprintf(stderr, "calculator: %s unexpectedly returned %s\n",
                input, result_text);
        return 0;
    }
    return 1;
}

int main(void)
{
    int ok = 1;
    ok &= expect_result("2+3*4", "14");
    ok &= expect_result("(2+3)*4", "20");
    ok &= expect_result("-7 + 2", "-5");
    ok &= expect_result("17 % 5", "2");
    ok &= expect_result("100 / 4 / 5", "5");
    ok &= expect_result("  9 + (8 - 3) * 2 ", "19");
    ok &= expect_error("1/0");
    ok &= expect_error("1%0");
    ok &= expect_error("2+");
    ok &= expect_error("(2+3");
    ok &= expect_error("9223372036854775807+1");
    if (!ok) return 1;
    puts("Calculator parser precedence, grouping, signed math, errors and overflow tests passed.");
    return 0;
}
