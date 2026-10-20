/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_msgq_rpc.h"

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>

#define THREAD_COUNT 4U
#define REQUESTS_PER_THREAD 8U
#define REQUEST_COUNT (THREAD_COUNT * REQUESTS_PER_THREAD)
#define PAYLOAD_SIZE 5000U
#define QUEUE_PAGES 80U
#define QUEUE_BYTES (QUEUE_PAGES * NVX_GSP_PAGE_SIZE)

static uint8_t tx_store[QUEUE_BYTES]
    __attribute__((aligned(NVX_GSP_MSGQ_HEADER_PAGE_SIZE)));
static uint8_t rx_store[QUEUE_BYTES]
    __attribute__((aligned(NVX_GSP_MSGQ_HEADER_PAGE_SIZE)));
static uint8_t scratch[THREAD_COUNT][NVX_GSP_MAX_MESSAGE_PAGES * NVX_GSP_PAGE_SIZE]
    __attribute__((aligned(NVX_GSP_PAGE_SIZE)));
static uint8_t payloads[THREAD_COUNT][REQUESTS_PER_THREAD][PAYLOAD_SIZE];
static uint32_t sequences[THREAD_COUNT][REQUESTS_PER_THREAD];

typedef struct fake_io {
    uint32_t registers[8];
} fake_io_t;

typedef struct worker_args {
    nvx_gsp_msgq_rpc_transport_t *transport;
    fake_io_t *io;
    unsigned thread_index;
    int failed;
} worker_args_t;

static int fake_read32(void *context, uint32_t reg, uint32_t *out)
{
    fake_io_t *io = (fake_io_t *)context;
    if (!io || !out || (reg & 3U) || reg / 4U >= 8U)
        return 0;
    *out = io->registers[reg / 4U];
    return 1;
}

static int fake_write32(void *context, uint32_t reg, uint32_t value)
{
    fake_io_t *io = (fake_io_t *)context;
    if (!io || (reg & 3U) || reg / 4U >= 8U)
        return 0;
    io->registers[reg / 4U] = value;
    return 1;
}

static void fake_barrier(void *context)
{
    (void)context;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

static void *send_worker(void *context)
{
    worker_args_t *args = (worker_args_t *)context;
    unsigned t = args->thread_index;
    for (unsigned i = 0U; i < REQUESTS_PER_THREAD; ++i) {
        uint32_t function = 0xA000U + t * REQUESTS_PER_THREAD + i;
        uint8_t fill = (uint8_t)(0x31U + t);
        memset(payloads[t][i], fill, PAYLOAD_SIZE);
        nvx_gsp_msgq_rpc_status_t status = nvx_gsp_msgq_rpc_send(
            args->transport, function, payloads[t][i], PAYLOAD_SIZE,
            scratch[t], sizeof(scratch[t]), &sequences[t][i]);
        if (status != NVX_GSP_MSGQ_RPC_OK)
            args->failed = 1;
    }
    return 0;
}

int main(void)
{
    fake_io_t io_state = {0};
    nvx_gsp_msgq_io_t io = {
        fake_read32, fake_write32, fake_barrier, fake_barrier, &io_state
    };
    nvx_gsp_msgq_queue_t tx = {0}, rx = {0};
    nvx_gsp_msgq_layout_t response_layout = {0};
    nvx_gsp_msgq_rpc_transport_t transport = {0};
    pthread_t threads[THREAD_COUNT];
    worker_args_t args[THREAD_COUNT] = {0};
    uint8_t seen[REQUEST_COUNT + 1U] = {0};

    assert(nvx_gsp_msgq_queue_init_tx(&tx, tx_store, sizeof(tx_store),
        NVX_GSP_PAGE_SIZE, &io, 0U, 4U) == NVX_GSP_MSGQ_QUEUE_OK);
    assert(nvx_gsp_msgq_prepare_tx(rx_store, sizeof(rx_store),
        NVX_GSP_PAGE_SIZE, &response_layout) == NVX_GSP_MSGQ_OK);
    assert(nvx_gsp_msgq_queue_init_rx(&rx, rx_store, sizeof(rx_store),
        NVX_GSP_PAGE_SIZE, &io, 8U, 12U) == NVX_GSP_MSGQ_QUEUE_OK);
    assert(nvx_gsp_msgq_rpc_init(&transport, &tx, &rx) == NVX_GSP_MSGQ_RPC_OK);

    for (unsigned t = 0U; t < THREAD_COUNT; ++t) {
        args[t].transport = &transport;
        args[t].io = &io_state;
        args[t].thread_index = t;
        assert(pthread_create(&threads[t], 0, send_worker, &args[t]) == 0);
    }
    for (unsigned t = 0U; t < THREAD_COUNT; ++t) {
        assert(pthread_join(threads[t], 0) == 0);
        assert(args[t].failed == 0);
    }

    /* Every 5 KB RPC must occupy two adjacent slots without interleaving. */
    assert(io_state.registers[0] == 2U * REQUEST_COUNT);
    for (unsigned i = 0U; i < REQUEST_COUNT; ++i) {
        const uint8_t *message = tx_store +
            NVX_GSP_PAGE_SIZE + (size_t)i * 2U * NVX_GSP_PAGE_SIZE;
        uint32_t function = read_le32(message + 36U);
        uint32_t sequence = read_le32(message + 48U);
        unsigned request_index;
        unsigned thread_index;
        unsigned per_thread_index;
        uint8_t expected_fill;

        assert(read_le32(message) == NVX_GSP_MCTP_MAGIC);
        assert(read_le32(message + 4U) ==
            NVX_GSP_MSG_HEADER_SIZE + NVX_GSP_RPC_HEADER_SIZE + PAYLOAD_SIZE);
        assert(function >= 0xA000U &&
               function < 0xA000U + REQUEST_COUNT);
        request_index = function - 0xA000U;
        thread_index = request_index / REQUESTS_PER_THREAD;
        per_thread_index = request_index % REQUESTS_PER_THREAD;
        expected_fill = (uint8_t)(0x31U + thread_index);

        assert(sequence > 0U && sequence <= REQUEST_COUNT);
        assert(seen[sequence] == 0U);
        seen[sequence] = 1U;
        assert(sequences[thread_index][per_thread_index] == sequence);
        for (size_t byte = 0U; byte < PAYLOAD_SIZE; ++byte)
            assert(message[NVX_GSP_MSG_HEADER_SIZE +
                NVX_GSP_RPC_HEADER_SIZE + byte] == expected_fill);
    }
    for (unsigned sequence = 1U; sequence <= REQUEST_COUNT; ++sequence)
        assert(seen[sequence] == 1U);

    return 0;
}
