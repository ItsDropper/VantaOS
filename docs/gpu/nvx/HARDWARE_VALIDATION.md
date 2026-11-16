# NVX hardware validation harness

Status: **host-tested policy core; kernel runner compile/link-checked; never executed on hardware.**
Source: `include/nvx_hwval.h`, `drivers/gpu/nvx/core/nvx_hwval.c` (policy, no hardware access),
`include/nvx_hwval_kernel.h`, `drivers/gpu/nvx/gsp/nvx_hwval_kernel.c` (kernel runner).
Test: `scripts/test_nvx_hwval.c` (mock stages, labelled HOST).

Run on the target with `nvx hwval <firmware-version>` (manual, opt-in). Capture the serial
log; every harness line starts with `NVXHW|`.

## The 12 stages and their current state

| # | Stage | Evidence recorded | State in the kernel runner |
| --- | --- | --- | --- |
| 1 | Discover the physical GA106 | PCI vendor/device/revision | Wired (adapter progress) |
| 2 | Validate PCI resources | BAR0 base/size | Wired |
| 3 | Initialize safely | handoff result and raw GSP/SEC2 registers, PCI command, coordinator status | Wired |
| 4 | Authenticate and launch firmware | firmware version, startup/bootstrap status, Booter mailbox | Wired (coordinator transaction) |
| 5 | GSP-RM communication | queue/GMCAPI/init status, init flags | Wired; the init RPC is **not** treated as the independent RPC validation |
| 6 | Create an RM device | - | **NOT_IMPLEMENTED** |
| 7 | Allocate GPU-visible memory | - | **NOT_IMPLEMENTED** |
| 8 | Create a copy channel | - | **NOT_IMPLEMENTED** |
| 9 | Submit a GPU operation | - | **NOT_IMPLEMENTED** |
| 10 | Observe GPU-produced completion | - | **NOT_IMPLEMENTED** |
| 11 | Verify copied data independently | - | **NOT_IMPLEMENTED** (CPU verifier exists) |
| 12 | Tear down safely | quiesce results, raw falcon registers | Wired; SEC2/GSP only |

Stages 1-5 come from one coordinator transaction; their per-stage outcome is read from the
adapter's recorded progress (`nvx_gsp_adapter_progress()`), not inferred. Stages 6-11 are
reported NOT_IMPLEMENTED with the specific blocker; the runner never substitutes mock results.

## Evidence policy (enforced and tested)

- Stages stop at the first non-PASS; later stages are BLOCKED.
- PASS without evidence is downgraded to FAIL (a register write is not evidence).
- Stage 10 needs `completion_gpu_written=1`, `completion_observed == completion_expected`,
  and `completion_initial != completion_expected` (rejects a stale or CPU-written value).
- Stage 11 needs `bytes_verified>0`, `mismatch_count=0` and `guard_mismatch_count=0`.
- Stage 12 runs whenever hardware may own DMA, even after a failure. If it cannot establish
  quiescence the report says `memory_retained=1` and DMA is not freed.
- `HARDWARE_VERIFIED` requires source `HARDWARE` and all 12 PASS. Host tests use `HOST_MOCK`
  and cannot produce it.

## Output format

```
NVXHW|env|build=<commit or unknown>|firmware_version=<v>|chip=ga106|target=RTX3060
NVXHW|stage=0x3|name=safe_init|status=PASS|handoff_result=0x0|...
NVXHW|stage=0x6|name=rm_device|status=NOT_IMPLEMENTED|reason=...
NVXHW|verdict=INCOMPLETE|source=HARDWARE|memory_retained=0
```

`build=unknown` unless the kernel was compiled with `-DVANTAOS_GIT_COMMIT=...`; record the
commit yourself when attaching a log.

## Stage 11 tooling (ready, unused on hardware)

`nvx_hwval_fill_pattern` (PRNG, address-derived, walking-ones, inverted PRNG),
`nvx_hwval_fill_sentinel`, `nvx_hwval_compare_buffers` (counts every mismatch, records the
first) and `nvx_hwval_verify_guard` (detects writes past the destination). Planned sizes:
4, 256, 4096, 4097, 65536 and 1048576 bytes.

## What this does not establish

Nothing here is hardware evidence until a log from a physical RTX 3060 exists with the exact
firmware version and commit. The firmware trust source is still a local hash manifest, NVKV
facts are empty, and no copy-engine, memory-visibility or channel path has been exercised.
