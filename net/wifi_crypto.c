#include "wifi_crypto.h"

#include <stddef.h>
#include <stdint.h>

#define SHA1_BLOCK_SIZE 64U
#define SHA1_DIGEST_SIZE 20U
#define WIFI_PBKDF2_MAX_PASSWORD 63U
#define WIFI_PBKDF2_MAX_SALT 32U
#define WIFI_PBKDF2_MAX_OUTPUT 64U
#define WIFI_PBKDF2_MAX_ITERATIONS 4096U

typedef struct {
    uint32_t state[5];
    uint64_t total_bytes;
    uint8_t block[SHA1_BLOCK_SIZE];
    size_t used;
} sha1_context_t;

static uint32_t rol32(uint32_t value, unsigned int count)
{
    return (value << count) | (value >> (32U - count));
}

static void store_be32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static uint32_t load_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void wipe(void *memory, size_t length)
{
    volatile uint8_t *p = (volatile uint8_t *)memory;
    while (length-- != 0U)
        *p++ = 0U;
}

static void sha1_transform(sha1_context_t *ctx, const uint8_t block[64])
{
    uint32_t w[80];
    uint32_t a, b, c, d, e;

    for (unsigned int i = 0U; i < 16U; ++i)
        w[i] = load_be32(block + i * 4U);
    for (unsigned int i = 16U; i < 80U; ++i)
        w[i] = rol32(w[i - 3U] ^ w[i - 8U] ^ w[i - 14U] ^ w[i - 16U], 1U);

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];

    for (unsigned int i = 0U; i < 80U; ++i) {
        uint32_t f, k, temp;
        if (i < 20U) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999U;
        } else if (i < 40U) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1U;
        } else if (i < 60U) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCU;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6U;
        }
        temp = rol32(a, 5U) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol32(b, 30U);
        b = a;
        a = temp;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    wipe(w, sizeof(w));
}

static void sha1_init(sha1_context_t *ctx)
{
    ctx->state[0] = 0x67452301U;
    ctx->state[1] = 0xEFCDAB89U;
    ctx->state[2] = 0x98BADCFEU;
    ctx->state[3] = 0x10325476U;
    ctx->state[4] = 0xC3D2E1F0U;
    ctx->total_bytes = 0U;
    ctx->used = 0U;
}

static void sha1_update(sha1_context_t *ctx, const uint8_t *data, size_t length)
{
    if (length == 0U)
        return;
    ctx->total_bytes += length;
    while (length != 0U) {
        size_t available = SHA1_BLOCK_SIZE - ctx->used;
        size_t amount = length < available ? length : available;
        for (size_t i = 0U; i < amount; ++i)
            ctx->block[ctx->used + i] = data[i];
        ctx->used += amount;
        data += amount;
        length -= amount;
        if (ctx->used == SHA1_BLOCK_SIZE) {
            sha1_transform(ctx, ctx->block);
            ctx->used = 0U;
        }
    }
}

static void sha1_final(sha1_context_t *ctx, uint8_t digest[20])
{
    uint64_t bit_length = ctx->total_bytes * 8U;
    ctx->block[ctx->used++] = 0x80U;
    if (ctx->used > 56U) {
        while (ctx->used < SHA1_BLOCK_SIZE)
            ctx->block[ctx->used++] = 0U;
        sha1_transform(ctx, ctx->block);
        ctx->used = 0U;
    }
    while (ctx->used < 56U)
        ctx->block[ctx->used++] = 0U;
    for (unsigned int i = 0U; i < 8U; ++i)
        ctx->block[56U + i] = (uint8_t)(bit_length >> (56U - 8U * i));
    sha1_transform(ctx, ctx->block);
    for (unsigned int i = 0U; i < 5U; ++i)
        store_be32(digest + i * 4U, ctx->state[i]);
    wipe(ctx, sizeof(*ctx));
}

static void sha1_hash(const uint8_t *data, size_t length, uint8_t digest[20])
{
    sha1_context_t ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, data, length);
    sha1_final(&ctx, digest);
}

static void hmac_sha1(const uint8_t *key, size_t key_length,
                      const uint8_t *data, size_t data_length,
                      uint8_t digest[20])
{
    uint8_t normalized[SHA1_BLOCK_SIZE] = {0};
    uint8_t inner_pad[SHA1_BLOCK_SIZE];
    uint8_t outer_pad[SHA1_BLOCK_SIZE];
    uint8_t inner_digest[SHA1_DIGEST_SIZE];
    sha1_context_t ctx;

    if (key_length > SHA1_BLOCK_SIZE) {
        sha1_hash(key, key_length, normalized);
    } else {
        for (size_t i = 0U; i < key_length; ++i)
            normalized[i] = key[i];
    }
    for (size_t i = 0U; i < SHA1_BLOCK_SIZE; ++i) {
        inner_pad[i] = (uint8_t)(normalized[i] ^ 0x36U);
        outer_pad[i] = (uint8_t)(normalized[i] ^ 0x5CU);
    }

    sha1_init(&ctx);
    sha1_update(&ctx, inner_pad, sizeof(inner_pad));
    sha1_update(&ctx, data, data_length);
    sha1_final(&ctx, inner_digest);

    sha1_init(&ctx);
    sha1_update(&ctx, outer_pad, sizeof(outer_pad));
    sha1_update(&ctx, inner_digest, sizeof(inner_digest));
    sha1_final(&ctx, digest);

    wipe(normalized, sizeof(normalized));
    wipe(inner_pad, sizeof(inner_pad));
    wipe(outer_pad, sizeof(outer_pad));
    wipe(inner_digest, sizeof(inner_digest));
}


int wifi_hmac_sha1(const uint8_t *key, size_t key_length,
                   const uint8_t *data, size_t data_length,
                   uint8_t digest[SHA1_DIGEST_SIZE])
{
    if (!key || key_length == 0U || (!data && data_length != 0U) || !digest)
        return 0;
    hmac_sha1(key, key_length, data, data_length, digest);
    return 1;
}

/*
 * IEEE 802.11i PRF-512 used for the WPA2 pairwise transient key. The context
 * sorts both MAC addresses and both nonces lexicographically as required by
 * the protocol; callers must pass the actual authenticator/supplicant values.
 */
int wifi_wpa2_prf512(const uint8_t pmk[WIFI_PMK_SIZE],
                     const uint8_t authenticator[6],
                     const uint8_t supplicant[6],
                     const uint8_t authenticator_nonce[32],
                     const uint8_t supplicant_nonce[32],
                     uint8_t ptk[64])
{
    static const uint8_t label[] = "Pairwise key expansion";
    uint8_t context[sizeof(label) + 12U + 64U + 1U];
    uint8_t digest[SHA1_DIGEST_SIZE];
    const uint8_t *mac_low, *mac_high, *nonce_low, *nonce_high;
    size_t offset = 0U;

    if (!pmk || !authenticator || !supplicant || !authenticator_nonce ||
        !supplicant_nonce || !ptk)
        return 0;

    int mac_order = 0;
    for (size_t i = 0U; i < 6U; ++i) {
        if (authenticator[i] != supplicant[i]) {
            mac_order = authenticator[i] < supplicant[i] ? -1 : 1;
            break;
        }
    }
    mac_low = mac_order <= 0 ? authenticator : supplicant;
    mac_high = mac_order <= 0 ? supplicant : authenticator;

    int nonce_order = 0;
    for (size_t i = 0U; i < 32U; ++i) {
        if (authenticator_nonce[i] != supplicant_nonce[i]) {
            nonce_order = authenticator_nonce[i] < supplicant_nonce[i] ? -1 : 1;
            break;
        }
    }
    nonce_low = nonce_order <= 0 ? authenticator_nonce : supplicant_nonce;
    nonce_high = nonce_order <= 0 ? supplicant_nonce : authenticator_nonce;

    for (size_t i = 0U; i < sizeof(label); ++i)
        context[offset++] = label[i]; /* Includes the required NUL separator. */
    for (size_t i = 0U; i < 6U; ++i) context[offset++] = mac_low[i];
    for (size_t i = 0U; i < 6U; ++i) context[offset++] = mac_high[i];
    for (size_t i = 0U; i < 32U; ++i) context[offset++] = nonce_low[i];
    for (size_t i = 0U; i < 32U; ++i) context[offset++] = nonce_high[i];

    size_t produced = 0U;
    for (uint8_t counter = 0U; produced < 64U; ++counter) {
        context[offset] = counter;
        hmac_sha1(pmk, WIFI_PMK_SIZE, context, offset + 1U, digest);
        size_t amount = 64U - produced;
        if (amount > sizeof(digest)) amount = sizeof(digest);
        for (size_t i = 0U; i < amount; ++i)
            ptk[produced + i] = digest[i];
        produced += amount;
        if (counter == 3U && produced < 64U) {
            wipe(context, sizeof(context));
            wipe(digest, sizeof(digest));
            return 0;
        }
    }
    wipe(context, sizeof(context));
    wipe(digest, sizeof(digest));
    return 1;
}

int wifi_pbkdf2_sha1(const uint8_t *password, size_t password_length,
                     const uint8_t *salt, size_t salt_length,
                     uint32_t iterations, uint8_t *output,
                     size_t output_length)
{
    uint8_t salt_block[WIFI_PBKDF2_MAX_SALT + 4U];
    uint8_t u[SHA1_DIGEST_SIZE];
    uint8_t accumulator[SHA1_DIGEST_SIZE];
    uint32_t block_index = 1U;
    size_t written = 0U;

    if (!password || password_length == 0U ||
        password_length > WIFI_PBKDF2_MAX_PASSWORD ||
        !salt || salt_length == 0U || salt_length > WIFI_PBKDF2_MAX_SALT ||
        !output || output_length == 0U ||
        output_length > WIFI_PBKDF2_MAX_OUTPUT ||
        iterations == 0U || iterations > WIFI_PBKDF2_MAX_ITERATIONS)
        return 0;

    for (size_t i = 0U; i < salt_length; ++i)
        salt_block[i] = salt[i];

    while (written < output_length) {
        size_t amount = output_length - written;
        if (amount > SHA1_DIGEST_SIZE)
            amount = SHA1_DIGEST_SIZE;
        salt_block[salt_length] = (uint8_t)(block_index >> 24);
        salt_block[salt_length + 1U] = (uint8_t)(block_index >> 16);
        salt_block[salt_length + 2U] = (uint8_t)(block_index >> 8);
        salt_block[salt_length + 3U] = (uint8_t)block_index;

        hmac_sha1(password, password_length, salt_block, salt_length + 4U, u);
        for (size_t i = 0U; i < SHA1_DIGEST_SIZE; ++i)
            accumulator[i] = u[i];

        for (uint32_t iteration = 1U; iteration < iterations; ++iteration) {
            hmac_sha1(password, password_length, u, sizeof(u), u);
            for (size_t i = 0U; i < SHA1_DIGEST_SIZE; ++i)
                accumulator[i] ^= u[i];
        }

        for (size_t i = 0U; i < amount; ++i)
            output[written + i] = accumulator[i];
        written += amount;
        ++block_index;
    }

    wipe(salt_block, sizeof(salt_block));
    wipe(u, sizeof(u));
    wipe(accumulator, sizeof(accumulator));
    return 1;
}
