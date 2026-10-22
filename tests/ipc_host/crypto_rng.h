#ifndef VANTAOS_IPC_HOST_CRYPTO_RNG_H
#define VANTAOS_IPC_HOST_CRYPTO_RNG_H
#include <stddef.h>
#include <stdint.h>
int crypto_rng_fill(void *output, size_t length);
#endif
