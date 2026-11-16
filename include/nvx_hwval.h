/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_HWVAL_H
#define VANTA_NVX_HWVAL_H

#include <stddef.h>
#include <stdint.h>

/*
 * NVX hardware validation harness: the 12-stage sequence for the first real
 * RTX 3060 run. This file is the evidence-policy core; it has no hardware
 * access. Rules it enforces (host-tested):
 *
 *  - Stages run in order and stop at the first non-PASS; later stages are
 *    reported BLOCKED, never silently skipped or assumed.
 *  - A stage that reports PASS with no evidence is downgraded to FAIL. A
 *    register write or a non-timeout poll is not evidence.
 *  - Stage 10 passes only if a GPU-written completion value equals the value
 *    the CPU requested and was flagged as written by the GPU.
 *  - Stage 11 passes only if the CPU compared >0 bytes and found 0 mismatches.
 *  - Teardown (stage 12) runs whenever hardware may have been touched, even
 *    after a failure. A failed teardown is reported as memory retained.
 *  - The verdict is HARDWARE_VERIFIED only when the run's source is HARDWARE
 *    and all 12 stages PASS. Host tests use SOURCE_HOST_MOCK and can never
 *    produce it.
 */

#define NVX_HWVAL_STAGE_COUNT   12U
#define NVX_HWVAL_MAX_EVIDENCE  16U
#define NVX_HWVAL_TEXT_MAX      48U

typedef enum nvx_hwval_stage_id {
    NVX_HWVAL_DISCOVER = 1,        /* discover the physical GA106 */
    NVX_HWVAL_PCI_RESOURCES,       /* validate PCI resources */
    NVX_HWVAL_SAFE_INIT,           /* initialize the device safely */
    NVX_HWVAL_FIRMWARE_LAUNCH,     /* authenticate and launch firmware */
    NVX_HWVAL_GSP_RM,              /* establish GSP-RM communication */
    NVX_HWVAL_RM_DEVICE,           /* create an RM device (first independent RPC) */
    NVX_HWVAL_GPU_MEMORY,          /* allocate GPU-visible memory */
    NVX_HWVAL_COPY_CHANNEL,        /* create a copy channel */
    NVX_HWVAL_SUBMIT,              /* submit a GPU operation */
    NVX_HWVAL_COMPLETION,          /* observe GPU-produced completion */
    NVX_HWVAL_VERIFY_DATA,         /* verify the copied data independently */
    NVX_HWVAL_TEARDOWN             /* tear down the device safely */
} nvx_hwval_stage_id_t;

typedef enum nvx_hwval_stage_status {
    NVX_HWVAL_NOT_RUN = 0,
    NVX_HWVAL_PASS,
    NVX_HWVAL_FAIL,
    NVX_HWVAL_BLOCKED,             /* an earlier stage did not pass */
    NVX_HWVAL_NOT_IMPLEMENTED      /* no real implementation exists yet */
} nvx_hwval_stage_status_t;

typedef enum nvx_hwval_source {
    NVX_HWVAL_SOURCE_HOST_MOCK = 0,   /* synthetic; never hardware evidence */
    NVX_HWVAL_SOURCE_HARDWARE         /* set only by the kernel runner */
} nvx_hwval_source_t;

typedef enum nvx_hwval_verdict {
    NVX_HWVAL_VERDICT_INCOMPLETE = 0,
    NVX_HWVAL_VERDICT_FAILED,
    NVX_HWVAL_VERDICT_HOST_ALL_PASS_NOT_HARDWARE,
    NVX_HWVAL_VERDICT_HARDWARE_VERIFIED
} nvx_hwval_verdict_t;

/* Evidence: a static key plus either a number or a short text value. */
typedef struct nvx_hwval_evidence {
    const char *key;               /* must outlive the report (string literal) */
    uint64_t value;
    char text[NVX_HWVAL_TEXT_MAX]; /* used when is_text */
    uint8_t is_text;
} nvx_hwval_evidence_t;

typedef struct nvx_hwval_stage_record {
    nvx_hwval_stage_status_t status;
    nvx_hwval_evidence_t evidence[NVX_HWVAL_MAX_EVIDENCE];
    uint32_t evidence_count;
    uint8_t evidence_overflow;
    uint8_t downgraded;            /* PASS reported but rejected by policy */
    const char *reason;            /* static text for FAIL/NOT_IMPLEMENTED/BLOCKED */
} nvx_hwval_stage_record_t;

/* Stage implementations call these to record evidence. */
void nvx_hwval_add_u64(nvx_hwval_stage_record_t *rec, const char *key, uint64_t value);
void nvx_hwval_add_text(nvx_hwval_stage_record_t *rec, const char *key, const char *text);
int nvx_hwval_find_u64(const nvx_hwval_stage_record_t *rec, const char *key, uint64_t *out);

/*
 * A stage returns PASS, FAIL or NOT_IMPLEMENTED. Anything else is treated as
 * FAIL. It must add evidence for every claim it makes.
 */
typedef nvx_hwval_stage_status_t (*nvx_hwval_stage_fn)(
    void *context, nvx_hwval_stage_record_t *record);

/*
 * Teardown must be safe to call after any partial failure. Return nonzero only
 * when resources were released with hardware quiescence established.
 */
typedef int (*nvx_hwval_teardown_fn)(void *context, nvx_hwval_stage_record_t *record);

/* Returns nonzero if hardware may have been touched, so teardown must run. */
typedef int (*nvx_hwval_touched_fn)(void *context);

typedef struct nvx_hwval_ops {
    /* Index 0 = stage 1 ... index 10 = stage 11. NULL means NOT_IMPLEMENTED. */
    nvx_hwval_stage_fn stage[NVX_HWVAL_STAGE_COUNT - 1U];
    const char *not_implemented_reason[NVX_HWVAL_STAGE_COUNT - 1U];
    nvx_hwval_teardown_fn teardown;
    nvx_hwval_touched_fn hardware_touched;
    void *context;
} nvx_hwval_ops_t;

typedef struct nvx_hwval_report {
    nvx_hwval_source_t source;
    nvx_hwval_stage_record_t stage[NVX_HWVAL_STAGE_COUNT];   /* [0] = stage 1 */
    nvx_hwval_verdict_t verdict;
    uint8_t memory_retained;       /* teardown was needed and did not succeed */
} nvx_hwval_report_t;

const char *nvx_hwval_stage_name(unsigned stage_number);          /* 1..12 */
const char *nvx_hwval_status_name(nvx_hwval_stage_status_t status);
const char *nvx_hwval_verdict_name(nvx_hwval_verdict_t verdict);

/* Run the whole sequence. Returns the verdict (also stored in report). */
nvx_hwval_verdict_t nvx_hwval_run(
    const nvx_hwval_ops_t *ops, nvx_hwval_source_t source,
    nvx_hwval_report_t *out_report);

/*
 * Structured output, one machine-parseable line per stage plus a verdict line:
 *   NVXHW|stage=7|name=gpu_memory|status=PASS|key=0x..|key=text|...
 *   NVXHW|verdict=INCOMPLETE|source=HARDWARE|memory_retained=0
 * '|' and control characters in text are replaced with '_'.
 */
typedef void (*nvx_hwval_emit_fn)(const char *line, void *context);
void nvx_hwval_emit_report(const nvx_hwval_report_t *report,
                           nvx_hwval_emit_fn emit, void *context);

/* ---- Independent CPU-side data verification (stage 11) ------------------- */

typedef enum nvx_hwval_pattern {
    NVX_HWVAL_PATTERN_PRNG = 0,        /* xorshift64*, seeded */
    NVX_HWVAL_PATTERN_ADDRESS,         /* each aligned word encodes its offset */
    NVX_HWVAL_PATTERN_WALKING_ONES,    /* a single set bit that walks per byte */
    NVX_HWVAL_PATTERN_INVERTED_PRNG,   /* complement of PRNG stream */
    NVX_HWVAL_PATTERN_COUNT
} nvx_hwval_pattern_t;

/* Buffer sizes for the first copy test (bytes); includes non-page multiples. */
extern const uint32_t nvx_hwval_copy_sizes[];
extern const size_t nvx_hwval_copy_size_count;

/* Returns 0 on success, nonzero for a bad argument/pattern. */
int nvx_hwval_fill_pattern(uint8_t *buf, size_t length,
                           nvx_hwval_pattern_t pattern, uint64_t seed);
void nvx_hwval_fill_sentinel(uint8_t *buf, size_t length, uint8_t sentinel);

typedef struct nvx_hwval_compare {
    uint64_t bytes_compared;
    uint64_t mismatch_count;
    uint64_t first_mismatch_offset;
    uint8_t first_expected;
    uint8_t first_actual;
    uint8_t valid;                 /* argument check passed */
} nvx_hwval_compare_t;

/* Byte-exact compare of expected vs actual. Never short-circuits the count. */
void nvx_hwval_compare_buffers(const uint8_t *expected, const uint8_t *actual,
                               size_t length, nvx_hwval_compare_t *out);

/*
 * Verify that [guard_offset, guard_offset+guard_length) of buf still holds the
 * sentinel. Detects the GPU writing past the destination range.
 */
void nvx_hwval_verify_guard(const uint8_t *buf, size_t guard_offset,
                            size_t guard_length, uint8_t sentinel,
                            nvx_hwval_compare_t *out);

#endif
