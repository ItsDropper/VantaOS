/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_gmcapi.h"
#include "nvx_gsp_ring.h"
#include <string.h>

#define GMCAPI_MAX_WIRE_BYTES (NVX_GSP_MAX_MESSAGE_PAGES * NVX_GSP_PAGE_SIZE)
#define GMCAPI_OUTER_HEADER_SIZE 24U
#define GMCAPI_DATA_OFFSET (GMCAPI_OUTER_HEADER_SIZE + NVX_GSP_GMCAPI_HEADER_SIZE)

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}
static uint64_t get64(const uint8_t *p)
{
    return (uint64_t)get32(p) | ((uint64_t)get32(p + 4U) << 32U);
}
static void put32(uint8_t *p, uint32_t v)
{
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8U);
    p[2]=(uint8_t)(v>>16U); p[3]=(uint8_t)(v>>24U);
}
static void put64(uint8_t *p, uint64_t v)
{
    put32(p, (uint32_t)v); put32(p + 4U, (uint32_t)(v >> 32U));
}
static int overlaps(const void *a, size_t an, const void *b, size_t bn)
{
    uintptr_t x=(uintptr_t)a, y=(uintptr_t)b;
    if (!an || !bn) return 0;
    if (!a || !b || x > UINTPTR_MAX-an || y > UINTPTR_MAX-bn) return 1;
    return x < y+bn && y < x+an;
}
static size_t pages_for(size_t bytes)
{
    return bytes / NVX_GSP_PAGE_SIZE +
           (bytes % NVX_GSP_PAGE_SIZE != 0U ? 1U : 0U);
}
static uint64_t load_le64(const uint8_t *p)
{
    return (uint64_t)get32(p) | ((uint64_t)get32(p + 4U) << 32U);
}

/* Validate NVKV framing and bounds without assuming which keys this firmware supports. */
static int nvkv_wire_valid(const void *data, size_t bytes)
{
    const uint8_t *p = (const uint8_t *)data;
    size_t words, index = 0U;
    if (!data || bytes == 0U || (bytes % sizeof(uint64_t)) != 0U)
        return 0;
    words = bytes / sizeof(uint64_t);
    while (index < words) {
        uint64_t instruction = load_le64(p + index * sizeof(uint64_t));
        uint32_t opcode = (uint32_t)((instruction >> 28U) & 0xFU);
        uint64_t count = instruction >> 32U;
        uint64_t payload_words = 0U;
        ++index;
        switch (opcode) {
        case 0U: /* IMM32: immediate value is in the instruction itself. */
            continue;
        case 1U: /* SEQ32 */
        case 4U: /* ARRAY32 */
            payload_words = (count + 1U) / 2U;
            break;
        case 2U: /* SEQ64 */
        case 5U: /* ARRAY64 */
            payload_words = count;
            break;
        case 3U: /* ARRAY8 */
            payload_words = (count + 7U) / 8U;
            break;
        default:
            return 0;
        }
        if (payload_words > (uint64_t)(words - index))
            return 0;
        index += (size_t)payload_words;
    }
    return index == words;
}

static int add_size(size_t a, size_t b, size_t *out)
{
    if (a > SIZE_MAX-b) return 0;
    *out=a+b; return 1;
}
static nvx_gsp_gmcapi_status_t send_request(
    nvx_gsp_gmcapi_transport_t *t, uint32_t command_id,
    const void *request, size_t request_size, size_t response_capacity,
    uint8_t *scratch, size_t scratch_size, uint64_t *out_sequence)
{
    size_t gmc_size, wire_size, slots;
    uint64_t sequence;
    if (!add_size(NVX_GSP_GMCAPI_HEADER_SIZE, request_size, &gmc_size) ||
        !add_size(GMCAPI_OUTER_HEADER_SIZE, gmc_size, &wire_size) ||
        wire_size > GMCAPI_MAX_WIRE_BYTES ||
        response_capacity > GMCAPI_MAX_WIRE_BYTES-GMCAPI_DATA_OFFSET)
        return NVX_GSP_GMCAPI_RESPONSE_TOO_LARGE;
    slots=pages_for(wire_size);
    if (slots==0U || slots>t->command_queue->layout.element_count ||
        scratch_size<slots*NVX_GSP_PAGE_SIZE)
        return NVX_GSP_GMCAPI_RESPONSE_TOO_LARGE;

    memset(scratch,0,slots*NVX_GSP_PAGE_SIZE);
    put32(scratch,NVX_GSP_MCTP_MAGIC);
    put32(scratch+4U,(uint32_t)wire_size);
    put32(scratch+8U,NVX_GSP_MCTP_HEADER_SINGLE);
    put32(scratch+12U,NVX_GSP_GMCAPI_NVDM_HEADER);
    put32(scratch+16U,(uint32_t)gmc_size);
    put32(scratch+20U,0U);

    sequence=t->next_sequence;
    if (sequence==0U) sequence=1U;
    put32(scratch+24U,command_id);
    put32(scratch+28U,(uint32_t)request_size);
    put64(scratch+32U,sequence);
    put32(scratch+40U,(uint32_t)response_capacity);
    /* Reserved GMCAPI request words [5] are zero from memset. */
    if (request_size) memcpy(scratch+GMCAPI_DATA_OFFSET,request,request_size);

    for (size_t i=0U;i<slots;i++) {
        uint8_t *element=NULL;
        if (nvx_gsp_msgq_tx_get(t->command_queue,(uint32_t)i,&element) !=
            NVX_GSP_MSGQ_QUEUE_OK || !element)
            return NVX_GSP_GMCAPI_QUEUE_ERROR;
        memcpy(element,scratch+i*NVX_GSP_PAGE_SIZE,NVX_GSP_PAGE_SIZE);
    }
    if (nvx_gsp_msgq_tx_submit(t->command_queue,(uint32_t)slots) !=
        NVX_GSP_MSGQ_QUEUE_OK)
        return NVX_GSP_GMCAPI_QUEUE_ERROR;
    t->next_sequence=sequence+1U;
    if (t->next_sequence==0U) t->next_sequence=1U;
    *out_sequence=sequence;
    return NVX_GSP_GMCAPI_OK;
}

static nvx_gsp_gmcapi_status_t receive_response(
    nvx_gsp_gmcapi_transport_t *t, uint32_t command_id, uint64_t sequence,
    void *response, size_t response_capacity, size_t *out_size,
    uint32_t *out_remote_status, uint8_t *scratch, size_t scratch_size)
{
    const uint8_t *first=NULL;
    uint32_t wire_size, gmc_size, slots;
    size_t rounded;
    nvx_gsp_msgq_queue_status_t qs;
    qs=nvx_gsp_msgq_rx_get(t->status_queue,0U,&first);
    if (qs==NVX_GSP_MSGQ_QUEUE_EMPTY) return NVX_GSP_GMCAPI_TIMEOUT;
    if (qs!=NVX_GSP_MSGQ_QUEUE_OK || !first)
        return NVX_GSP_GMCAPI_QUEUE_ERROR;

    wire_size=get32(first+4U);
    if (get32(first)!=NVX_GSP_MCTP_MAGIC ||
        wire_size<GMCAPI_DATA_OFFSET ||
        wire_size>GMCAPI_MAX_WIRE_BYTES) {
        (void)nvx_gsp_msgq_rx_consume(t->status_queue,1U);
        return NVX_GSP_GMCAPI_BAD_MESSAGE;
    }
    slots=(uint32_t)pages_for(wire_size);
    rounded=(size_t)slots*NVX_GSP_PAGE_SIZE;
    if (scratch_size<rounded) return NVX_GSP_GMCAPI_RESPONSE_TOO_LARGE;
    for (uint32_t i=0U;i<slots;i++) {
        const uint8_t *element=NULL;
        qs=nvx_gsp_msgq_rx_get(t->status_queue,i,&element);
        if (qs==NVX_GSP_MSGQ_QUEUE_EMPTY) return NVX_GSP_GMCAPI_TIMEOUT;
        if (qs!=NVX_GSP_MSGQ_QUEUE_OK || !element)
            return NVX_GSP_GMCAPI_QUEUE_ERROR;
        memcpy(scratch+(size_t)i*NVX_GSP_PAGE_SIZE,element,NVX_GSP_PAGE_SIZE);
    }

    gmc_size=get32(scratch+16U);
    if (get32(scratch+8U)!=NVX_GSP_MCTP_HEADER_SINGLE ||
        get32(scratch+12U)!=NVX_GSP_GMCAPI_NVDM_HEADER ||
        get32(scratch+20U)!=0U ||
        gmc_size!=wire_size-GMCAPI_OUTER_HEADER_SIZE ||
        gmc_size<NVX_GSP_GMCAPI_HEADER_SIZE ||
        get32(scratch+28U)!=gmc_size-NVX_GSP_GMCAPI_HEADER_SIZE ||
        (get32(scratch+24U)&~NVX_GSP_GMCAPI_COMMAND_FLAG_RESPONSE)!=command_id ||
        (get32(scratch+24U)&NVX_GSP_GMCAPI_COMMAND_FLAG_RESPONSE)==0U ||
        get64(scratch+32U)!=sequence) {
        (void)nvx_gsp_msgq_rx_consume(t->status_queue,slots);
        return NVX_GSP_GMCAPI_UNMATCHED_RESPONSE;
    }
    for (size_t i=0U;i<5U;i++) {
        if (get32(scratch+44U+i*4U)!=0U) {
            (void)nvx_gsp_msgq_rx_consume(t->status_queue,slots);
            return NVX_GSP_GMCAPI_BAD_MESSAGE;
        }
    }
    *out_size=get32(scratch+28U);
    *out_remote_status=get32(scratch+40U);
    if (*out_size>response_capacity || (*out_size && !response)) {
        (void)nvx_gsp_msgq_rx_consume(t->status_queue,slots);
        return NVX_GSP_GMCAPI_RESPONSE_TOO_LARGE;
    }
    if (*out_size) memcpy(response,scratch+GMCAPI_DATA_OFFSET,*out_size);
    if (nvx_gsp_msgq_rx_consume(t->status_queue,slots)!=NVX_GSP_MSGQ_QUEUE_OK)
        return NVX_GSP_GMCAPI_QUEUE_ERROR;
    return *out_remote_status==0U
        ? NVX_GSP_GMCAPI_OK : NVX_GSP_GMCAPI_REMOTE_ERROR;
}

nvx_gsp_gmcapi_status_t nvx_gsp_gmcapi_init(
    nvx_gsp_gmcapi_transport_t *t,
    nvx_gsp_msgq_queue_t *command_queue,
    nvx_gsp_msgq_queue_t *status_queue)
{
    if (!t || !command_queue || !status_queue ||
        command_queue==status_queue ||
        overlaps(t,sizeof(*t),command_queue,sizeof(*command_queue)) ||
        overlaps(t,sizeof(*t),status_queue,sizeof(*status_queue)) ||
        overlaps(command_queue,sizeof(*command_queue),status_queue,sizeof(*status_queue)))
        return NVX_GSP_GMCAPI_INVALID_ARGUMENT;
    if (t->initialized) return NVX_GSP_GMCAPI_INVALID_ARGUMENT;
    if (!command_queue->initialized || !command_queue->transmit ||
        !status_queue->initialized || status_queue->transmit ||
        overlaps(command_queue->backing_store,command_queue->layout.backing_store_size,
                 status_queue->backing_store,status_queue->layout.backing_store_size))
        return NVX_GSP_GMCAPI_NOT_INITIALIZED;
    t->command_queue=command_queue;
    t->status_queue=status_queue;
    t->next_sequence=1U;
    t->initialized=1U;
    return NVX_GSP_GMCAPI_OK;
}

nvx_gsp_gmcapi_status_t nvx_gsp_gmcapi_call(
    nvx_gsp_gmcapi_transport_t *t, uint32_t command_id,
    const void *request, size_t request_size,
    void *response, size_t response_capacity, size_t *out_response_size,
    uint32_t *out_remote_status, void *scratch, size_t scratch_size,
    uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_gmcapi_delay_fn delay_ms, void *context)
{
    uint64_t sequence=0U;
    uint32_t elapsed=0U;
    size_t request_wire, response_wire, needed;
    nvx_gsp_gmcapi_status_t status;
    if (out_response_size) *out_response_size=0U;
    if (out_remote_status) *out_remote_status=UINT32_MAX;
    if (!t || !request || !request_size || !response || !response_capacity ||
        !out_response_size || !out_remote_status || !scratch || !delay_ms ||
        command_id==0U || (command_id&~NVX_GSP_GMCAPI_COMMAND_ID_MASK)!=0U ||
        timeout_ms==0U || poll_interval_ms==0U)
        return NVX_GSP_GMCAPI_INVALID_ARGUMENT;
    if (!t->initialized) return NVX_GSP_GMCAPI_NOT_INITIALIZED;
    if (!add_size(GMCAPI_DATA_OFFSET,request_size,&request_wire) ||
        !add_size(GMCAPI_DATA_OFFSET,response_capacity,&response_wire))
        return NVX_GSP_GMCAPI_RESPONSE_TOO_LARGE;
    needed=pages_for(request_wire);
    if (pages_for(response_wire)>needed) needed=pages_for(response_wire);
    if (needed>NVX_GSP_MAX_MESSAGE_PAGES ||
        scratch_size<needed*NVX_GSP_PAGE_SIZE ||
        overlaps(t,sizeof(*t),scratch,scratch_size) ||
        overlaps(t,sizeof(*t),request,request_size) ||
        overlaps(t,sizeof(*t),response,response_capacity) ||
        overlaps(request,request_size,response,response_capacity) ||
        overlaps(scratch,scratch_size,request,request_size) ||
        overlaps(scratch,scratch_size,response,response_capacity) ||
        overlaps(scratch,scratch_size,t->command_queue->backing_store,
                 t->command_queue->layout.backing_store_size) ||
        overlaps(scratch,scratch_size,t->status_queue->backing_store,
                 t->status_queue->layout.backing_store_size) ||
        overlaps(request,request_size,t->command_queue->backing_store,
                 t->command_queue->layout.backing_store_size) ||
        overlaps(request,request_size,t->status_queue->backing_store,
                 t->status_queue->layout.backing_store_size) ||
        overlaps(response,response_capacity,t->command_queue->backing_store,
                 t->command_queue->layout.backing_store_size) ||
        overlaps(response,response_capacity,t->status_queue->backing_store,
                 t->status_queue->layout.backing_store_size))
        return NVX_GSP_GMCAPI_INVALID_ARGUMENT;

    status=send_request(t,command_id,request,request_size,response_capacity,
                        (uint8_t *)scratch,scratch_size,&sequence);
    if (status!=NVX_GSP_GMCAPI_OK) return status;
    do {
        status=receive_response(t,command_id,sequence,response,response_capacity,
                                out_response_size,out_remote_status,
                                (uint8_t *)scratch,scratch_size);
        if (status!=NVX_GSP_GMCAPI_TIMEOUT) return status;
        uint32_t delay=poll_interval_ms;
        if (delay>timeout_ms-elapsed) delay=timeout_ms-elapsed;
        delay_ms(delay,context);
        elapsed+=delay;
    } while (elapsed<timeout_ms);
    return NVX_GSP_GMCAPI_TIMEOUT;
}

nvx_gsp_gmcapi_status_t nvx_gsp_gmcapi_gsp_init(
    nvx_gsp_gmcapi_transport_t *transport,
    const void *encoded_system_info, size_t encoded_system_info_size,
    void *static_info, size_t static_info_capacity, size_t *out_static_info_size,
    uint32_t *out_remote_status, void *scratch, size_t scratch_size,
    uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_gmcapi_delay_fn delay_ms, void *context)
{
    nvx_gsp_gmcapi_status_t status;
    if (!encoded_system_info || !static_info || !out_static_info_size ||
        !nvkv_wire_valid(encoded_system_info, encoded_system_info_size))
        return NVX_GSP_GMCAPI_BAD_MESSAGE;

    status = nvx_gsp_gmcapi_call(transport,NVX_GSP_GMCAPI_CMD_GSP_INIT,
        encoded_system_info,encoded_system_info_size,static_info,
        static_info_capacity,out_static_info_size,out_remote_status,
        scratch,scratch_size,timeout_ms,poll_interval_ms,delay_ms,context);
    if (status != NVX_GSP_GMCAPI_OK)
        return status;
    if (!nvkv_wire_valid(static_info, *out_static_info_size)) {
        *out_static_info_size = 0U;
        return NVX_GSP_GMCAPI_BAD_MESSAGE;
    }
    return NVX_GSP_GMCAPI_OK;
}
