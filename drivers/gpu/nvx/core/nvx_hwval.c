/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_hwval.h"

#include <string.h>

/* ---- Names ---------------------------------------------------------------- */

static const char *const k_stage_names[NVX_HWVAL_STAGE_COUNT] = {
    "discover", "pci_resources", "safe_init", "firmware_launch", "gsp_rm",
    "rm_device", "gpu_memory", "copy_channel", "submit", "completion",
    "verify_data", "teardown"
};

const char *nvx_hwval_stage_name(unsigned n)
{
    return (n >= 1U && n <= NVX_HWVAL_STAGE_COUNT) ? k_stage_names[n - 1U] : "invalid";
}

const char *nvx_hwval_status_name(nvx_hwval_stage_status_t s)
{
    switch (s) {
    case NVX_HWVAL_NOT_RUN: return "NOT_RUN";
    case NVX_HWVAL_PASS: return "PASS";
    case NVX_HWVAL_FAIL: return "FAIL";
    case NVX_HWVAL_BLOCKED: return "BLOCKED";
    case NVX_HWVAL_NOT_IMPLEMENTED: return "NOT_IMPLEMENTED";
    }
    return "INVALID";
}

const char *nvx_hwval_verdict_name(nvx_hwval_verdict_t v)
{
    switch (v) {
    case NVX_HWVAL_VERDICT_INCOMPLETE: return "INCOMPLETE";
    case NVX_HWVAL_VERDICT_FAILED: return "FAILED";
    case NVX_HWVAL_VERDICT_HOST_ALL_PASS_NOT_HARDWARE: return "HOST_ALL_PASS_NOT_HARDWARE";
    case NVX_HWVAL_VERDICT_HARDWARE_VERIFIED: return "HARDWARE_VERIFIED";
    }
    return "INVALID";
}

/* ---- Evidence ------------------------------------------------------------- */

/* Bounded, always-terminated copy; the freestanding kernel has no strncpy. */
static void copy_text(char *dst, size_t cap, const char *src)
{
    size_t i = 0U;
    if (!dst || cap == 0U)
        return;
    for (; src && src[i] && i + 1U < cap; ++i)
        dst[i] = src[i];
    dst[i] = '\0';
}

static nvx_hwval_evidence_t *next_slot(nvx_hwval_stage_record_t *rec)
{
    if (!rec)
        return NULL;
    if (rec->evidence_count >= NVX_HWVAL_MAX_EVIDENCE) {
        rec->evidence_overflow = 1U;
        return NULL;
    }
    nvx_hwval_evidence_t *e = &rec->evidence[rec->evidence_count++];
    memset(e, 0, sizeof(*e));
    return e;
}

void nvx_hwval_add_u64(nvx_hwval_stage_record_t *rec, const char *key, uint64_t value)
{
    nvx_hwval_evidence_t *e = key ? next_slot(rec) : NULL;
    if (e) {
        e->key = key;
        e->value = value;
    }
}

void nvx_hwval_add_text(nvx_hwval_stage_record_t *rec, const char *key, const char *text)
{
    nvx_hwval_evidence_t *e = (key && text) ? next_slot(rec) : NULL;
    if (!e)
        return;
    e->key = key;
    e->is_text = 1U;
    copy_text(e->text, sizeof(e->text), text);
}

static int key_equal(const char *a, const char *b)
{
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

int nvx_hwval_find_u64(const nvx_hwval_stage_record_t *rec, const char *key, uint64_t *out)
{
    if (!rec || !key || !out)
        return 0;
    for (uint32_t i = 0U; i < rec->evidence_count; ++i) {
        const nvx_hwval_evidence_t *e = &rec->evidence[i];
        if (!e->is_text && e->key && key_equal(e->key, key)) {
            *out = e->value;
            return 1;
        }
    }
    return 0;
}

/* ---- Policy --------------------------------------------------------------- */

/* Returns NULL if the PASS claim is acceptable, otherwise a static reason. */
static const char *check_pass_policy(unsigned stage, const nvx_hwval_stage_record_t *r)
{
    uint64_t a, b, c;

    if (r->evidence_count == 0U)
        return "PASS reported without evidence";
    if (r->evidence_overflow)
        return "evidence overflow; record incomplete";

    if (stage == NVX_HWVAL_COMPLETION) {
        if (!nvx_hwval_find_u64(r, "completion_gpu_written", &a) || a != 1U)
            return "completion not flagged as GPU-written";
        if (!nvx_hwval_find_u64(r, "completion_expected", &a) ||
            !nvx_hwval_find_u64(r, "completion_observed", &b) || a != b)
            return "completion value missing or not equal to expected";
        if (!nvx_hwval_find_u64(r, "completion_initial", &c) || c == a)
            return "completion initial value missing or already equal to expected";
    } else if (stage == NVX_HWVAL_VERIFY_DATA) {
        if (!nvx_hwval_find_u64(r, "bytes_verified", &a) || a == 0U)
            return "no bytes verified";
        if (!nvx_hwval_find_u64(r, "mismatch_count", &a) || a != 0U)
            return "destination mismatch";
        if (!nvx_hwval_find_u64(r, "guard_mismatch_count", &a) || a != 0U)
            return "guard region missing or modified";
    }
    return NULL;
}

nvx_hwval_verdict_t nvx_hwval_run(
    const nvx_hwval_ops_t *ops, nvx_hwval_source_t source,
    nvx_hwval_report_t *out)
{
    int blocked = 0;
    int any_fail = 0;
    int all_pass = 1;

    if (!out)
        return NVX_HWVAL_VERDICT_FAILED;
    memset(out, 0, sizeof(*out));
    out->source = source;
    if (!ops) {
        out->verdict = NVX_HWVAL_VERDICT_FAILED;
        return out->verdict;
    }

    for (unsigned i = 0U; i < NVX_HWVAL_STAGE_COUNT - 1U; ++i) {
        nvx_hwval_stage_record_t *rec = &out->stage[i];

        if (blocked) {
            rec->status = NVX_HWVAL_BLOCKED;
            rec->reason = "an earlier stage did not pass";
            all_pass = 0;
            continue;
        }
        if (!ops->stage[i]) {
            rec->status = NVX_HWVAL_NOT_IMPLEMENTED;
            rec->reason = ops->not_implemented_reason[i] ?
                ops->not_implemented_reason[i] : "no implementation";
            blocked = 1;
            all_pass = 0;
            continue;
        }

        nvx_hwval_stage_status_t st = ops->stage[i](ops->context, rec);
        if (st == NVX_HWVAL_PASS) {
            const char *why = check_pass_policy(i + 1U, rec);
            if (why) {
                rec->downgraded = 1U;
                rec->reason = why;
                st = NVX_HWVAL_FAIL;
            }
        } else if (st != NVX_HWVAL_NOT_IMPLEMENTED) {
            if (st != NVX_HWVAL_FAIL && !rec->reason)
                rec->reason = "invalid stage status";
            st = NVX_HWVAL_FAIL;
        }
        rec->status = st;
        if (st != NVX_HWVAL_PASS) {
            blocked = 1;
            all_pass = 0;
            if (st == NVX_HWVAL_FAIL)
                any_fail = 1;
        }
    }

    /* Teardown always runs if hardware may have been touched. */
    {
        nvx_hwval_stage_record_t *rec = &out->stage[NVX_HWVAL_STAGE_COUNT - 1U];
        int touched = ops->hardware_touched ? ops->hardware_touched(ops->context) : 0;

        if (!touched) {
            rec->status = NVX_HWVAL_NOT_RUN;
            rec->reason = "hardware not touched; nothing to tear down";
            all_pass = 0;
        } else if (!ops->teardown) {
            rec->status = NVX_HWVAL_FAIL;
            rec->reason = "hardware touched but no teardown implemented";
            out->memory_retained = 1U;
            any_fail = 1;
            all_pass = 0;
        } else {
            int ok = ops->teardown(ops->context, rec);
            if (!ok) {
                rec->status = NVX_HWVAL_FAIL;
                if (!rec->reason)
                    rec->reason = "teardown did not establish quiescence";
                out->memory_retained = 1U;
                any_fail = 1;
                all_pass = 0;
            } else if (rec->evidence_count == 0U) {
                rec->status = NVX_HWVAL_FAIL;
                rec->downgraded = 1U;
                rec->reason = "PASS reported without evidence";
                out->memory_retained = 1U;
                any_fail = 1;
                all_pass = 0;
            } else {
                rec->status = NVX_HWVAL_PASS;
            }
        }
    }

    if (any_fail)
        out->verdict = NVX_HWVAL_VERDICT_FAILED;
    else if (all_pass)
        out->verdict = source == NVX_HWVAL_SOURCE_HARDWARE ?
            NVX_HWVAL_VERDICT_HARDWARE_VERIFIED :
            NVX_HWVAL_VERDICT_HOST_ALL_PASS_NOT_HARDWARE;
    else
        out->verdict = NVX_HWVAL_VERDICT_INCOMPLETE;
    return out->verdict;
}

/* ---- Structured output ---------------------------------------------------- */

#define LINE_MAX 640U

typedef struct line {
    char buf[LINE_MAX];
    size_t len;
    uint8_t truncated;
} line_t;

static void put_char(line_t *l, char c)
{
    if (l->len + 1U < LINE_MAX - 16U)   /* keep room for the truncation marker */
        l->buf[l->len++] = c;
    else
        l->truncated = 1U;
}

static void put_str(line_t *l, const char *s)
{
    while (*s)
        put_char(l, *s++);
}

static void put_clean(line_t *l, const char *s)
{
    for (; *s; ++s) {
        unsigned char c = (unsigned char)*s;
        put_char(l, (c < 0x20U || c == '|' || c == 0x7FU) ? '_' : (char)c);
    }
}

static void put_hex(line_t *l, uint64_t v)
{
    static const char d[] = "0123456789abcdef";
    int started = 0;
    put_str(l, "0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        unsigned n = (unsigned)((v >> shift) & 0xFU);
        if (n || started || shift == 0) {
            put_char(l, d[n]);
            started = 1;
        }
    }
}

static void flush_line(line_t *l, nvx_hwval_emit_fn emit, void *ctx)
{
    if (l->truncated) {
        l->len = l->len < LINE_MAX - 16U ? l->len : LINE_MAX - 16U;
        const char *m = "|truncated=1";
        while (*m)
            l->buf[l->len++] = *m++;
    }
    l->buf[l->len] = '\0';
    emit(l->buf, ctx);
}

void nvx_hwval_emit_report(const nvx_hwval_report_t *rep, nvx_hwval_emit_fn emit, void *ctx)
{
    if (!rep || !emit)
        return;
    for (unsigned i = 0U; i < NVX_HWVAL_STAGE_COUNT; ++i) {
        const nvx_hwval_stage_record_t *r = &rep->stage[i];
        line_t l = { {0}, 0U, 0U };

        put_str(&l, "NVXHW|stage=");
        put_hex(&l, i + 1U);
        put_str(&l, "|name=");
        put_str(&l, k_stage_names[i]);
        put_str(&l, "|status=");
        put_str(&l, nvx_hwval_status_name(r->status));
        if (r->reason) {
            put_str(&l, "|reason=");
            put_clean(&l, r->reason);
        }
        if (r->downgraded)
            put_str(&l, "|downgraded=1");
        if (r->evidence_overflow)
            put_str(&l, "|evidence_overflow=1");
        for (uint32_t k = 0U; k < r->evidence_count; ++k) {
            const nvx_hwval_evidence_t *e = &r->evidence[k];
            put_char(&l, '|');
            put_clean(&l, e->key);
            put_char(&l, '=');
            if (e->is_text)
                put_clean(&l, e->text);
            else
                put_hex(&l, e->value);
        }
        flush_line(&l, emit, ctx);
    }
    {
        line_t l = { {0}, 0U, 0U };
        put_str(&l, "NVXHW|verdict=");
        put_str(&l, nvx_hwval_verdict_name(rep->verdict));
        put_str(&l, "|source=");
        put_str(&l, rep->source == NVX_HWVAL_SOURCE_HARDWARE ? "HARDWARE" : "HOST_MOCK");
        put_str(&l, "|memory_retained=");
        put_char(&l, rep->memory_retained ? '1' : '0');
        flush_line(&l, emit, ctx);
    }
}

/* ---- Data verification ---------------------------------------------------- */

const uint32_t nvx_hwval_copy_sizes[] = { 4U, 256U, 4096U, 4097U, 65536U, 1048576U };
const size_t nvx_hwval_copy_size_count =
    sizeof(nvx_hwval_copy_sizes) / sizeof(nvx_hwval_copy_sizes[0]);

static uint64_t xorshift64star(uint64_t *s)
{
    uint64_t x = *s;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *s = x;
    return x * UINT64_C(0x2545F4914F6CDD1D);
}

static uint64_t splitmix64(uint64_t x)
{
    x += UINT64_C(0x9E3779B97F4A7C15);
    x = (x ^ (x >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94D049BB133111EB);
    return x ^ (x >> 31);
}

int nvx_hwval_fill_pattern(uint8_t *buf, size_t length,
                           nvx_hwval_pattern_t pattern, uint64_t seed)
{
    uint64_t state = seed ? seed : UINT64_C(0x1B873593A5A5A5A5);

    if (!buf || (unsigned)pattern >= (unsigned)NVX_HWVAL_PATTERN_COUNT)
        return 1;

    if (pattern == NVX_HWVAL_PATTERN_WALKING_ONES) {
        for (size_t i = 0U; i < length; ++i)
            buf[i] = (uint8_t)(1U << ((i + (size_t)(seed & 7U)) & 7U));
        return 0;
    }
    for (size_t i = 0U; i < length; i += 8U) {
        uint64_t w;
        if (pattern == NVX_HWVAL_PATTERN_ADDRESS)
            w = splitmix64((uint64_t)i ^ (seed << 1));
        else
            w = xorshift64star(&state);
        if (pattern == NVX_HWVAL_PATTERN_INVERTED_PRNG)
            w = ~w;
        for (size_t b = 0U; b < 8U && i + b < length; ++b)
            buf[i + b] = (uint8_t)(w >> (8U * b));
    }
    return 0;
}

void nvx_hwval_fill_sentinel(uint8_t *buf, size_t length, uint8_t sentinel)
{
    if (buf)
        memset(buf, sentinel, length);
}

void nvx_hwval_compare_buffers(const uint8_t *expected, const uint8_t *actual,
                               size_t length, nvx_hwval_compare_t *out)
{
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    if (length != 0U && (!expected || !actual))
        return;
    out->valid = 1U;
    out->bytes_compared = length;
    for (size_t i = 0U; i < length; ++i) {
        if (expected[i] != actual[i]) {
            if (out->mismatch_count == 0U) {
                out->first_mismatch_offset = i;
                out->first_expected = expected[i];
                out->first_actual = actual[i];
            }
            ++out->mismatch_count;
        }
    }
}

void nvx_hwval_verify_guard(const uint8_t *buf, size_t off, size_t len,
                            uint8_t sentinel, nvx_hwval_compare_t *out)
{
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    if (len != 0U && !buf)
        return;
    out->valid = 1U;
    out->bytes_compared = len;
    for (size_t i = 0U; i < len; ++i) {
        if (buf[off + i] != sentinel) {
            if (out->mismatch_count == 0U) {
                out->first_mismatch_offset = off + i;
                out->first_expected = sentinel;
                out->first_actual = buf[off + i];
            }
            ++out->mismatch_count;
        }
    }
}
