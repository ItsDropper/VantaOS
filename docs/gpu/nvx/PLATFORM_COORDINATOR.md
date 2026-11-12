# NVX GA10x platform coordinator

Status checked against `master` on 2026-11-12.

The coordinator core is implemented in:

- `include/nvx_gsp_platform.h`
- `drivers/gpu/nvx/gsp/nvx_gsp_platform.c`

It is the orchestration layer above the existing GA10x firmware and startup
components. It deliberately does not invent PCI resource sizes, GPU topology,
chip identity, firmware trust, framebuffer size, or shutdown success.

## Startup ownership and ordering

`nvx_gsp_platform_start()` performs the following transaction:

1. Validate the PCI candidate, supported GA10x profile, exact firmware chip
   token, version, firmware provider callbacks, trusted BAR0 extent, explicit
   GPU facts, queue sizes and bounded timeout configuration. This is a
   side-effect-free preflight.
2. Attach the PCI function using the caller's trusted BAR0 resource. The
   attach path maps BAR0 and performs the existing read-only PMC identity probe.
3. Require PMC_BOOT_42's exact GA106 chip ID (`0x176`). A broad GA10x PCI
   profile alone is not enough to select GA106 firmware; GA107 (`0x177`) and
   other Ampere chips are rejected.
4. Read the VBIOS from the PROM window into a page-backed DMA allocation and
   parse the production FWSEC descriptor.
5. Acquire all five same-version GA106 GSP components through the supplied
   `nvx_firmware_provider_t`, stage the runtime ELF/bootloader/signature
   artifacts, and stage the fuse-selected SEC2 booters.
6. Read the usable framebuffer size and GA10x WPR/MMU-lock facts from BAR0.
   Feed the real firmware artifact extents and those hardware facts into the
   GA10x WPR planner; materialize the resulting 256-byte metadata in its own
   DMA page.
7. Allocate the shared queue and init-argument pages, plan the queue layout,
   build and validate its self-mapping PTE table, and materialize the modern
   GA10x cached-init arguments.
8. Enable PCI bus mastering only after all required DMA allocations and
   preparation have completed.
9. Invoke `nvx_gsp_ga10x_start_and_init_rm_from_pci()`, which performs the
   existing FWSEC/FRTS, SEC2 Booter Load, RISC-V readiness, MSGQ/GMCAPI and
   GSP-RM initialization transaction.
10. Advance the NVX lifecycle to `INITIALIZED` only if both GSP-RM and RPC
    initialization report success. No 3D, compute, display or copy-engine
    capability is published by this coordinator.

The firmware provider's context must remain valid until session shutdown,
because its release callback is invoked during teardown.

The coordinator owns the firmware bundle, boot-memory artifacts, VBIOS DMA,
init-argument DMA, shared-queue DMA and all runtime queue/RPC objects in
`nvx_gsp_platform_session_t`. That session must live in static or other
long-lived kernel storage; it must not be allocated on a transient stack.

## Platform inputs that must be supplied

| Input | Required source / constraint |
| --- | --- |
| `trusted_bar0` | PCI/firmware resource manager with a known BAR extent large enough for the Falcon/GSP registers and the complete 1 MiB VBIOS PROM window. Do not size a live BAR by destructive probing while decode or bus mastering is active. |
| PMC chip identity | The coordinator pins GA106 to PMC_BOOT_42 chip ID `0x176` and rejects any other chip ID. The value follows the Linux Nova chipset table. |
| `firmware_provider` | Request, verify and release callbacks for the five exact-version firmware components. |
| Firmware verification | The provider's `verify` callback must enforce the platform's trust policy. A local SHA-256 manifest detects file drift but is not NVIDIA signature authentication. |
| `trusted_gpu_facts` | Only values actually collected and known to match the GSP firmware branch's NVKV ABI. PCI identity and host facts are collected by NVX itself. |
| Queue sizes | Explicit page-aligned command/status sizes for the selected GSP firmware ABI. The coordinator checks the current single-PTE-page limit and DMA allocation bound. |
| `delay_ms` / timeout | A scheduler-backed bounded delay callback and nonzero timeout/poll interval. Busy-wait loops are not substituted. |

The current GA10x WPR policy still has its documented assumptions and
limitations: first-boot margin only, the display-fuse path is not implemented,
and the PRAMIN size assumption remains under audit. The coordinator fails when
the register reads or WPR planner reject the inputs.

## Failure and shutdown semantics

- Failures before bus mastering/startup unwind acquired resources and detach
  the device.
- If PCI bus-master enable readback is ambiguous, the coordinator tries to
  disable bus mastering. If it cannot prove the bit is clear, it retains all
  allocations and returns `NVX_GSP_PLATFORM_BUSY`.
- Once the startup transaction is attempted, no DMA allocation is freed on
  failure or timeout. A partially running SEC2/GSP may still own memory.
- `nvx_gsp_platform_shutdown()` requires a platform quiesce callback after
  startup. That callback must stop both SEC2 and GSP and prove that neither can
  issue further DMA. Only then does NVX reclaim DMA ownership, disable bus
  mastering, detach BAR0 through `nvx_device_detach_quiesced()`, and release
  allocations. Generic lifecycle transitions cannot move `FAILED` to
  `QUIESCING`; the explicit recovery API requires the quiescence proof.
- The repository does not yet contain a universal GA10x SEC2/GSP stop and
  quiescence implementation. The callback is an explicit integration boundary,
  not a fake built-in shutdown.

## Current verification boundary

The NVX CI workflow compiles the coordinator with `-Wall -Wextra -Werror`,
tests its configuration preflight, and runs mocked orchestration tests for
successful startup, firmware/VBIOS/queue allocation failures, ambiguous
bus-master enable, startup failure retention, and quiesce-gated teardown.

These tests validate coordinator control flow and ownership policy. They do
not execute the composed startup against real SEC2/GSP registers and do not
prove the firmware ABI or hardware sequence on a physical RTX 3060.

There is still no boot-time or shell-command caller. The missing platform
adapter must obtain the trusted BAR resource, provide a firmware trust source
and truthful NVKV facts, and implement
a real SEC2/GSP quiesce operation. Until that adapter and physical test exist,
this is a production-oriented coordinator core, not a production-ready
accelerated GPU driver.
