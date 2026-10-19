/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef VANTAOS_WIFI_CRYPTO_H
#define VANTAOS_WIFI_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

/*
 * WPA2-Personal key derivation primitive.
 *
 * This implements PBKDF2-HMAC-SHA1 for deriving a 256-bit PMK from a
 * passphrase and SSID. It is not a complete WPA2 implementation and must
 * not be treated as a substitute for a reviewed cryptographic library.
 * Inputs and output are bounded; invalid inputs fail without writing output.
 */
#define WIFI_PMK_SIZE 32U
#define WIFI_PBKDF2_SHA1_ITERATIONS 4096U
int wifi_pbkdf2_sha1(const uint8_t *password, size_t password_length,
                     const uint8_t *salt, size_t salt_length,
                     uint32_t iterations, uint8_t *output,
                     size_t output_length);

/* WPA2 supplicant primitives. Keep inputs and outputs fixed-size and bounded. */
int wifi_hmac_sha1(const uint8_t *key, size_t key_length,
                   const uint8_t *data, size_t data_length,
                   uint8_t digest[20]);
int wifi_wpa2_prf512(const uint8_t pmk[WIFI_PMK_SIZE],
                     const uint8_t authenticator[6],
                     const uint8_t supplicant[6],
                     const uint8_t authenticator_nonce[32],
                     const uint8_t supplicant_nonce[32],
                     uint8_t ptk[64]);

#endif
