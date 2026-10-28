/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vanta/ipc.h>
#include "../kernel/ipc.c"

static uint64_t rng_counter = 0x123456789ULL;
static int rng_should_fail;
int crypto_rng_fill(void *output, size_t length)
{
    uint8_t *bytes = (uint8_t *)output;
    if (rng_should_fail) return 0;
    uint64_t value = ++rng_counter;
    if (length > sizeof(value)) return 0;
    for (size_t i=0;i<length;i++) bytes[i]=(uint8_t)(value>>(i*8U));
    return 1;
}

static void test_endpoint_exhaustion_cleanup_and_rng_failure(void)
{
    int64_t tokens[32];
    char payload[] = "private";
    char output[IPC_MESSAGE_MAX] = {0};

    rng_should_fail = 1;
    assert(ipc_endpoint_create(500U) == -19);
    rng_should_fail = 0;

    for (uint32_t i = 0U; i < 32U; ++i) {
        tokens[i] = ipc_endpoint_create(1000U + i);
        assert(tokens[i] > 0);
    }
    assert(ipc_endpoint_create(2000U) == -12);

    assert(ipc_message_send((uint64_t)tokens[5], 9000U,
                            payload, sizeof(payload)) == 0);
    uint32_t slot = 0U;
    while (slot < 32U && (!endpoints[slot].active ||
           endpoints[slot].token != (uint64_t)tokens[5]))
        ++slot;
    assert(slot < 32U);
    ipc_process_cleanup(1005U);
    assert(!endpoints[slot].active);
    assert(endpoints[slot].token == 0U && endpoints[slot].count == 0U);
    assert(endpoints[slot].queue[0].length == 0U);
    for (size_t i = 0U; i < IPC_MESSAGE_MAX; ++i)
        assert(endpoints[slot].queue[0].bytes[i] == 0U);
    assert(ipc_message_receive((uint64_t)tokens[5], 1005U,
                               output, sizeof(output)) == -9);

    int64_t replacement = ipc_endpoint_create(3000U);
    assert(replacement > 0);
    assert(ipc_endpoint_close((uint64_t)replacement, 3000U) == 0);
    for (uint32_t i = 0U; i < 32U; ++i) {
        if (i != 5U)
            assert(ipc_endpoint_close((uint64_t)tokens[i], 1000U + i) == 0);
    }
}

int main(void)
{
    vanta_ipc_message_t abi_message;
    const char abi_payload[] = "hello";
    assert(vanta_ipc_message_init(&abi_message, 1U, 42U,
                                  abi_payload, sizeof(abi_payload)) == 0);
    assert(sizeof(abi_message) == VANTA_IPC_MESSAGE_MAX);
    assert(vanta_ipc_message_validate(&abi_message,
           VANTA_IPC_HEADER_SIZE + sizeof(abi_payload)) == 0);
    assert(abi_message.magic == VANTA_IPC_MAGIC);
    assert(abi_message.version == VANTA_IPC_VERSION);
    assert(abi_message.type == 1U && abi_message.request_id == 42U);
    assert(abi_message.payload_length == sizeof(abi_payload));
    assert(memcmp(abi_message.payload, abi_payload, sizeof(abi_payload)) == 0);
    assert(vanta_ipc_message_init(&abi_message, 0U, 0U, 0, 0U) == -VANTA_EINVAL);
    assert(vanta_ipc_message_init(&abi_message, 1U, 0U, abi_payload,
                                  VANTA_IPC_PAYLOAD_MAX + 1U) == -VANTA_EMSGSIZE);
    assert(vanta_ipc_message_init(&abi_message, 1U, 0U, 0, 0U) == 0);
    assert(vanta_ipc_message_validate(&abi_message,
           VANTA_IPC_HEADER_SIZE) == 0);
    assert(vanta_ipc_message_validate(&abi_message,
           VANTA_IPC_HEADER_SIZE + 1U) == -VANTA_EPROTO);
    abi_message.version++;
    assert(vanta_ipc_message_validate(&abi_message,
           VANTA_IPC_HEADER_SIZE) == -VANTA_EPROTO);
    int64_t endpoint = ipc_endpoint_create(10U);
    assert(endpoint > 0);
    char out[IPC_MESSAGE_MAX] = {0};
    assert(ipc_message_receive((uint64_t)endpoint,10U,out,sizeof(out)) == -11);
    assert(ipc_message_peek_length((uint64_t)endpoint,10U) == -11);
    assert(ipc_message_peek_length((uint64_t)endpoint,11U) == -1);
    assert(ipc_message_receive((uint64_t)endpoint,11U,out,sizeof(out)) == -1);
    assert(ipc_message_send((uint64_t)endpoint,11U,"one",3U) == 0);
    assert(ipc_message_send((uint64_t)endpoint,12U,"two",3U) == 0);
    assert(ipc_message_peek_length((uint64_t)endpoint,10U) == 3);
    assert(ipc_message_peek_length((uint64_t)endpoint,10U) == 3);
    assert(ipc_message_receive((uint64_t)endpoint,10U,out,2U) == -90);
    assert(ipc_message_receive((uint64_t)endpoint,10U,out,sizeof(out)) == 3);
    assert(memcmp(out,"one",3U)==0);
    assert(ipc_message_receive((uint64_t)endpoint,10U,out,sizeof(out)) == 3);
    assert(memcmp(out,"two",3U)==0);
    assert(ipc_message_send((uint64_t)endpoint,11U,"",0U) == -22);
    assert(ipc_message_send((uint64_t)endpoint,11U,out,IPC_MESSAGE_MAX+1U) == -22);
    assert(ipc_message_send(0U,11U,"x",1U) == -22);
    assert(ipc_message_send((uint64_t)endpoint,11U,"x",1U) == 0);
    for (unsigned i=1;i<IPC_QUEUE_DEPTH;i++)
        assert(ipc_message_send((uint64_t)endpoint,11U,"x",1U) == 0);
    assert(ipc_message_send((uint64_t)endpoint,11U,"x",1U) == -11);
    assert(ipc_endpoint_close((uint64_t)endpoint,11U) == -1);
    assert(ipc_endpoint_close((uint64_t)endpoint,10U) == 0);
    assert(ipc_message_send((uint64_t)endpoint,11U,"x",1U) == -9);
    assert(ipc_message_peek_length((uint64_t)endpoint,10U) == -9);
    int64_t cleanup_endpoint = ipc_endpoint_create(77U);
    assert(cleanup_endpoint > 0);
    assert(ipc_message_send((uint64_t)cleanup_endpoint,88U,"queued",6U) == 0);
    ipc_process_cleanup(77U);
    assert(ipc_message_send((uint64_t)cleanup_endpoint,88U,"x",1U) == -9);
    test_endpoint_exhaustion_cleanup_and_rng_failure();
    puts("VANTAOS_IPC_HOST_TEST_OK");
    return 0;
}
