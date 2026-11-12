# NVX GA10x GSP startup: current integration gaps

**Status checked against `master` on 2026-11-12.** The old next-step description
is obsolete: NVX now has a callable composed startup transaction. The remaining
gap is platform integration, end-to-end verification and physical hardware
evidence—not the absence of a top-level sequence.

## Implemented callable sequence

The PCI-facing entry point is
`nvx_gsp_ga10x_start_and_init_rm_from_pci()` in
`drivers/gpu/nvx/gsp/nvx_gsp_startup_pci.c`. It rejects unsupported PCI
profiles, collects/encodes system information from supplied trusted platform
facts, then calls `nvx_gsp_ga10x_start_and_init_rm()`.

The composed startup routine in `nvx_gsp_startup.c` performs:

1. Prepare the GA10x MSGQ command queue.
2. Call `nvx_gsp_ga10x_bootstrap_traced()`.
3. Execute the FWSEC/FRTS stage using the supplied staged VBIOS and parsed
   FWSEC descriptor/fuse version.
4. Prepare the RISC-V boot artifacts and shared queue.
5. Execute the SEC2 Booter Load transaction with phase tracing and bounded
   timeout/recovery.
6. Wait for the GSP RISC-V active indication.
7. Link the MSGQ status queue.
8. Initialize GMCAPI and issue the GSP-RM initialization RPC.
9. Initialize the MSGQ RPC transport and RPC transaction manager.

The lower-level code has dedicated host tests for firmware parsing/staging,
FWSEC/SEC2 transaction behavior, boot-memory ownership, queue framing and
linking, GMCAPI/RPC behavior, and timeout/recovery paths.

## What is still missing

- **No production caller:** repository search finds the startup entry point's
  declaration and definition, but no boot-time or shell-command call site.
  `nvx status`, `nvx vbios`, and `nvx fw` remain diagnostic/preflight
  commands; the firmware command explicitly reports that it has not started GSP.
- **No platform adapter/caller:** `nvx_gsp_platform_start()` now implements
  the orchestration core, including VBIOS extraction, five-component firmware
  acquisition, SEC2 booter staging, WPR planning/materialization, queue/PTE and
  init-argument allocation, bus-master gating, and invocation of the composed
  startup transaction. It still requires a platform-supplied trusted BAR
  resource, trusted firmware provider, firmware-compatible NVKV facts, queue
  sizing and delay callback. The coordinator itself pins GA106 to PMC_BOOT_42
  chip ID `0x176` and rejects other Ampere chips. No boot or
  shell path currently supplies those inputs.
- **BAR handoff is unresolved:** the generic BAR sizing path refuses to probe
  while PCI decode or bus mastering is enabled. A live firmware-driven GPU
  therefore needs a trusted resource-size/handoff contract; current automatic
  discovery cannot safely size and attach every active device.
- **No end-to-end startup test:** the constituent components have host tests,
  but there is no dedicated test that invokes
  `nvx_gsp_ga10x_start_and_init_rm_from_pci()` through every phase with
  fault injection and verifies complete rollback/retained ownership.
- **No physical GA106 evidence:** no authenticated SEC2/FWSEC result, stable
  RISC-V-ready state, GSP-RM RPC round trip, RM memory object, GPU-visible
  mapping, or GPU-produced copy has been independently verified on an RTX 3060.
- **RPC coverage is bounded:** MSGQ records are limited to the implemented
  16-page format until the continuation-record payload schema is added; broad
  NVIDIA RPC function schemas and the complete runtime device-service surface
  remain incomplete.

## Required next milestone

Build the platform adapter around the coordinator: obtain BAR0 extent from the
trusted PCI/firmware resource manager; provide a firmware
provider whose verification callback enforces the platform trust policy; and
populate only NVKV facts actually collected for the selected GSP ABI. Implement
a real SEC2/GSP quiesce operation rather than treating a timeout as proof of
shutdown.

The coordinator now has mocked CI coverage for successful orchestration,
pre-start rollback, startup failure retention and quiesce-gated teardown. The
remaining host-test gap is a dedicated integration test of the real composed
startup transaction with register/firmware fault injection and complete
rollback checks.

After that passes, perform the first bounded physical GA106 experiment. Record
device identity, firmware hashes/versions, every startup phase, SEC2/FWSEC
result, GSP readiness, the first GSP-RM response, and a copy-engine operation
whose destination bytes are independently checked by the CPU.

Do not mark NVX initialized or advertise acceleration based on source presence,
ABI conformance, synthetic MMIO, or component-level CI alone.
