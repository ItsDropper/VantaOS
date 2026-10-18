#ifndef VANTAOS_WIFI_CCMP_H
#define VANTAOS_WIFI_CCMP_H

#include <stddef.h>
#include <stdint.h>

/*
 * AES-128 CCM primitive for 802.11 CCMP frames.
 * The caller constructs the 13-byte CCMP nonce and canonical AAD exactly as
 * specified by IEEE 802.11. Payload is encrypted/decrypted in place.
 * This primitive alone does not implement PN replay protection, key install,
 * EAPOL, association, or a complete authenticated Wi-Fi connection.
 */
#define WIFI_CCMP_KEY_SIZE 16U
#define WIFI_CCMP_NONCE_SIZE 13U
#define WIFI_CCMP_TAG_SIZE 8U
#define WIFI_CCMP_MAX_PAYLOAD 2304U
#define WIFI_CCMP_MAX_AAD 64U

int wifi_ccmp_encrypt(const uint8_t key[WIFI_CCMP_KEY_SIZE],
                      const uint8_t nonce[WIFI_CCMP_NONCE_SIZE],
                      const uint8_t *aad, size_t aad_length,
                      uint8_t *payload, size_t payload_length,
                      uint8_t tag[WIFI_CCMP_TAG_SIZE]);
int wifi_ccmp_decrypt(const uint8_t key[WIFI_CCMP_KEY_SIZE],
                      const uint8_t nonce[WIFI_CCMP_NONCE_SIZE],
                      const uint8_t *aad, size_t aad_length,
                      uint8_t *payload, size_t payload_length,
                      const uint8_t tag[WIFI_CCMP_TAG_SIZE]);

/* RFC 3394 AES-128 key unwrap, used for WPA2 GTK key-data recovery. */
#define WIFI_AES128_WRAP_MAX_CIPHER 256U
int wifi_aes128_key_unwrap(const uint8_t kek[WIFI_CCMP_KEY_SIZE],
                           const uint8_t *ciphertext, size_t ciphertext_length,
                           uint8_t *plaintext, size_t plaintext_capacity,
                           size_t *plaintext_length);

#endif
