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
- **No complete platform coordinator:** the caller must supply a live supported
  PCI device, trusted GPU facts, mapped BAR0, staged VBIOS/FWSEC descriptor,
  hardware fuse version, DMA-backed boot artifacts, initialized ownership
  trackers, WPR metadata, framebuffer offset, init arguments, shared queue
  memory/layout, scratch buffers and scheduler-delay callback. The repository
  does not yet gather and validate all of these as one production operation.
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

Build one production platform coordinator that obtains the VBIOS/FWSEC and
version-matched firmware, establishes trusted BAR and framebuffer/WPR resources,
allocates and maps all DMA-visible memory, and invokes the existing startup
transaction. Add a dedicated host integration test around that coordinator and
the composed startup API, injecting failure at each stage and checking that
queue use stops and device-owned memory is retained until quiescence is proven.

After that passes, perform the first bounded physical GA106 experiment. Record
device identity, firmware hashes/versions, every startup phase, SEC2/FWSEC
result, GSP readiness, the first GSP-RM response, and a copy-engine operation
whose destination bytes are independently checked by the CPU.

Do not mark NVX initialized or advertise acceleration based on source presence,
ABI conformance, synthetic MMIO, or component-level CI alone.
