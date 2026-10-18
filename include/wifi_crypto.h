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

#endif
