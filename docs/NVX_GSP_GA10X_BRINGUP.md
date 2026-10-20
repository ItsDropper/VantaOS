# NVX GA10x GSP Falcon bring-up notes

This note records the next hardware boundary for Ampere GA10x (including the
RTX 3060 GA106 family). It separates register observations from claims about
firmware execution.

## Independently checked register facts

NVX's GA10x read-only inspector uses the following BAR0-relative locations:

| Register | BAR0 offset | Purpose |
| --- | ---: | --- |
| GSP Falcon mailbox 0 | `0x110040` | Firmware-to-host status mailbox |
| GSP Falcon mailbox 1 | `0x110044` | Secondary firmware mailbox |
| GSP Falcon HWCFG2 | `0x1100F4` | Reset-ready, memory-scrub, and RISC-V capability bits |
| GSP Falcon CPUCTL | `0x110100` | Falcon halted/start control state |
| GSP RISC-V CPUCTL | `0x111388` | RISC-V active/halted state |

The register locations and decoded bits were cross-checked against the public
Linux Nova register definitions:
https://github.com/torvalds/linux/blob/master/drivers/gpu/nova-core/regs.rs

The GSP Falcon windows are based at `0x110000` and `0x111000`, respectively:
https://github.com/torvalds/linux/blob/master/drivers/gpu/nova-core/falcon/gsp.rs

Linux Nova documents the `0xBADF41xx` read pattern as evidence that the GSP
PRIV target is still masked. NVX masks off the varying low byte and refuses to
initialize queue state when that pattern is observed. It does not attempt to
bypass the mask or write any register during this check:
https://github.com/torvalds/linux/blob/master/drivers/gpu/nova-core/falcon/gsp.rs

## Implemented in NVX

- `nvx_gsp_ga10x_inspect_falcon()` performs bounded, read-only MMIO reads and
  returns raw register values plus decoded reset-ready, memory-scrubbing,
  Falcon-halted/start-requested, RISC-V-present/active/halted, and mailbox
  suspended fields.
- It rejects truncated mappings and output objects that alias the BAR mapping.
- GA10x queue setup now requires the entire diagnostic register range to be
  mapped and refuses to mark the queue transport ready when the PRIV target is
  still masked.
- `nvx_gsp_ga10x_wait_riscv_active()` polls the actual RISC-V active bit with a
  caller-supplied scheduler delay, checks the PRIV lock on each iteration, and
  returns a distinct timeout rather than spinning forever.
- Host tests cover decoded bitfields, the locked-window signature, short BARs,
  null arguments, and alias rejection.

## Hardware-derived firmware fuse selection

NVX now reads the GA102+ FPF ucode-version arrays through the bounded BAR0
MMIO accessor. Register offsets are `0x824100` (NVDEC), `0x824140` (SEC2), and
`0x8241C0` (GSP); each array contains 16 32-bit slots, with the fuse bitmap in
bits 15:0. The returned version is the bitmap's bit length (highest set bit
plus one), matching the GA102 HAL/Nova logic; it is not the raw register value.
Engine masks must match one supported value exactly and ucode IDs must be in
the documented 1..16 range. Short mappings, unknown engine masks, invalid IDs,
and nonzero reserved bits fail closed.

`nvx_gsp_preflight_stage_booters_from_bar0()` connects this hardware read to the
actual booter staging path: it validates the bundle first, takes each booter's
engine mask and ucode ID from parsed firmware metadata, reads the matching
fuse version, then stages both payloads. The older explicit-version staging API
remains available for offline tests and tooling; it should not be used as the
physical-hardware path.

Register definitions and the bit-length conversion were cross-checked against
the [Linux Nova GA102 Falcon HAL](https://kernel.googlesource.com/pub/scm/linux/kernel/git/next/linux-next/+/2f03a3fe84216541ed29f9b7bc014acb5f1b676b/drivers/gpu/nova-core/falcon/hal/ga102.rs),
the [Nova fuse register definitions](https://lists.openwall.net/linux-kernel/2026/08/17/1877),
and [NVIDIA's GA102 GSP HAL](https://github.com/NVIDIA/open-gpu-kernel-modules/blob/main/src/nvidia/src/kernel/gpu/gsp/arch/ampere/kernel_gsp_ga102.c).
The host tests exercise register bounds, engine/ucode rejection, bit-length
conversion, reserved-bit rejection, and no-staging-on-fuse-read-failure. These
tests use synthetic memory; they do not validate physical GPU fuse values.
## Booter payload staging

The public NVIDIA firmware extraction script documents the signed booter container
layout: a 24-byte outer header, a version-2 secure-load header, a packed signature
array, patch-location/signature metadata, patch metadata, signature count, load
descriptor, and firmware payload. The Linux NVIDIA open module selects the
signature with index `num_sigs - 1 - fuse_version` (when multiple signatures
exist) and rejects a nonzero `patch_sig` for this format.

The separate `bootloader-<version>.bin` is now parsed as the GA10x NVFW container too: version 1, a 24-byte outer header, an 84-byte descriptor, payload offset 108, and a 256-byte-aligned total size. Its payload can be copied into private staging, and the preflight report stores descriptor offsets rather than pointers that could outlive the verified bundle.\n\nNVX now implements that data path in `nvx_gsp_image_prepare()`: it copies only the
firmware payload into private staging, selects the fuse-indexed signature, and
patches it at the payload-relative location. The integrated
`nvx_gsp_preflight_stage_booters()` path validates the whole bundle, prechecks
both fuse versions/capacities/aliasing, and stages load plus unload images as a
single fail-closed operation. It rejects unsupported fuse
versions, truncated staging buffers, input/output aliasing, and signatures that
would extend outside the payload's DMEM range. Host tests cover both signature
orders and ensure failure leaves the staging buffer untouched.

Research references:
- https://github.com/NVIDIA/open-gpu-kernel-modules/blob/main/nouveau/extract-firmware-nouveau.py
- https://github.com/NVIDIA/open-gpu-kernel-modules/blob/main/src/nvidia/src/kernel/gpu/gsp/kernel_gsp_booter.c

The parser also enforces the actual v1 layout documented by NVIDIA's extraction script: signature array immediately after the secure-load header, patch metadata and count immediately after the signature array, exactly one application descriptor, and payload immediately after the descriptor. It rejects unsupported container versions, nonzero patch_sig, invalid app layout, and unaligned outer sizes.\n\nThis is signature **selection and placement**, not cryptographic authentication
and not a firmware launch. The hardware SEC2/FWSEC fuse-version read and actual
DMA upload/boot sequence are still missing.

## What this does not mean

A readable Falcon register window does not mean the GSP is running. These
checks do not upload firmware, authenticate NVIDIA's signed payloads, configure
the secure boot chain, allocate and map the GSP's firmware working set, build
the real firmware WPR layout, start the RISC-V core, or receive a genuine GSP
RPC. The queue integration test uses host memory and a fake BAR; it cannot prove
physical GPU support.

The next implementation step is the **GA10x-specific SEC2/FWSEC and GSP boot
sequence**, with verified register definitions, firmware-specific signing and
patch rules, a physically DMA-visible working set, WPR metadata, and bounded
polling of real hardware status. Do not substitute guessed register writes or
a fake "boot succeeded" flag for these requirements. NVIDIA's open kernel
module implementation and the independent Nova driver are research references;
their GPL-covered implementation code must not be copied into NVX without a
compatible licensing decision.


## Runtime ELF materialization (host-verified, not hardware boot)

The new `nvx_gsp_elf_load_segments()` path reparses the acquired runtime ELF,
rejects ET_DYN until a relocation policy exists, resolves every PT_LOAD target
through a platform-supplied mapping callback, rejects overlapping physical and
CPU mappings, and validates every mapping before it writes anything. It copies
file-backed bytes and zeroes each segment's BSS tail. The callback contract
requires the caller to map only memory reserved and addressable by the GSP; the
loader does not allocate memory, alter PCI bus mastering, or start the engine.

`nvx_gsp_preflight_load_runtime()` joins this operation to full GA10x firmware
bundle preflight. This makes segment materialization callable only after the
bundle and its firmware assets have passed the existing preflight checks. Host
tests cover data copy, BSS clearing, destination alias rejection, unsupported
ET_DYN, and no-write behavior when mapping fails.

This still is **not a complete boot path**. No GA10x secure-boot/SEC2 FWSEC
transaction, firmware-authenticated WPR placement, VRAM-resident WPR allocation,
Falcon DMA transfer, reset/boot register sequence, or GSP-RM init RPC is wired
to physical hardware. The loader's map callback must not be mistaken for a
real BAR1/VRAM mapping implementation. Do not mark the device initialized or
enable acceleration based on these host tests.


## System-memory boot artifacts and WPR handoff

`nvx_gsp_boot_memory_acquire()` now performs full bundle preflight before allocating
three distinct physically contiguous DMA buffers for the RISC-V runtime ELF,
the extracted GA10x bootloader payload, and the runtime's `.fwsignature_ga10x`
section. It copies only validated source ranges, checks physical ranges for
overlap, and unwinds every allocation if any later allocation or copy fails.
`nvx_gsp_boot_memory_bind_wpr()` transfers the actual DMA physical addresses
and exact byte lengths into a caller-owned WPR layout while preserving its
framebuffer offsets.

This closes the CPU-side SYSRAM artifact allocation/binding step; it does not
supply the missing VRAM/WPR allocator or bootloader descriptor semantics. The
platform still must derive the code/data/manifest offsets from the validated
GA10x descriptor, plan non-overlapping framebuffer regions against real FB
size, build and validate the 256-byte WPR metadata, perform the vendor-defined
SEC2/FWSEC secure boot flow, launch the GSP, and complete the first real GSP-RM
RPC. The allocator never enables PCI bus mastering or writes a boot register.


## WPR layout planning

Added `nvx_gsp_wpr_layout_plan()`, which applies the GA10x/Turing-derived WPR placement arithmetic with explicit device/platform inputs: usable framebuffer size, VBIOS workspace offset, optional MMU-lock lower bound, WPR end margin, FRTS size, and non-WPR heap requirement. It aligns the WPR end to 128 KiB, the boot image to 4 KiB, the runtime ELF to 64 KiB, and heap/meta regions to 1 MiB; it validates the completed layout before publishing it. WPR heap sizing is delegated to a required platform policy callback because that sizing depends on VantaOS memory-manager policy and firmware requirements.

This avoids inventing VRAM offsets. It does **not** discover the VBIOS workspace, read the MMU lock, determine usable framebuffer size, implement the platform heap policy, parse the bootloader's monitor-code/data/manifest offsets, or write metadata into actual VRAM. Those inputs must come from real hardware/platform integration before secure boot is safe.
