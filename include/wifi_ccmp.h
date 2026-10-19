#ifndef VANTAOS_WIFI_CCMP_H
#define VANTAOS_WIFI_CCMP_H

#include <stddef.h>
#include <stdint.h>

/*
 * AES-128 CCM primitive for 802.11 CCMP frames.
 * The caller constructs the 13-byte CCMP nonce and canonical AAD exactly as
 * specified by IEEE 802.11. Payload is encrypted/decrypted in place.
 * The packet-number helpers below implement allocation, wire headers and a
 * bounded replay window, but callers must still authenticate a received frame
 * before committing its PN. This is not a complete Wi-Fi connection.
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

/*
 * CCMP packet-number handling. Each TX state belongs to one installed key;
 * each RX state belongs to one peer/key/TID replay domain. Initialize TX with
 * wifi_ccmp_replay_reset() before installing a fresh key. A fresh key must
 * never inherit packet-number state from an old key.
 */
#define WIFI_CCMP_PN_MAX UINT64_C(0x0000FFFFFFFFFFFF)
#define WIFI_CCMP_HEADER_SIZE 8U
#define WIFI_CCMP_REPLAY_WINDOW 64U

typedef struct {
    uint64_t tx_next_pn;
    uint64_t rx_highest_pn;
    uint64_t rx_seen_bitmap;
    uint8_t rx_initialized;
} wifi_ccmp_replay_t;

void wifi_ccmp_replay_reset(wifi_ccmp_replay_t *state);
int wifi_ccmp_next_tx_pn(wifi_ccmp_replay_t *state, uint64_t *packet_number);
/* Call only after CCMP MIC/tag verification succeeds. */
int wifi_ccmp_accept_rx_pn(wifi_ccmp_replay_t *state, uint64_t packet_number);
int wifi_ccmp_header_encode(uint8_t header[WIFI_CCMP_HEADER_SIZE],
                            uint64_t packet_number, uint8_t key_id);
int wifi_ccmp_header_decode(const uint8_t header[WIFI_CCMP_HEADER_SIZE],
                            uint64_t *packet_number, uint8_t *key_id);
void wifi_ccmp_build_nonce(uint8_t nonce[WIFI_CCMP_NONCE_SIZE],
                           uint8_t priority, const uint8_t transmitter[6],
                           uint64_t packet_number);

/* RFC 3394 AES-128 key unwrap, used for WPA2 GTK key-data recovery. */
#define WIFI_AES128_WRAP_MAX_CIPHER 256U
int wifi_aes128_key_wrap(const uint8_t kek[WIFI_CCMP_KEY_SIZE],
                         const uint8_t *plaintext, size_t plaintext_length,
                         uint8_t *ciphertext, size_t ciphertext_capacity,
                         size_t *ciphertext_length);
int wifi_aes128_key_unwrap(const uint8_t kek[WIFI_CCMP_KEY_SIZE],
                           const uint8_t *ciphertext, size_t ciphertext_length,
                           uint8_t *plaintext, size_t plaintext_capacity,
                           size_t *plaintext_length);

#endif
