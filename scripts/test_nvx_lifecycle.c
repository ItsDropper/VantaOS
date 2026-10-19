/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_lifecycle.h"

#include <assert.h>
#include <stdint.h>

int main(void)
{
    nvx_lifecycle_t lifecycle;

    nvx_lifecycle_init(0);
    nvx_lifecycle_init(&lifecycle);
    assert(lifecycle.state == NVX_STATE_DISCOVERED);
    assert(lifecycle.error == NVX_ERROR_NONE);
    assert(lifecycle.verified_capabilities == 0);

    /* Discovery must never jump straight to initialized or running. */
    assert(!nvx_lifecycle_transition(&lifecycle, NVX_STATE_INITIALIZED));
    assert(!nvx_lifecycle_transition(&lifecycle, NVX_STATE_RUNNING));
    assert(lifecycle.state == NVX_STATE_DISCOVERED);
    assert(!nvx_lifecycle_set_capabilities(&lifecycle, UINT64_MAX));

    assert(nvx_lifecycle_transition(&lifecycle, NVX_STATE_PROBING));
    assert(nvx_lifecycle_transition(&lifecycle, NVX_STATE_RESOURCES_READY));
    assert(nvx_lifecycle_transition(&lifecycle, NVX_STATE_PROBED));
    assert(!nvx_lifecycle_set_capabilities(&lifecycle, UINT64_MAX));
    assert(nvx_lifecycle_transition(&lifecycle, NVX_STATE_INITIALIZING));
    assert(nvx_lifecycle_transition(&lifecycle, NVX_STATE_INITIALIZED));
    assert(nvx_lifecycle_set_capabilities(&lifecycle, 1U));
    assert(lifecycle.verified_capabilities == 1U);
    assert(nvx_lifecycle_transition(&lifecycle, NVX_STATE_RUNNING));
    assert(nvx_lifecycle_set_capabilities(&lifecycle, 3U));
    assert(nvx_lifecycle_transition(&lifecycle, NVX_STATE_QUIESCING));
    assert(nvx_lifecycle_transition(&lifecycle, NVX_STATE_STOPPED));
    assert(!nvx_lifecycle_transition(&lifecycle, NVX_STATE_RUNNING));
    assert(!nvx_lifecycle_fail(&lifecycle, NVX_ERROR_RUNTIME));

    nvx_lifecycle_init(&lifecycle);
    assert(nvx_lifecycle_transition(&lifecycle, NVX_STATE_PROBING));
    assert(nvx_lifecycle_fail(&lifecycle, NVX_ERROR_RESOURCE_VALIDATION));
    assert(lifecycle.state == NVX_STATE_FAILED);
    assert(lifecycle.error == NVX_ERROR_RESOURCE_VALIDATION);
    assert(lifecycle.verified_capabilities == 0);
    assert(!nvx_lifecycle_transition(&lifecycle, NVX_STATE_RESOURCES_READY));
    assert(!nvx_lifecycle_fail(&lifecycle, NVX_ERROR_RUNTIME));
    assert(!nvx_lifecycle_fail(&lifecycle, NVX_ERROR_NONE));
    assert(!nvx_lifecycle_set_capabilities(&lifecycle, UINT64_MAX));

    /* A late runtime failure must revoke previously verified capabilities. */
    nvx_lifecycle_init(&lifecycle);
    assert(nvx_lifecycle_transition(&lifecycle, NVX_STATE_PROBING));
    assert(nvx_lifecycle_transition(&lifecycle, NVX_STATE_RESOURCES_READY));
    assert(nvx_lifecycle_transition(&lifecycle, NVX_STATE_INITIALIZING));
    assert(nvx_lifecycle_transition(&lifecycle, NVX_STATE_INITIALIZED));
    assert(nvx_lifecycle_set_capabilities(&lifecycle, UINT64_MAX));
    assert(nvx_lifecycle_fail(&lifecycle, NVX_ERROR_RUNTIME));
    assert(lifecycle.state == NVX_STATE_FAILED);
    assert(lifecycle.error == NVX_ERROR_RUNTIME);
    assert(lifecycle.verified_capabilities == 0);

    assert(!nvx_lifecycle_transition(0, NVX_STATE_PROBING));
    assert(!nvx_lifecycle_set_capabilities(0, 0));

    return 0;
}
