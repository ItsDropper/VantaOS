#include "wifi_ccmp.h"

#include <stddef.h>
#include <stdint.h>

static const uint8_t aes_sbox[256] = {
    0x63U, 0x7cU, 0x77U, 0x7bU, 0xf2U, 0x6bU, 0x6fU, 0xc5U, 0x30U, 0x01U, 0x67U, 0x2bU, 0xfeU, 0xd7U, 0xabU, 0x76U,
    0xcaU, 0x82U, 0xc9U, 0x7dU, 0xfaU, 0x59U, 0x47U, 0xf0U, 0xadU, 0xd4U, 0xa2U, 0xafU, 0x9cU, 0xa4U, 0x72U, 0xc0U,
    0xb7U, 0xfdU, 0x93U, 0x26U, 0x36U, 0x3fU, 0xf7U, 0xccU, 0x34U, 0xa5U, 0xe5U, 0xf1U, 0x71U, 0xd8U, 0x31U, 0x15U,
    0x04U, 0xc7U, 0x23U, 0xc3U, 0x18U, 0x96U, 0x05U, 0x9aU, 0x07U, 0x12U, 0x80U, 0xe2U, 0xebU, 0x27U, 0xb2U, 0x75U,
    0x09U, 0x83U, 0x2cU, 0x1aU, 0x1bU, 0x6eU, 0x5aU, 0xa0U, 0x52U, 0x3bU, 0xd6U, 0xb3U, 0x29U, 0xe3U, 0x2fU, 0x84U,
    0x53U, 0xd1U, 0x00U, 0xedU, 0x20U, 0xfcU, 0xb1U, 0x5bU, 0x6aU, 0xcbU, 0xbeU, 0x39U, 0x4aU, 0x4cU, 0x58U, 0xcfU,
    0xd0U, 0xefU, 0xaaU, 0xfbU, 0x43U, 0x4dU, 0x33U, 0x85U, 0x45U, 0xf9U, 0x02U, 0x7fU, 0x50U, 0x3cU, 0x9fU, 0xa8U,
    0x51U, 0xa3U, 0x40U, 0x8fU, 0x92U, 0x9dU, 0x38U, 0xf5U, 0xbcU, 0xb6U, 0xdaU, 0x21U, 0x10U, 0xffU, 0xf3U, 0xd2U,
    0xcdU, 0x0cU, 0x13U, 0xecU, 0x5fU, 0x97U, 0x44U, 0x17U, 0xc4U, 0xa7U, 0x7eU, 0x3dU, 0x64U, 0x5dU, 0x19U, 0x73U,
    0x60U, 0x81U, 0x4fU, 0xdcU, 0x22U, 0x2aU, 0x90U, 0x88U, 0x46U, 0xeeU, 0xb8U, 0x14U, 0xdeU, 0x5eU, 0x0bU, 0xdbU,
    0xe0U, 0x32U, 0x3aU, 0x0aU, 0x49U, 0x06U, 0x24U, 0x5cU, 0xc2U, 0xd3U, 0xacU, 0x62U, 0x91U, 0x95U, 0xe4U, 0x79U,
    0xe7U, 0xc8U, 0x37U, 0x6dU, 0x8dU, 0xd5U, 0x4eU, 0xa9U, 0x6cU, 0x56U, 0xf4U, 0xeaU, 0x65U, 0x7aU, 0xaeU, 0x08U,
    0xbaU, 0x78U, 0x25U, 0x2eU, 0x1cU, 0xa6U, 0xb4U, 0xc6U, 0xe8U, 0xddU, 0x74U, 0x1fU, 0x4bU, 0xbdU, 0x8bU, 0x8aU,
    0x70U, 0x3eU, 0xb5U, 0x66U, 0x48U, 0x03U, 0xf6U, 0x0eU, 0x61U, 0x35U, 0x57U, 0xb9U, 0x86U, 0xc1U, 0x1dU, 0x9eU,
    0xe1U, 0xf8U, 0x98U, 0x11U, 0x69U, 0xd9U, 0x8eU, 0x94U, 0x9bU, 0x1eU, 0x87U, 0xe9U, 0xceU, 0x55U, 0x28U, 0xdfU,
    0x8cU, 0xa1U, 0x89U, 0x0dU, 0xbfU, 0xe6U, 0x42U, 0x68U, 0x41U, 0x99U, 0x2dU, 0x0fU, 0xb0U, 0x54U, 0xbbU, 0x16U
};

static const uint8_t aes_inv_sbox[256] = {
    0x52U,0x09U,0x6aU,0xd5U,0x30U,0x36U,0xa5U,0x38U,0xbfU,0x40U,0xa3U,0x9eU,0x81U,0xf3U,0xd7U,0xfbU,
    0x7cU,0xe3U,0x39U,0x82U,0x9bU,0x2fU,0xffU,0x87U,0x34U,0x8eU,0x43U,0x44U,0xc4U,0xdeU,0xe9U,0xcbU,
    0x54U,0x7bU,0x94U,0x32U,0xa6U,0xc2U,0x23U,0x3dU,0xeeU,0x4cU,0x95U,0x0bU,0x42U,0xfaU,0xc3U,0x4eU,
    0x08U,0x2eU,0xa1U,0x66U,0x28U,0xd9U,0x24U,0xb2U,0x76U,0x5bU,0xa2U,0x49U,0x6dU,0x8bU,0xd1U,0x25U,
    0x72U,0xf8U,0xf6U,0x64U,0x86U,0x68U,0x98U,0x16U,0xd4U,0xa4U,0x5cU,0xccU,0x5dU,0x65U,0xb6U,0x92U,
    0x6cU,0x70U,0x48U,0x50U,0xfdU,0xedU,0xb9U,0xdaU,0x5eU,0x15U,0x46U,0x57U,0xa7U,0x8dU,0x9dU,0x84U,
    0x90U,0xd8U,0xabU,0x00U,0x8cU,0xbcU,0xd3U,0x0aU,0xf7U,0xe4U,0x58U,0x05U,0xb8U,0xb3U,0x45U,0x06U,
    0xd0U,0x2cU,0x1eU,0x8fU,0xcaU,0x3fU,0x0fU,0x02U,0xc1U,0xafU,0xbdU,0x03U,0x01U,0x13U,0x8aU,0x6bU,
    0x3aU,0x91U,0x11U,0x41U,0x4fU,0x67U,0xdcU,0xeaU,0x97U,0xf2U,0xcfU,0xceU,0xf0U,0xb4U,0xe6U,0x73U,
    0x96U,0xacU,0x74U,0x22U,0xe7U,0xadU,0x35U,0x85U,0xe2U,0xf9U,0x37U,0xe8U,0x1cU,0x75U,0xdfU,0x6eU,
    0x47U,0xf1U,0x1aU,0x71U,0x1dU,0x29U,0xc5U,0x89U,0x6fU,0xb7U,0x62U,0x0eU,0xaaU,0x18U,0xbeU,0x1bU,
    0xfcU,0x56U,0x3eU,0x4bU,0xc6U,0xd2U,0x79U,0x20U,0x9aU,0xdbU,0xc0U,0xfeU,0x78U,0xcdU,0x5aU,0xf4U,
    0x1fU,0xddU,0xa8U,0x33U,0x88U,0x07U,0xc7U,0x31U,0xb1U,0x12U,0x10U,0x59U,0x27U,0x80U,0xecU,0x5fU,
    0x60U,0x51U,0x7fU,0xa9U,0x19U,0xb5U,0x4aU,0x0dU,0x2dU,0xe5U,0x7aU,0x9fU,0x93U,0xc9U,0x9cU,0xefU,
    0xa0U,0xe0U,0x3bU,0x4dU,0xaeU,0x2aU,0xf5U,0xb0U,0xc8U,0xebU,0xbbU,0x3cU,0x83U,0x53U,0x99U,0x61U,
    0x17U,0x2bU,0x04U,0x7eU,0xbaU,0x77U,0xd6U,0x26U,0xe1U,0x69U,0x14U,0x63U,0x55U,0x21U,0x0cU,0x7dU
};


static void wipe_bytes(void *memory, size_t length)
{
    volatile uint8_t *p = (volatile uint8_t *)memory;
    while (length-- != 0U)
        *p++ = 0U;
}

static uint8_t xtime(uint8_t value)
{
    return (uint8_t)((value << 1) ^ ((value & 0x80U) ? 0x1BU : 0U));
}

static void aes_expand_key(const uint8_t key[16], uint8_t round_key[176])
{
    static const uint8_t rcon[10] =
        {0x01U,0x02U,0x04U,0x08U,0x10U,0x20U,0x40U,0x80U,0x1BU,0x36U};
    for (size_t i = 0U; i < 16U; ++i)
        round_key[i] = key[i];
    size_t generated = 16U;
    unsigned int round = 0U;
    uint8_t temp[4];
    while (generated < 176U) {
        for (size_t i = 0U; i < 4U; ++i)
            temp[i] = round_key[generated - 4U + i];
        if ((generated & 15U) == 0U) {
            uint8_t first = temp[0];
            temp[0] = (uint8_t)(aes_sbox[temp[1]] ^ rcon[round++]);
            temp[1] = aes_sbox[temp[2]];
            temp[2] = aes_sbox[temp[3]];
            temp[3] = aes_sbox[first];
        }
        for (size_t i = 0U; i < 4U; ++i) {
            round_key[generated] =
                (uint8_t)(round_key[generated - 16U] ^ temp[i]);
            ++generated;
        }
    }
    wipe_bytes(temp, sizeof(temp));
}

static void aes_add_round_key(uint8_t state[16], const uint8_t *key)
{
    for (size_t i = 0U; i < 16U; ++i)
        state[i] ^= key[i];
}

static void aes_sub_bytes(uint8_t state[16])
{
    for (size_t i = 0U; i < 16U; ++i)
        state[i] = aes_sbox[state[i]];
}

static void aes_shift_rows(uint8_t s[16])
{
    uint8_t t;
    t=s[1]; s[1]=s[5]; s[5]=s[9]; s[9]=s[13]; s[13]=t;
    t=s[2]; s[2]=s[10]; s[10]=t; t=s[6]; s[6]=s[14]; s[14]=t;
    t=s[15]; s[15]=s[11]; s[11]=s[7]; s[7]=s[3]; s[3]=t;
}

static void aes_mix_columns(uint8_t s[16])
{
    for (size_t i = 0U; i < 4U; ++i) {
        uint8_t *p = s + i * 4U;
        uint8_t a=p[0], b=p[1], c=p[2], d=p[3];
        uint8_t all = (uint8_t)(a ^ b ^ c ^ d);
        p[0] = (uint8_t)(a ^ all ^ xtime((uint8_t)(a ^ b)));
        p[1] = (uint8_t)(b ^ all ^ xtime((uint8_t)(b ^ c)));
        p[2] = (uint8_t)(c ^ all ^ xtime((uint8_t)(c ^ d)));
        p[3] = (uint8_t)(d ^ all ^ xtime((uint8_t)(d ^ a)));
    }
}

static void aes128_encrypt_block(const uint8_t round_key[176],
                                 const uint8_t input[16], uint8_t output[16])
{
    uint8_t state[16];
    for (size_t i = 0U; i < 16U; ++i)
        state[i] = input[i];
    aes_add_round_key(state, round_key);
    for (unsigned int round = 1U; round < 10U; ++round) {
        aes_sub_bytes(state);
        aes_shift_rows(state);
        aes_mix_columns(state);
        aes_add_round_key(state, round_key + round * 16U);
    }
    aes_sub_bytes(state);
    aes_shift_rows(state);
    aes_add_round_key(state, round_key + 160U);
    for (size_t i = 0U; i < 16U; ++i)
        output[i] = state[i];
    wipe_bytes(state, sizeof(state));
}


static uint8_t aes_multiply(uint8_t a, uint8_t b)
{
    uint8_t result = 0U;
    while (b != 0U) {
        if (b & 1U) result ^= a;
        a = xtime(a);
        b >>= 1;
    }
    return result;
}

static void aes_inverse_shift_rows(uint8_t s[16])
{
    uint8_t t;
    t=s[13]; s[13]=s[9]; s[9]=s[5]; s[5]=s[1]; s[1]=t;
    t=s[2]; s[2]=s[10]; s[10]=t; t=s[6]; s[6]=s[14]; s[14]=t;
    t=s[3]; s[3]=s[7]; s[7]=s[11]; s[11]=s[15]; s[15]=t;
}

static void aes_inverse_sub_bytes(uint8_t s[16])
{
    for (size_t i = 0U; i < 16U; ++i)
        s[i] = aes_inv_sbox[s[i]];
}

static void aes_inverse_mix_columns(uint8_t s[16])
{
    for (size_t i = 0U; i < 4U; ++i) {
        uint8_t *p = s + i * 4U;
        uint8_t a=p[0], b=p[1], c=p[2], d=p[3];
        p[0] = (uint8_t)(aes_multiply(a,14U)^aes_multiply(b,11U)^
                         aes_multiply(c,13U)^aes_multiply(d,9U));
        p[1] = (uint8_t)(aes_multiply(a,9U)^aes_multiply(b,14U)^
                         aes_multiply(c,11U)^aes_multiply(d,13U));
        p[2] = (uint8_t)(aes_multiply(a,13U)^aes_multiply(b,9U)^
                         aes_multiply(c,14U)^aes_multiply(d,11U));
        p[3] = (uint8_t)(aes_multiply(a,11U)^aes_multiply(b,13U)^
                         aes_multiply(c,9U)^aes_multiply(d,14U));
    }
}

static void aes128_decrypt_block(const uint8_t round_key[176],
                                 const uint8_t input[16], uint8_t output[16])
{
    uint8_t state[16];
    for (size_t i = 0U; i < 16U; ++i) state[i] = input[i];
    aes_add_round_key(state, round_key + 160U);
    for (unsigned int round = 9U; round > 0U; --round) {
        aes_inverse_shift_rows(state);
        aes_inverse_sub_bytes(state);
        aes_add_round_key(state, round_key + round * 16U);
        aes_inverse_mix_columns(state);
    }
    aes_inverse_shift_rows(state);
    aes_inverse_sub_bytes(state);
    aes_add_round_key(state, round_key);
    for (size_t i = 0U; i < 16U; ++i) output[i] = state[i];
    wipe_bytes(state, sizeof(state));
}

/*
 * RFC 3394 section 2.2.3.2. The integrity register is checked in constant
 * time; output is erased on every failure so unauthenticated key data cannot
 * accidentally be consumed by a caller.
 */

int wifi_aes128_key_wrap(const uint8_t kek[WIFI_CCMP_KEY_SIZE],
                         const uint8_t *plaintext, size_t plaintext_length,
                         uint8_t *ciphertext, size_t ciphertext_capacity,
                         size_t *ciphertext_length)
{
    uint8_t round_key[176], a[8], block[16], encrypted[16];
    size_t n;
    if (ciphertext_length) *ciphertext_length = 0U;
    if (!kek || !plaintext || !ciphertext || !ciphertext_length ||
        plaintext_length < 16U || plaintext_length > WIFI_AES128_WRAP_MAX_CIPHER - 8U ||
        (plaintext_length & 7U) != 0U ||
        ciphertext_capacity < plaintext_length + 8U)
        return 0;

    n = plaintext_length / 8U;
    for (size_t i = 0U; i < 8U; ++i) a[i] = 0xA6U;
    for (size_t i = 0U; i < plaintext_length; ++i) ciphertext[8U + i] = plaintext[i];
    aes_expand_key(kek, round_key);

    for (uint32_t j = 0U; j < 6U; ++j) {
        for (size_t i = 1U; i <= n; ++i) {
            for (size_t k = 0U; k < 8U; ++k)
                block[k] = a[k];
            for (size_t k = 0U; k < 8U; ++k)
                block[8U + k] = ciphertext[8U + (i - 1U) * 8U + k];
            aes128_encrypt_block(round_key, block, encrypted);
            uint64_t t = (uint64_t)n * (uint64_t)j + (uint64_t)i;
            for (size_t k = 0U; k < 8U; ++k)
                a[k] = (uint8_t)(encrypted[k] ^
                                  (uint8_t)(t >> (56U - 8U*k)));
            for (size_t k = 0U; k < 8U; ++k)
                ciphertext[8U + (i - 1U) * 8U + k] = encrypted[8U + k];
        }
    }

    for (size_t i = 0U; i < 8U; ++i) ciphertext[i] = a[i];
    *ciphertext_length = plaintext_length + 8U;
    wipe_bytes(round_key, sizeof(round_key));
    wipe_bytes(a, sizeof(a));
    wipe_bytes(block, sizeof(block));
    wipe_bytes(encrypted, sizeof(encrypted));
    return 1;
}

int wifi_aes128_key_unwrap(const uint8_t kek[WIFI_CCMP_KEY_SIZE],
                           const uint8_t *ciphertext, size_t ciphertext_length,
                           uint8_t *plaintext, size_t plaintext_capacity,
                           size_t *plaintext_length)
{
    uint8_t round_key[176], a[8], block[16], decrypted[16];
    size_t key_length, n;
    uint8_t difference = 0U;

    if (plaintext_length) *plaintext_length = 0U;
    if (!kek || !ciphertext || !plaintext || !plaintext_length ||
        ciphertext_length < 24U ||
        ciphertext_length > WIFI_AES128_WRAP_MAX_CIPHER ||
        (ciphertext_length & 7U) != 0U)
        return 0;
    key_length = ciphertext_length - 8U;
    if (plaintext_capacity < key_length)
        return 0;

    n = key_length / 8U;
    for (size_t i = 0U; i < 8U; ++i) a[i] = ciphertext[i];
    for (size_t i = 0U; i < key_length; ++i) plaintext[i] = ciphertext[8U + i];
    aes_expand_key(kek, round_key);

    for (int j = 5; j >= 0; --j) {
        for (size_t i = n; i > 0U; --i) {
            uint64_t t = (uint64_t)n * (uint64_t)j + (uint64_t)i;
            for (size_t k = 0U; k < 8U; ++k)
                block[k] = (uint8_t)(a[k] ^ (uint8_t)(t >> (56U - 8U*k)));
            for (size_t k = 0U; k < 8U; ++k)
                block[8U + k] = plaintext[(i - 1U) * 8U + k];
            aes128_decrypt_block(round_key, block, decrypted);
            for (size_t k = 0U; k < 8U; ++k) a[k] = decrypted[k];
            for (size_t k = 0U; k < 8U; ++k)
                plaintext[(i - 1U) * 8U + k] = decrypted[8U + k];
        }
    }

    for (size_t i = 0U; i < 8U; ++i) difference |= (uint8_t)(a[i] ^ 0xA6U);
    wipe_bytes(round_key, sizeof(round_key));
    wipe_bytes(a, sizeof(a));
    wipe_bytes(block, sizeof(block));
    wipe_bytes(decrypted, sizeof(decrypted));
    if (difference != 0U) {
        wipe_bytes(plaintext, key_length);
        return 0;
    }
    *plaintext_length = key_length;
    return 1;
}

static void ccm_mac_block(const uint8_t round_key[176], uint8_t mac[16],
                          const uint8_t block[16])
{
    uint8_t input[16];
    for (size_t i = 0U; i < 16U; ++i)
        input[i] = (uint8_t)(mac[i] ^ block[i]);
    aes128_encrypt_block(round_key, input, mac);
    wipe_bytes(input, sizeof(input));
}

static void ccm_make_counter(uint8_t block[16], const uint8_t nonce[13],
                             uint16_t counter)
{
    block[0] = 0x01U; /* L - 1, with a 13-byte nonce and two-byte length. */
    for (size_t i = 0U; i < 13U; ++i)
        block[1U + i] = nonce[i];
    block[14] = (uint8_t)(counter >> 8);
    block[15] = (uint8_t)counter;
}

static int ccm_compute_tag(const uint8_t round_key[176],
                           const uint8_t nonce[13],
                           const uint8_t *aad, size_t aad_length,
                           const uint8_t *payload, size_t payload_length,
                           uint8_t tag[8])
{
    uint8_t mac[16] = {0};
    uint8_t block[16] = {0};
    uint8_t counter_block[16];
    uint8_t s0[16];

    /*
     * CCM B0: Adata | M' | L', followed by the nonce and message length.
     * This implementation uses a 13-byte nonce, an 8-byte tag, and L=2.
     */
    block[0] = (uint8_t)((aad_length != 0U ? 0x40U : 0U) | 0x18U | 0x01U);
    for (size_t i = 0U; i < 13U; ++i)
        block[1U + i] = nonce[i];
    block[14] = (uint8_t)(payload_length >> 8);
    block[15] = (uint8_t)payload_length;
    ccm_mac_block(round_key, mac, block);

    /*
     * AAD is encoded as a two-byte big-endian length followed by AAD bytes,
     * then zero-padded to a block boundary. Process each encoded block only
     * after clearing it, so padding is explicit and independent of buffers.
     */
    if (aad_length != 0U) {
        size_t encoded_length = aad_length + 2U;
        size_t consumed = 0U;
        while (consumed < encoded_length) {
            for (size_t i = 0U; i < sizeof(block); ++i)
                block[i] = 0U;
            for (size_t i = 0U; i < sizeof(block) && consumed < encoded_length;
                 ++i, ++consumed) {
                if (consumed == 0U)
                    block[i] = (uint8_t)(aad_length >> 8);
                else if (consumed == 1U)
                    block[i] = (uint8_t)aad_length;
                else
                    block[i] = aad[consumed - 2U];
            }
            ccm_mac_block(round_key, mac, block);
        }
    }

    /* CBC-MAC always authenticates the original plaintext, not ciphertext. */
    for (size_t offset = 0U; offset < payload_length; offset += 16U) {
        for (size_t i = 0U; i < sizeof(block); ++i)
            block[i] = 0U;
        size_t amount = payload_length - offset;
        if (amount > sizeof(block))
            amount = sizeof(block);
        for (size_t i = 0U; i < amount; ++i)
            block[i] = payload[offset + i];
        ccm_mac_block(round_key, mac, block);
    }

    ccm_make_counter(counter_block, nonce, 0U);
    aes128_encrypt_block(round_key, counter_block, s0);
    for (size_t i = 0U; i < WIFI_CCMP_TAG_SIZE; ++i)
        tag[i] = (uint8_t)(mac[i] ^ s0[i]);

    wipe_bytes(mac, sizeof(mac));
    wipe_bytes(block, sizeof(block));
    wipe_bytes(counter_block, sizeof(counter_block));
    wipe_bytes(s0, sizeof(s0));
    return 1;
}

static void ccm_crypt(const uint8_t round_key[176],
                      const uint8_t nonce[13], uint8_t *payload,
                      size_t payload_length)
{
    uint8_t counter_block[16];
    uint8_t stream[16];
    uint16_t counter = 1U;
    for (size_t offset = 0U; offset < payload_length; offset += 16U) {
        ccm_make_counter(counter_block, nonce, counter++);
        aes128_encrypt_block(round_key, counter_block, stream);
        size_t amount = payload_length - offset;
        if (amount > 16U)
            amount = 16U;
        for (size_t i = 0U; i < amount; ++i)
            payload[offset + i] ^= stream[i];
    }
    wipe_bytes(counter_block, sizeof(counter_block));
    wipe_bytes(stream, sizeof(stream));
}

static int ccm_validate(const uint8_t *key, const uint8_t *nonce,
                        const uint8_t *aad, size_t aad_length,
                        const uint8_t *payload, size_t payload_length,
                        const uint8_t *tag)
{
    return key && nonce && (aad || aad_length == 0U) &&
           (payload || payload_length == 0U) && tag &&
           aad_length <= WIFI_CCMP_MAX_AAD &&
           payload_length <= WIFI_CCMP_MAX_PAYLOAD &&
           payload_length <= 65535U;
}

int wifi_ccmp_encrypt(const uint8_t key[16], const uint8_t nonce[13],
                      const uint8_t *aad, size_t aad_length,
                      uint8_t *payload, size_t payload_length,
                      uint8_t tag[8])
{
    uint8_t round_key[176];
    if (!ccm_validate(key, nonce, aad, aad_length, payload, payload_length, tag))
        return 0;
    aes_expand_key(key, round_key);
    if (!ccm_compute_tag(round_key, nonce, aad, aad_length,
                         payload, payload_length, tag)) {
        wipe_bytes(round_key, sizeof(round_key));
        return 0;
    }
    ccm_crypt(round_key, nonce, payload, payload_length);
    wipe_bytes(round_key, sizeof(round_key));
    return 1;
}

int wifi_ccmp_decrypt(const uint8_t key[16], const uint8_t nonce[13],
                      const uint8_t *aad, size_t aad_length,
                      uint8_t *payload, size_t payload_length,
                      const uint8_t tag[8])
{
    uint8_t round_key[176];
    uint8_t computed[8];
    uint8_t difference = 0U;
    if (!ccm_validate(key, nonce, aad, aad_length, payload, payload_length, tag))
        return 0;
    aes_expand_key(key, round_key);
    ccm_crypt(round_key, nonce, payload, payload_length);
    if (!ccm_compute_tag(round_key, nonce, aad, aad_length,
                         payload, payload_length, computed)) {
        wipe_bytes(payload, payload_length);
        wipe_bytes(round_key, sizeof(round_key));
        return 0;
    }
    for (size_t i = 0U; i < sizeof(computed); ++i)
        difference |= (uint8_t)(computed[i] ^ tag[i]);
    if (difference != 0U)
        wipe_bytes(payload, payload_length);
    wipe_bytes(computed, sizeof(computed));
    wipe_bytes(round_key, sizeof(round_key));
    return difference == 0U;
}

/*
 * CCMP packet-number lifecycle and wire-format helpers. These are deliberately
 * separate from AES-CCM: callers must still build canonical 802.11 AAD, verify
 * the tag, and scope replay state to the installed key and traffic identifier.
 */
void wifi_ccmp_replay_reset(wifi_ccmp_replay_t *state)
{
    if (!state) return;
    state->tx_next_pn = 1U;
    state->rx_highest_pn = 0U;
    state->rx_seen_bitmap = 0U;
    state->rx_initialized = 0U;
}

int wifi_ccmp_next_tx_pn(wifi_ccmp_replay_t *state, uint64_t *packet_number)
{
    if (packet_number) *packet_number = 0U;
    if (!state || !packet_number || state->tx_next_pn == 0U ||
        state->tx_next_pn > WIFI_CCMP_PN_MAX)
        return 0;
    *packet_number = state->tx_next_pn;
    if (state->tx_next_pn == WIFI_CCMP_PN_MAX)
        state->tx_next_pn = 0U; /* Exhausted: never wrap and reuse a nonce. */
    else
        ++state->tx_next_pn;
    return 1;
}

int wifi_ccmp_accept_rx_pn(wifi_ccmp_replay_t *state, uint64_t pn)
{
    uint64_t distance;
    if (!state || pn == 0U || pn > WIFI_CCMP_PN_MAX) return 0;
    if (!state->rx_initialized) {
        state->rx_highest_pn = pn;
        state->rx_seen_bitmap = 1U;
        state->rx_initialized = 1U;
        return 1;
    }
    if (pn > state->rx_highest_pn) {
        distance = pn - state->rx_highest_pn;
        state->rx_seen_bitmap = distance >= WIFI_CCMP_REPLAY_WINDOW
            ? 1U : (state->rx_seen_bitmap << distance) | 1U;
        state->rx_highest_pn = pn;
        return 1;
    }
    distance = state->rx_highest_pn - pn;
    if (distance >= WIFI_CCMP_REPLAY_WINDOW ||
        (state->rx_seen_bitmap & (UINT64_C(1) << distance)) != 0U)
        return 0;
    state->rx_seen_bitmap |= UINT64_C(1) << distance;
    return 1;
}

int wifi_ccmp_header_encode(uint8_t header[WIFI_CCMP_HEADER_SIZE],
                            uint64_t pn, uint8_t key_id)
{
    if (!header || pn == 0U || pn > WIFI_CCMP_PN_MAX || key_id > 3U)
        return 0;
    header[0] = (uint8_t)pn;
    header[1] = (uint8_t)(pn >> 8);
    header[2] = 0U;
    header[3] = (uint8_t)(0x20U | (uint8_t)(key_id << 6));
    header[4] = (uint8_t)(pn >> 16);
    header[5] = (uint8_t)(pn >> 24);
    header[6] = (uint8_t)(pn >> 32);
    header[7] = (uint8_t)(pn >> 40);
    return 1;
}

int wifi_ccmp_header_decode(const uint8_t header[WIFI_CCMP_HEADER_SIZE],
                            uint64_t *pn, uint8_t *key_id)
{
    uint64_t value;
    if (pn) *pn = 0U;
    if (key_id) *key_id = 0U;
    if (!header || !pn || !key_id || header[2] != 0U ||
        (header[3] & 0x3fU) != 0x20U)
        return 0;
    value = (uint64_t)header[0] |
            ((uint64_t)header[1] << 8) |
            ((uint64_t)header[4] << 16) |
            ((uint64_t)header[5] << 24) |
            ((uint64_t)header[6] << 32) |
            ((uint64_t)header[7] << 40);
    if (value == 0U) return 0;
    *pn = value;
    *key_id = (uint8_t)(header[3] >> 6);
    return 1;
}

void wifi_ccmp_build_nonce(uint8_t nonce[WIFI_CCMP_NONCE_SIZE],
                           uint8_t priority, const uint8_t transmitter[6],
                           uint64_t pn)
{
    if (!nonce) return;
    if (!transmitter || pn == 0U || pn > WIFI_CCMP_PN_MAX) {
        for (size_t i = 0U; i < WIFI_CCMP_NONCE_SIZE; ++i) nonce[i] = 0U;
        return;
    }
    nonce[0] = priority;
    for (size_t i = 0U; i < 6U; ++i) nonce[1U + i] = transmitter[i];
    nonce[7] = (uint8_t)(pn >> 40);
    nonce[8] = (uint8_t)(pn >> 32);
    nonce[9] = (uint8_t)(pn >> 24);
    nonce[10] = (uint8_t)(pn >> 16);
    nonce[11] = (uint8_t)(pn >> 8);
    nonce[12] = (uint8_t)pn;
}
