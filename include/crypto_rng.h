/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef VANTAOS_CRYPTO_RNG_H
#define VANTAOS_CRYPTO_RNG_H

#include <stddef.h>
#include <stdint.h>

/*
 * Fail-closed hardware entropy source for cryptographic consumers.
 * Returns 1 only when the CPU advertises RDRAND and every requested word was
 * obtained successfully. There is deliberately no timer/PID/PRNG fallback.
 */
int crypto_rng_available(void);
int crypto_rng_fill(void *output, size_t length);

#endif
