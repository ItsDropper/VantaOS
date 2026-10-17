#include "crypto_rng.h"

#include <stdint.h>

#define CPUID_RDRAND (1U << 30)
#define RDRAND_RETRY_LIMIT 10U

static void zero_bytes(void *output, size_t length)
{
    volatile uint8_t *p = (volatile uint8_t *)output;
    while (length--)
        *p++ = 0U;
}

static uint32_t cpuid_leaf1_ecx(void)
{
    uint32_t eax = 1U, ebx, ecx, edx;
    __asm__ volatile ("cpuid"
                      : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                      :
                      : "memory");
    (void)ebx;
    (void)edx;
    return ecx;
}

int crypto_rng_available(void)
{
    uint32_t maximum_leaf;
    uint32_t ebx, ecx, edx;
    __asm__ volatile ("cpuid"
                      : "=a"(maximum_leaf), "=b"(ebx), "=c"(ecx), "=d"(edx)
                      : "a"(0U)
                      : "memory");
    (void)ebx;
    (void)ecx;
    (void)edx;
    if (maximum_leaf < 1U)
        return 0;
    return (cpuid_leaf1_ecx() & CPUID_RDRAND) != 0U;
}

static int rdrand64(uint64_t *value)
{
    if (!value)
        return 0;
    for (uint32_t attempt = 0; attempt < RDRAND_RETRY_LIMIT; ++attempt) {
        uint8_t success;
        uint64_t sample;
        __asm__ volatile ("rdrand %0; setc %1"
                          : "=r"(sample), "=qm"(success)
                          :
                          : "cc");
        if (success) {
            *value = sample;
            return 1;
        }
    }
    *value = 0U;
    return 0;
}

int crypto_rng_fill(void *output, size_t length)
{
    if (length == 0U)
        return 1;
    if (!output || !crypto_rng_available())
        return 0;

    uint8_t *out = (uint8_t *)output;
    size_t offset = 0U;
    while (offset < length) {
        uint64_t sample;
        if (!rdrand64(&sample)) {
            zero_bytes(output, length);
            return 0;
        }
        size_t count = length - offset;
        if (count > sizeof(sample))
            count = sizeof(sample);
        for (size_t i = 0; i < count; ++i)
            out[offset + i] = (uint8_t)(sample >> (i * 8U));
        sample = 0U;
        offset += count;
    }
    return 1;
}
