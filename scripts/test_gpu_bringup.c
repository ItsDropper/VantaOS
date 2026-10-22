/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "gpu_bringup.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

typedef struct test_context {
    unsigned prepared;
    unsigned rolled_back;
    unsigned fail_prepare_at;
    unsigned fail_verify_at;
    unsigned fail_rollback_at;
} test_context_t;

static int prepare(void *opaque, unsigned stage)
{
    test_context_t *ctx = (test_context_t *)opaque;
    if (ctx->fail_prepare_at == stage)
        return 0;
    ctx->prepared |= 1U << stage;
    return 1;
}
static int verify(void *opaque, unsigned stage)
{
    test_context_t *ctx = (test_context_t *)opaque;
    return ctx->fail_verify_at != stage &&
           (ctx->prepared & (1U << stage)) != 0U;
}
static int rollback(void *opaque, unsigned stage)
{
    test_context_t *ctx = (test_context_t *)opaque;
    if (ctx->fail_rollback_at == stage)
        return 0;
    ctx->rolled_back |= 1U << stage;
    ctx->prepared &= ~(1U << stage);
    return 1;
}
#define STAGE(n, caps) { #n, \
    /* prepare */ (int (*)(void *))prepare, \
    /* verify */ (int (*)(void *))verify, \
    /* rollback */ (int (*)(void *))rollback, (caps) }

static int prepare0(void *p) { return prepare(p, 0U); }
static int verify0(void *p) { return verify(p, 0U); }
static int rollback0(void *p) { return rollback(p, 0U); }
static int prepare1(void *p) { return prepare(p, 1U); }
static int verify1(void *p) { return verify(p, 1U); }
static int rollback1(void *p) { return rollback(p, 1U); }
static int prepare2(void *p) { return prepare(p, 2U); }
static int verify2(void *p) { return verify(p, 2U); }
static int rollback2(void *p) { return rollback(p, 2U); }

int main(void)
{
    const gpu_bringup_stage_t stages[] = {
        { "mmio", prepare0, verify0, rollback0, 1ULL << 0 },
        { "reset", prepare1, verify1, rollback1, 1ULL << 1 },
        { "firmware", prepare2, verify2, rollback2, 1ULL << 2 }
    };
    test_context_t ctx = {0};
    gpu_bringup_result_t result;

    assert(gpu_bringup_execute(stages, 3U, &ctx, &result) == GPU_BRINGUP_OK);
    assert(result.completed_stages == 3U);
    assert(result.verified_capabilities == 7U);
    assert(result.rollback_complete == 1U);

    ctx = (test_context_t){ .fail_verify_at = 2U };
    assert(gpu_bringup_execute(stages, 3U, &ctx, &result)
           == GPU_BRINGUP_STAGE_FAILED);
    assert(result.failed_stage == 2U);
    assert(result.completed_stages == 0U);
    assert(result.verified_capabilities == 0U);
    assert(result.rollback_complete == 1U);
    assert(ctx.rolled_back == 7U);

    ctx = (test_context_t){ .fail_prepare_at = 1U };
    assert(gpu_bringup_execute(stages, 3U, &ctx, &result)
           == GPU_BRINGUP_STAGE_FAILED);
    assert(ctx.rolled_back == 1U);
    assert(result.verified_capabilities == 0U);

    ctx = (test_context_t){ .fail_verify_at = 2U, .fail_rollback_at = 1U };
    assert(gpu_bringup_execute(stages, 3U, &ctx, &result)
           == GPU_BRINGUP_ROLLBACK_FAILED);
    assert(result.rollback_complete == 0U);
    assert(result.verified_capabilities == 0U);

    assert(gpu_bringup_execute(NULL, 3U, &ctx, &result) == GPU_BRINGUP_INVALID);
    assert(gpu_bringup_execute(stages, GPU_BRINGUP_MAX_STAGES + 1U,
                               &ctx, &result) == GPU_BRINGUP_INVALID);
    return 0;
}
