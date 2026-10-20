# NVX SEC2 transaction evidence trace

The SEC2 Booter Load entry point now has a traced variant,
`nvx_gsp_ga10x_execute_booter_load_traced()`. It executes the same MMIO/DMA
path as the compatibility entry point and reports software-observed milestones:

- current phase and last phase whose operation returned success;
- the phase associated with the returned error (`NONE` when the transaction succeeds);
- the returned status, the status that triggered recovery, and the recovery result separately;
- whether reset-based recovery was attempted and whether reset/reclamation
  reported success;
- whether SEC2 quiescence was confirmed by the reset/boot helper.

The trace intentionally starts with `NONE`; preflight failure must not look like
a completed hardware phase. The executor marks each transfer/ownership handoff
only after its helper succeeds. A failure records the phase in progress rather
than claiming that phase completed.

This is a small original observability contract for NVX: the same trace shape
can be logged over serial or included in a future crash report without parsing
human-readable strings. It is particularly useful when a physical attempt stops
at an unknown firmware-generation boundary. It does not change the secure-boot
protocol or add undocumented register assumptions.

**Evidence limits:** host tests validate phase bookkeeping against synthetic
BAR memory. A trace of `COMPLETE` means the software helper sequence returned
success for the supplied inputs; with synthetic MMIO it is not evidence that
the real SEC2 BROM authenticated a booter, that FWSEC established WPR, or that
the GSP runtime launched. Hardware diagnostics must record the source of the
BAR mapping and firmware identity separately.


## Fault-injection coverage

The synthetic executor test now deliberately leaves the SEC2 DMA command busy until the configured timeout. The expected result is a DMA timeout attributed to `UPLOAD_IMEM`; the previous completed phase remains the booter DMA ownership handoff. The test then checks that reset/reclaim was attempted, that the synthetic reset succeeded, and that ownership returns to the CPU only after recovery.

The DMEM-upload failure branch also records its reset/reclaim result. A successful transaction reports `failed_phase = NONE`, rather than mislabeling the `COMPLETE` phase as a failure.

These tests exercise NVX's software response to a modeled fault. They do not simulate SEC2 hardware or prove that real DMA commands time out or reset exactly this way.


## Preserve the root cause when recovery also fails

A timeout followed by failed reset/reclamation has two distinct outcomes: the initiating phase error and the reason the transaction finally returned. The trace now preserves both as `recovery_trigger_status` and `recovery_result`, while `result` remains the public API's final return status.

For example, the synthetic IMEM-timeout case can report:
- `recovery_trigger_status = DMA_TIMEOUT`;
- `recovery_result = OK`;
- `result = DMA_TIMEOUT`, because recovery restored ownership but the original transfer still failed.

If reset/reclamation fails, the trace preserves `recovery_trigger_status = DMA_TIMEOUT` while `recovery_result` and final `result` report the ownership/recovery failure. This prevents recovery from hiding the initiating failure during later serial-log analysis.
