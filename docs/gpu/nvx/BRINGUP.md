# NVX manual bring-up (RTX 3060)

No firmware or GSP startup runs at boot. Boot performs read-only NVX PCI
discovery and a bounded PMC identity probe on the supported profile; it does
not reset a Falcon, load firmware, enable bus mastering, or submit GPU work.
The commands below are manual shell diagnostics.

| Command | What it does | Touches GPU |
| --- | --- | --- |
| `nvx status` | Finds the RTX 3060, maps BAR0, reads PMC identity registers, detaches. | Read-only |
| `nvx vbios` | Reads the VBIOS through the PROM window (BAR0+0x300000) using NVIDIA's IFR/PCI-ROM-chain logic, then locates the FWSEC image. FWSEC is not executed. | Read-only |
| `nvx fw <version>` | Loads the five GSP files from `/nvidia/ga106/gsp/`, verifies each against a `SHA256SUMS` you pinned in that directory, runs structural preflight. | No |

Put `gsp-<v>.bin`, `ucodes-<v>.bin`, `booter_load-<v>.bin`,
`booter_unload-<v>.bin`, `bootloader-<v>.bin` and a `SHA256SUMS`
(`sha256sum *.bin > SHA256SUMS`) in `/nvidia/ga106/gsp/`. A missing manifest or
a missing/mismatching entry fails closed. The manifest proves the file is the
one you pinned, not that NVIDIA signed it; the GPU's boot ROM authenticates
the signed images.

## GA10x WPR/FRTS/heap sizing (`nvx_gsp_wpr_policy`)

The layout planner existed but its policy inputs did not. They are now
transcribed from NVIDIA's open-gpu-kernel-modules and pinned by
`scripts/test_nvx_wpr_policy.c` (expected values derived independently):

- FRTS 1 MiB; first boot has no WPR end margin; non-WPR heap 1 MiB.
- Heap = 7 MiB carveout + 8 MiB base + 96 KiB/GB + 96 MiB client + 55 MiB ucode
  reclaim, clamped to >= 37 MiB and to the 256 MiB pre-scrubbed window
  (GA10x has no scrubber ucode). 12 GiB card: 168 MiB.
- MMU lock registers 0x1FA82C/0x1FA830 (value bits 31:4, shifted 12), readable
  only with PLM 0x1FA7C8 bit 0; VGA workspace register 0x625F04.

Known gaps (fail closed): `gpuFuseSupportsDisplay` is not read (a PRI-error
VGA register is treated as "no display"); NV_PRAMIN is assumed 1 MiB; no
recovery margin for retry boots.

## Not done yet

`nvx start` (GSP launch) is intentionally not a shell command yet. A callable
API now exists:
`nvx_gsp_ga10x_start_and_init_rm_from_pci()`. It composes the FWSEC/FRTS,
SEC2 Booter Load, RISC-V readiness, MSGQ link and GSP-RM initialization stages,
but no boot-time or shell path invokes it.

The platform coordinator core now exists as
`nvx_gsp_platform_start()`; see
[`PLATFORM_COORDINATOR.md`](PLATFORM_COORDINATOR.md). It owns VBIOS/FWSEC
extraction, five-component firmware acquisition, WPR planning, DMA queue and
init-argument construction, and startup sequencing.

The platform adapter is still missing. It must supply the trusted BAR handoff,
a firmware provider with the required trust policy, firmware-compatible
NVKV facts, queue sizes and delay callback. The coordinator itself checks the
PMC_BOOT_42 GA106 chip ID (`0x176`) before selecting firmware. Shutdown also requires a real SEC2/GSP quiesce implementation.
The current `nvx fw <version>` command only verifies the five firmware files
against the locally pinned SHA-256 manifest and runs structural preflight; it
does not start GSP or prove NVIDIA signature authentication.

There is no dedicated end-to-end startup test and no physical GA106 validation.
All current results are component-level host tests or structural evidence.
