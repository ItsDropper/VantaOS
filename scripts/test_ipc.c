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
#include "../../kernel/ipc.c"

static uint64_t rng_counter = 0x123456789ULL;
int crypto_rng_fill(void *output, size_t length)
{
    uint8_t *bytes = (uint8_t *)output;
    uint64_t value = ++rng_counter;
    if (length > sizeof(value)) return 0;
    for (size_t i=0;i<length;i++) bytes[i]=(uint8_t)(value>>(i*8U));
    return 1;
}

int main(void)
{
    int64_t endpoint = ipc_endpoint_create(10U);
    assert(endpoint > 0);
    char out[IPC_MESSAGE_MAX] = {0};
    assert(ipc_message_receive((uint64_t)endpoint,10U,out,sizeof(out)) == -11);
    assert(ipc_message_receive((uint64_t)endpoint,11U,out,sizeof(out)) == -1);
    assert(ipc_message_send((uint64_t)endpoint,11U,"one",3U) == 0);
    assert(ipc_message_send((uint64_t)endpoint,12U,"two",3U) == 0);
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
    int64_t cleanup_endpoint = ipc_endpoint_create(77U);
    assert(cleanup_endpoint > 0);
    assert(ipc_message_send((uint64_t)cleanup_endpoint,88U,"queued",6U) == 0);
    ipc_process_cleanup(77U);
    assert(ipc_message_send((uint64_t)cleanup_endpoint,88U,"x",1U) == -9);
    puts("VANTAOS_IPC_HOST_TEST_OK");
    return 0;
}
