/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
/* HOST TEST of the evidence policy with mock stages. Not hardware evidence. */
#include "nvx_hwval.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct mock {
    int fail_stage;        /* 1-based stage that returns FAIL, 0 = none */
    int touched;
    int teardown_ok;
    int teardown_calls;
    int calls[NVX_HWVAL_STAGE_COUNT];
    int evidence_free_stage;    /* stage that returns PASS with no evidence */
    int no_gpu_flag;            /* stage 10 omits completion_gpu_written */
    int stale_completion;       /* stage 10 initial == expected */
    int mismatch;               /* stage 11 reports mismatches */
    int guard_bad;
    int bad_status_stage;
} mock_t;

static nvx_hwval_stage_status_t generic(mock_t *m, unsigned stage, nvx_hwval_stage_record_t *r)
{
    m->calls[stage - 1U]++;
    m->touched = 1;
    if (m->fail_stage == (int)stage) {
        r->reason = "mock failure";
        nvx_hwval_add_u64(r, "err", 5U);
        return NVX_HWVAL_FAIL;
    }
    if (m->bad_status_stage == (int)stage)
        return (nvx_hwval_stage_status_t)99;
    if (m->evidence_free_stage == (int)stage)
        return NVX_HWVAL_PASS;
    nvx_hwval_add_u64(r, "ok", stage);
    return NVX_HWVAL_PASS;
}
#define G(n) static nvx_hwval_stage_status_t s##n(void *c, nvx_hwval_stage_record_t *r) { return generic((mock_t *)c, n, r); }
G(1) G(2) G(3) G(4) G(5) G(6) G(7) G(8) G(9)

static nvx_hwval_stage_status_t s10(void *c, nvx_hwval_stage_record_t *r)
{
    mock_t *m = c;
    nvx_hwval_stage_status_t g = generic(m, 10U, r);
    if (g != NVX_HWVAL_PASS) return g;
    if (m->no_gpu_flag != 1) nvx_hwval_add_u64(r, "completion_gpu_written", m->no_gpu_flag == 2 ? 0U : 1U);
    nvx_hwval_add_u64(r, "completion_initial", m->stale_completion ? 0xABCDU : 0U);
    nvx_hwval_add_u64(r, "completion_expected", 0xABCDU);
    nvx_hwval_add_u64(r, "completion_observed", 0xABCDU);
    return NVX_HWVAL_PASS;
}
static nvx_hwval_stage_status_t s11(void *c, nvx_hwval_stage_record_t *r)
{
    mock_t *m = c;
    nvx_hwval_stage_status_t g = generic(m, 11U, r);
    if (g != NVX_HWVAL_PASS) return g;
    nvx_hwval_add_u64(r, "bytes_verified", 4096U);
    nvx_hwval_add_u64(r, "mismatch_count", m->mismatch ? 3U : 0U);
    if (!m->guard_bad) nvx_hwval_add_u64(r, "guard_mismatch_count", 0U);
    return NVX_HWVAL_PASS;
}
static int touched(void *c) { return ((mock_t *)c)->touched; }
static int teardown(void *c, nvx_hwval_stage_record_t *r)
{
    mock_t *m = c;
    m->teardown_calls++;
    nvx_hwval_add_u64(r, "quiesced", (uint64_t)m->teardown_ok);
    return m->teardown_ok;
}

static void make_ops(nvx_hwval_ops_t *ops, mock_t *m)
{
    memset(ops, 0, sizeof(*ops));
    ops->stage[0] = s1; ops->stage[1] = s2; ops->stage[2] = s3; ops->stage[3] = s4;
    ops->stage[4] = s5; ops->stage[5] = s6; ops->stage[6] = s7; ops->stage[7] = s8;
    ops->stage[8] = s9; ops->stage[9] = s10; ops->stage[10] = s11;
    ops->teardown = teardown; ops->hardware_touched = touched; ops->context = m;
}

static nvx_hwval_report_t rep;
static nvx_hwval_ops_t ops;

static nvx_hwval_verdict_t run(mock_t *m, nvx_hwval_source_t src)
{
    make_ops(&ops, m);
    return nvx_hwval_run(&ops, src, &rep);
}

static char lines[32][700];
static int nlines;
static void emit(const char *l, void *c) { (void)c; assert(nlines < 32); strcpy(lines[nlines++], l); }

int main(void)
{
    mock_t m;

    /* All pass, mock source: never HARDWARE_VERIFIED. */
    memset(&m, 0, sizeof(m)); m.teardown_ok = 1;
    assert(run(&m, NVX_HWVAL_SOURCE_HOST_MOCK) == NVX_HWVAL_VERDICT_HOST_ALL_PASS_NOT_HARDWARE);
    for (unsigned i = 0; i < 12; i++) assert(rep.stage[i].status == NVX_HWVAL_PASS);
    assert(!rep.memory_retained);

    /* Same run flagged as hardware source verifies. */
    memset(&m, 0, sizeof(m)); m.teardown_ok = 1;
    assert(run(&m, NVX_HWVAL_SOURCE_HARDWARE) == NVX_HWVAL_VERDICT_HARDWARE_VERIFIED);

    /* Failure at stage 7: 8-11 BLOCKED and never invoked; teardown still runs. */
    memset(&m, 0, sizeof(m)); m.teardown_ok = 1; m.fail_stage = 7;
    assert(run(&m, NVX_HWVAL_SOURCE_HARDWARE) == NVX_HWVAL_VERDICT_FAILED);
    assert(rep.stage[6].status == NVX_HWVAL_FAIL);
    for (unsigned i = 7; i < 11; i++) { assert(rep.stage[i].status == NVX_HWVAL_BLOCKED); assert(m.calls[i] == 0); }
    assert(m.teardown_calls == 1 && rep.stage[11].status == NVX_HWVAL_PASS);

    /* Failed teardown retains memory and fails the run. */
    memset(&m, 0, sizeof(m)); m.teardown_ok = 0;
    assert(run(&m, NVX_HWVAL_SOURCE_HARDWARE) == NVX_HWVAL_VERDICT_FAILED);
    assert(rep.memory_retained && rep.stage[11].status == NVX_HWVAL_FAIL);

    /* Missing stage -> NOT_IMPLEMENTED, rest BLOCKED, verdict INCOMPLETE. */
    memset(&m, 0, sizeof(m)); m.teardown_ok = 1;
    make_ops(&ops, &m); ops.stage[5] = 0; ops.not_implemented_reason[5] = "no RM device path";
    assert(nvx_hwval_run(&ops, NVX_HWVAL_SOURCE_HARDWARE, &rep) == NVX_HWVAL_VERDICT_INCOMPLETE);
    assert(rep.stage[5].status == NVX_HWVAL_NOT_IMPLEMENTED);
    assert(strcmp(rep.stage[5].reason, "no RM device path") == 0);
    assert(rep.stage[6].status == NVX_HWVAL_BLOCKED && rep.stage[10].status == NVX_HWVAL_BLOCKED);
    assert(rep.stage[11].status == NVX_HWVAL_PASS);   /* touched -> torn down */

    /* Hardware never touched: teardown not run, NOT_RUN. */
    memset(&m, 0, sizeof(m));
    make_ops(&ops, &m); ops.stage[0] = 0;
    assert(nvx_hwval_run(&ops, NVX_HWVAL_SOURCE_HARDWARE, &rep) == NVX_HWVAL_VERDICT_INCOMPLETE);
    assert(m.teardown_calls == 0 && rep.stage[11].status == NVX_HWVAL_NOT_RUN);

    /* Touched but no teardown op: fail closed. */
    memset(&m, 0, sizeof(m));
    make_ops(&ops, &m); ops.teardown = 0;
    assert(nvx_hwval_run(&ops, NVX_HWVAL_SOURCE_HARDWARE, &rep) == NVX_HWVAL_VERDICT_FAILED);
    assert(rep.memory_retained);

    /* PASS without evidence is downgraded. */
    memset(&m, 0, sizeof(m)); m.teardown_ok = 1; m.evidence_free_stage = 3;
    assert(run(&m, NVX_HWVAL_SOURCE_HARDWARE) == NVX_HWVAL_VERDICT_FAILED);
    assert(rep.stage[2].status == NVX_HWVAL_FAIL && rep.stage[2].downgraded);
    assert(rep.stage[3].status == NVX_HWVAL_BLOCKED);

    /* Invalid stage status is a failure. */
    memset(&m, 0, sizeof(m)); m.teardown_ok = 1; m.bad_status_stage = 2;
    assert(run(&m, NVX_HWVAL_SOURCE_HARDWARE) == NVX_HWVAL_VERDICT_FAILED);
    assert(rep.stage[1].status == NVX_HWVAL_FAIL);

    /* Stage 10: not GPU-written / stale completion rejected. */
    memset(&m, 0, sizeof(m)); m.teardown_ok = 1; m.no_gpu_flag = 1;
    assert(run(&m, NVX_HWVAL_SOURCE_HARDWARE) == NVX_HWVAL_VERDICT_FAILED);
    assert(rep.stage[9].downgraded && rep.stage[10].status == NVX_HWVAL_BLOCKED);
    memset(&m, 0, sizeof(m)); m.teardown_ok = 1; m.no_gpu_flag = 2;   /* CPU-written: flag present but 0 */
    assert(run(&m, NVX_HWVAL_SOURCE_HARDWARE) == NVX_HWVAL_VERDICT_FAILED);
    assert(rep.stage[9].downgraded);
    memset(&m, 0, sizeof(m)); m.teardown_ok = 1; m.stale_completion = 1;
    assert(run(&m, NVX_HWVAL_SOURCE_HARDWARE) == NVX_HWVAL_VERDICT_FAILED);
    assert(rep.stage[9].status == NVX_HWVAL_FAIL);

    /* Stage 11: mismatches or missing guard evidence rejected. */
    memset(&m, 0, sizeof(m)); m.teardown_ok = 1; m.mismatch = 1;
    assert(run(&m, NVX_HWVAL_SOURCE_HARDWARE) == NVX_HWVAL_VERDICT_FAILED);
    assert(rep.stage[10].downgraded);
    memset(&m, 0, sizeof(m)); m.teardown_ok = 1; m.guard_bad = 1;
    assert(run(&m, NVX_HWVAL_SOURCE_HARDWARE) == NVX_HWVAL_VERDICT_FAILED);

    /* Evidence overflow rejected. */
    {
        nvx_hwval_stage_record_t r; memset(&r, 0, sizeof(r));
        for (unsigned i = 0; i < NVX_HWVAL_MAX_EVIDENCE + 2U; i++) nvx_hwval_add_u64(&r, "k", i);
        assert(r.evidence_count == NVX_HWVAL_MAX_EVIDENCE && r.evidence_overflow);
        nvx_hwval_add_text(&r, 0, "x"); nvx_hwval_add_u64(0, "k", 1);
    }

    /* Structured output: one line per stage + verdict, sanitized, parseable. */
    memset(&m, 0, sizeof(m)); m.teardown_ok = 1; m.fail_stage = 2;
    make_ops(&ops, &m);
    assert(nvx_hwval_run(&ops, NVX_HWVAL_SOURCE_HARDWARE, &rep) == NVX_HWVAL_VERDICT_FAILED);
    nvx_hwval_add_text(&rep.stage[0], "fw", "a|b\nc");
    nlines = 0; nvx_hwval_emit_report(&rep, emit, 0);
    assert(nlines == 13);
    assert(strncmp(lines[0], "NVXHW|stage=0x1|name=discover|status=PASS", 41) == 0);
    assert(strstr(lines[0], "|fw=a_b_c"));
    assert(strstr(lines[1], "|status=FAIL|reason=mock failure|err=0x5"));
    assert(strstr(lines[2], "status=BLOCKED"));
    assert(strcmp(lines[12], "NVXHW|verdict=FAILED|source=HARDWARE|memory_retained=0") == 0);
    nvx_hwval_emit_report(0, emit, 0);

    /* Data verification helpers. */
    {
        static const uint32_t expect_sizes[] = { 4, 256, 4096, 4097, 65536, 1048576 };
        assert(nvx_hwval_copy_size_count == 6);
        for (size_t i = 0; i < 6; i++) assert(nvx_hwval_copy_sizes[i] == expect_sizes[i]);
        for (int p = 0; p < NVX_HWVAL_PATTERN_COUNT; p++) {
            for (size_t si = 0; si < nvx_hwval_copy_size_count; si++) {
                size_t n = nvx_hwval_copy_sizes[si];
                uint8_t *a = malloc(n + 64), *b = malloc(n + 64);
                assert(a && b);
                assert(nvx_hwval_fill_pattern(a, n, (nvx_hwval_pattern_t)p, 7) == 0);
                assert(nvx_hwval_fill_pattern(b, n, (nvx_hwval_pattern_t)p, 7) == 0);
                nvx_hwval_compare_t c;
                nvx_hwval_compare_buffers(a, b, n, &c);
                assert(c.valid && c.bytes_compared == n && c.mismatch_count == 0);
                /* Nontrivial: not constant, and seed changes the data. */
                int distinct = 0;
                for (size_t i = 1; i < n; i++) if (a[i] != a[0]) { distinct = 1; break; }
                if (n >= 256) assert(distinct);
                free(a); free(b);
            }
        }
        uint8_t x[64], y[64], z[64];
        nvx_hwval_fill_pattern(x, 64, NVX_HWVAL_PATTERN_PRNG, 1);
        nvx_hwval_fill_pattern(y, 64, NVX_HWVAL_PATTERN_PRNG, 2);
        nvx_hwval_fill_pattern(z, 64, NVX_HWVAL_PATTERN_INVERTED_PRNG, 1);
        assert(memcmp(x, y, 64) != 0);
        for (int i = 0; i < 64; i++) assert(z[i] == (uint8_t)~x[i]);
        assert(nvx_hwval_fill_pattern(x, 8, NVX_HWVAL_PATTERN_COUNT, 0) != 0);
        assert(nvx_hwval_fill_pattern(0, 8, NVX_HWVAL_PATTERN_PRNG, 0) != 0);

        /* Mismatch counting: every difference counted, first one recorded. */
        nvx_hwval_compare_t c;
        memcpy(y, x, 64); y[10] ^= 0x40; y[11] ^= 1; y[63] ^= 0xFF;
        nvx_hwval_compare_buffers(x, y, 64, &c);
        assert(c.mismatch_count == 3 && c.first_mismatch_offset == 10);
        assert(c.first_expected == x[10] && c.first_actual == y[10]);
        nvx_hwval_compare_buffers(0, y, 4, &c); assert(!c.valid);
        nvx_hwval_compare_buffers(0, 0, 0, &c); assert(c.valid && c.bytes_compared == 0);

        /* Sentinel and guard detection (writes past the destination). */
        uint8_t d[64];
        nvx_hwval_fill_sentinel(d, 64, 0xA5);
        nvx_hwval_verify_guard(d, 32, 32, 0xA5, &c); assert(c.valid && c.mismatch_count == 0);
        d[40] = 0; d[50] = 1;
        nvx_hwval_verify_guard(d, 32, 32, 0xA5, &c);
        assert(c.mismatch_count == 2 && c.first_mismatch_offset == 40 && c.first_actual == 0);
        nvx_hwval_verify_guard(0, 0, 4, 0xA5, &c); assert(!c.valid);
    }
    return 0;
}
