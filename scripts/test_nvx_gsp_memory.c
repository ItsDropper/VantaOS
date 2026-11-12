/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_memory.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

#define RESPONSE_HEADER 32U

static nvx_gsp_rmapi_status_t alloc_submit_status, free_submit_status,
    wait_status;
static nvx_gsp_rpc_status_t release_status;
static int alloc_handle_valid = 1, free_handle_valid = 1;
static int truncate_alloc_response;
static uint64_t returned_limit_delta;
static unsigned alloc_calls, free_calls, wait_calls, release_calls;
static uint32_t allocated_class, allocated_parent, allocated_object;
static nvx_gsp_memory_allocation_params_t captured_params;

nvx_gsp_rmapi_status_t nvx_gsp_rm_alloc_submit(
    nvx_gsp_rpc_manager_t *manager, uint32_t client, uint32_t parent,
    uint32_t object, uint32_t class_id, uint32_t flags, const void *params,
    size_t params_size, void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, nvx_gsp_rmapi_result_t *out)
{
    (void)manager; (void)client; (void)flags; (void)scratch;
    (void)scratch_size; (void)response; (void)response_capacity;
    assert(params_size == sizeof(captured_params));
    memcpy(&captured_params, params, sizeof(captured_params));
    allocated_parent = parent;
    allocated_object = object;
    allocated_class = class_id;
    alloc_calls++;
    out->function = NVX_GSP_RPC_FN_RM_ALLOC;
    out->response_size = RESPONSE_HEADER + sizeof(captured_params);
    out->handle.valid = (uint8_t)alloc_handle_valid;
    return alloc_submit_status;
}

nvx_gsp_rmapi_status_t nvx_gsp_rm_free_submit(
    nvx_gsp_rpc_manager_t *manager, uint32_t root, uint32_t parent,
    uint32_t object, void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, nvx_gsp_rmapi_result_t *out)
{
    (void)manager; (void)root; (void)scratch; (void)scratch_size;
    (void)response; (void)response_capacity;
    assert(parent == 2U && object == 3U);
    free_calls++;
    out->function = NVX_GSP_RPC_FN_FREE;
    out->response_size = 16U;
    out->handle.valid = (uint8_t)free_handle_valid;
    return free_submit_status;
}

nvx_gsp_rmapi_status_t nvx_gsp_rmapi_wait(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_rmapi_result_t *operation,
    void *scratch, size_t scratch_size, void *response, size_t response_size,
    uint32_t timeout, uint32_t interval, nvx_gsp_rmapi_delay_fn delay,
    void *delay_context)
{
    (void)manager; (void)scratch; (void)scratch_size; (void)timeout;
    (void)interval; (void)delay; (void)delay_context;
    wait_calls++;
    if (operation->function == NVX_GSP_RPC_FN_RM_ALLOC &&
        wait_status == NVX_GSP_RMAPI_OK) {
        nvx_gsp_memory_allocation_params_t returned = captured_params;
        returned.size = captured_params.size + 4096U;
        returned.limit = returned.size - 1U + returned_limit_delta;
        returned.address = 0U;
        if (truncate_alloc_response) {
            operation->response_size = RESPONSE_HEADER;
        } else {
            assert(response_size >= RESPONSE_HEADER + sizeof(returned));
            memcpy((uint8_t *)response + RESPONSE_HEADER, &returned,
                sizeof(returned));
            operation->response_size = RESPONSE_HEADER + sizeof(returned);
        }
    } else if (operation->function == NVX_GSP_RPC_FN_RM_ALLOC) {
        operation->response_size = RESPONSE_HEADER;
    }
    return wait_status;
}

nvx_gsp_rpc_status_t nvx_gsp_rpc_release(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_rpc_handle_t *handle)
{
    (void)manager;
    release_calls++;
    if (release_status == NVX_GSP_RPC_OK && handle)
        *handle = (nvx_gsp_rpc_handle_t){0};
    return release_status;
}

static void delay(uint32_t ms, void *context)
{
    (void)ms; (void)context;
}

static nvx_gsp_memory_allocation_params_t params(void)
{
    nvx_gsp_memory_allocation_params_t p = {0};
    assert(nvx_gsp_memory_params_init_system(&p, 16384U, 0x1234U) ==
        NVX_GSP_RMAPI_OK);
    return p;
}

int main(void)
{
    nvx_gsp_rpc_manager_t manager = {0};
    nvx_gsp_memory_object_t object = {0};
    nvx_gsp_memory_allocation_params_t p = params();
    uint8_t scratch[512], response[512];

    alloc_submit_status = free_submit_status = wait_status =
        NVX_GSP_RMAPI_OK;
    release_status = NVX_GSP_RPC_OK;

    assert(nvx_gsp_memory_params_init_system(&p, 0U, 0U) ==
        NVX_GSP_RMAPI_INVALID_ARGUMENT);
    p = params();
    assert(p.owner == UINT32_C(0x7368696D));
    assert(p.type == 0U);
    assert(p.attr == UINT32_C(0x0A000000));
    assert(p.attr2 == UINT32_C(0x00000008));
    assert(p.flags == NVX_GSP_MEMORY_ALLOC_FLAG_ALIGNMENT_FORCE &&
           p.size == 16384U && p.alignment == 4096U);
    assert(sizeof(p) == 128U);
    assert(offsetof(nvx_gsp_memory_allocation_params_t, range_lo) == 48U);
    assert(offsetof(nvx_gsp_memory_allocation_params_t, size) == 64U);
    assert(offsetof(nvx_gsp_memory_allocation_params_t, limit) == 88U);
    assert(offsetof(nvx_gsp_memory_allocation_params_t, address) == 96U);
    assert(nvx_gsp_memory_object_create(&manager, &object, 1U, 2U, 3U, &p,
        scratch, sizeof(scratch), response, sizeof(response), 100U, 1U,
        delay, NULL) == NVX_GSP_RMAPI_OK);
    assert(object.stage == NVX_GSP_MEMORY_CREATED);
    assert(object.remote_created && object.result_valid);
    assert(object.actual_size == 20480U && object.limit == 20479U);
    assert(allocated_class == NVX_GSP_RM_CLASS_MEMORY_SYSTEM);
    assert(allocated_parent == 2U && allocated_object == 3U);
    assert(captured_params.owner == p.owner && captured_params.size == p.size);
    assert(alloc_calls == 1U && release_calls == 1U);

    assert(nvx_gsp_memory_object_create(&manager, &object, 1U, 2U, 4U, &p,
        scratch, sizeof(scratch), response, sizeof(response), 100U, 1U,
        delay, NULL) == NVX_GSP_RMAPI_INVALID_ARGUMENT);
    assert(nvx_gsp_memory_object_destroy(&manager, &object, scratch,
        sizeof(scratch), response, sizeof(response), 100U, 1U, delay, NULL)
        == NVX_GSP_RMAPI_OK);
    assert(object.stage == NVX_GSP_MEMORY_IDLE && free_calls == 1U);

    /* Invalid allocation alignment is rejected before issuing an RPC. */
    p.alignment = 3000U;
    assert(nvx_gsp_memory_object_create(&manager, &object, 1U, 2U, 3U, &p,
        scratch, sizeof(scratch), response, sizeof(response), 100U, 1U,
        delay, NULL) == NVX_GSP_RMAPI_INVALID_ARGUMENT);
    p = params();

    /* Timeout and transport failure retain the allocation RPC and buffers. */
    wait_status = NVX_GSP_RMAPI_TIMEOUT;
    assert(nvx_gsp_memory_object_create(&manager, &object, 1U, 2U, 3U, &p,
        scratch, sizeof(scratch), response, sizeof(response), 10U, 1U,
        delay, NULL) == NVX_GSP_RMAPI_TIMEOUT);
    assert(object.stage == NVX_GSP_MEMORY_ALLOC_PENDING &&
        object.operation.handle.valid);
    assert(nvx_gsp_memory_object_recover(&manager, &object, scratch,
        sizeof(scratch), response, sizeof(response), 10U, 1U, delay, NULL)
        == NVX_GSP_RMAPI_TIMEOUT);
    wait_status = NVX_GSP_RMAPI_RPC_ERROR;
    assert(nvx_gsp_memory_object_recover(&manager, &object, scratch,
        sizeof(scratch), response, sizeof(response), 10U, 1U, delay, NULL)
        == NVX_GSP_RMAPI_RPC_ERROR);
    assert(object.operation.handle.valid);
    wait_status = NVX_GSP_RMAPI_OK;
    assert(nvx_gsp_memory_object_recover(&manager, &object, scratch,
        sizeof(scratch), response, sizeof(response), 10U, 1U, delay, NULL)
        == NVX_GSP_RMAPI_OK);
    assert(object.stage == NVX_GSP_MEMORY_CREATED && object.result_valid);

    /* A completed remote allocation with truncated params remains owned. */
    assert(nvx_gsp_memory_object_destroy(&manager, &object, scratch,
        sizeof(scratch), response, sizeof(response), 100U, 1U, delay, NULL)
        == NVX_GSP_RMAPI_OK);
    truncate_alloc_response = 1;
    assert(nvx_gsp_memory_object_create(&manager, &object, 1U, 2U, 3U, &p,
        scratch, sizeof(scratch), response, sizeof(response), 10U, 1U,
        delay, NULL) == NVX_GSP_RMAPI_MALFORMED_RESPONSE);
    assert(object.stage == NVX_GSP_MEMORY_CREATED && object.remote_created &&
        !object.result_valid);
    truncate_alloc_response = 0;
    wait_status = NVX_GSP_RMAPI_OK;
    assert(nvx_gsp_memory_object_destroy(&manager, &object, scratch,
        sizeof(scratch), response, sizeof(response), 100U, 1U, delay, NULL)
        == NVX_GSP_RMAPI_OK);

    /*
     * An inclusive limit that extends beyond the returned allocation is not
     * safe to pass to CTXDMA. Keep ownership so the malformed object is freed.
     */
    returned_limit_delta = 1U;
    assert(nvx_gsp_memory_object_create(&manager, &object, 1U, 2U, 3U, &p,
        scratch, sizeof(scratch), response, sizeof(response), 10U, 1U,
        delay, NULL) == NVX_GSP_RMAPI_MALFORMED_RESPONSE);
    assert(object.stage == NVX_GSP_MEMORY_CREATED && object.remote_created &&
        !object.result_valid);
    returned_limit_delta = 0U;
    assert(nvx_gsp_memory_object_destroy(&manager, &object, scratch,
        sizeof(scratch), response, sizeof(response), 100U, 1U, delay, NULL)
        == NVX_GSP_RMAPI_OK);
    assert(object.stage == NVX_GSP_MEMORY_IDLE);

    /* Free failure preserves the object; a later free can safely retry. */

    assert(nvx_gsp_memory_object_create(&manager, &object, 1U, 2U, 3U, &p,
        scratch, sizeof(scratch), response, sizeof(response), 100U, 1U,
        delay, NULL) == NVX_GSP_RMAPI_OK);
    wait_status = NVX_GSP_RMAPI_REMOTE_ERROR;
    assert(nvx_gsp_memory_object_destroy(&manager, &object, scratch,
        sizeof(scratch), response, sizeof(response), 100U, 1U, delay, NULL)
        == NVX_GSP_RMAPI_REMOTE_ERROR);
    assert(object.stage == NVX_GSP_MEMORY_CREATED);
    wait_status = NVX_GSP_RMAPI_OK;
    assert(nvx_gsp_memory_object_destroy(&manager, &object, scratch,
        sizeof(scratch), response, sizeof(response), 100U, 1U, delay, NULL)
        == NVX_GSP_RMAPI_OK);

    /* A local handle-release failure is retryable without duplicate free. */
    assert(nvx_gsp_memory_object_create(&manager, &object, 1U, 2U, 3U, &p,
        scratch, sizeof(scratch), response, sizeof(response), 100U, 1U,
        delay, NULL) == NVX_GSP_RMAPI_OK);
    release_status = NVX_GSP_RPC_TRANSPORT_ERROR;
    assert(nvx_gsp_memory_object_destroy(&manager, &object, scratch,
        sizeof(scratch), response, sizeof(response), 100U, 1U, delay, NULL)
        == NVX_GSP_RMAPI_RPC_ERROR);
    assert(object.stage == NVX_GSP_MEMORY_FREE_RELEASE_PENDING);
    release_status = NVX_GSP_RPC_OK;
    assert(nvx_gsp_memory_object_recover(&manager, &object, scratch,
        sizeof(scratch), response, sizeof(response), 100U, 1U, delay, NULL)
        == NVX_GSP_RMAPI_OK);
    assert(object.stage == NVX_GSP_MEMORY_IDLE);

    assert(nvx_gsp_memory_object_create(&manager, &object, 1U, 2U, 3U, &p,
        scratch, sizeof(scratch), response, sizeof(response), 100U, 1U,
        delay, NULL) == NVX_GSP_RMAPI_OK);
    assert(nvx_gsp_memory_object_destroy(&manager, &object, scratch,
        sizeof(scratch), response, sizeof(response), 100U, 1U, delay, NULL)
        == NVX_GSP_RMAPI_OK);

    /* Distinct objects may not alias the manager or RPC work buffers. */
    memset(&object, 0, sizeof(object));
    assert(nvx_gsp_memory_object_create(&manager, &object, 1U, 2U, 3U, &p,
        scratch, sizeof(scratch), response, sizeof(response), 100U, 1U,
        delay, NULL) == NVX_GSP_RMAPI_OK);
    assert(nvx_gsp_memory_object_destroy(&manager, &object, scratch,
        sizeof(scratch), response, sizeof(response), 100U, 1U, delay, NULL)
        == NVX_GSP_RMAPI_OK);
    return 0;
}
