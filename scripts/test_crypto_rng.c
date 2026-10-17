#include "crypto_rng.h"

#include <stdint.h>
#include <stdio.h>

int main(void)
{
    uint8_t bytes[32];
    for (size_t i = 0; i < sizeof(bytes); ++i)
        bytes[i] = 0xA5U;

    if (crypto_rng_fill(0, sizeof(bytes)) != 0)
        return 1;
    if (crypto_rng_fill(bytes, 0U) != 1)
        return 2;

    if (!crypto_rng_available()) {
        if (crypto_rng_fill(bytes, sizeof(bytes)) != 0)
            return 3;
        for (size_t i = 0; i < sizeof(bytes); ++i)
            if (bytes[i] != 0U)
                return 4;
        puts("crypto RNG correctly fails closed without RDRAND.");
        return 0;
    }

    if (crypto_rng_fill(bytes, sizeof(bytes)) != 1)
        return 5;

    uint8_t any = 0U;
    for (size_t i = 0; i < sizeof(bytes); ++i)
        any |= bytes[i];
    if (!any)
        return 6;

    uint8_t second[32];
    if (crypto_rng_fill(second, sizeof(second)) != 1)
        return 7;
    uint8_t different = 0U;
    for (size_t i = 0; i < sizeof(bytes); ++i)
        different |= (uint8_t)(bytes[i] ^ second[i]);
    if (!different)
        return 8;

    puts("crypto RNG hardware capability and fill tests passed.");
    return 0;
}
