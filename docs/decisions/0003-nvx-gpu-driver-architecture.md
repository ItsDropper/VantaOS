# ADR-003: Layered GPU driver architecture for NVX

- Status: proposed
- Date: 2026-10-19

## Context

VantaOS needs a path toward discrete GPU support without coupling its current
boot framebuffer and compositor to one vendor's register programming. Device
initialization, firmware, memory management, command submission and display
scanout are distinct problems and should be independently testable.

## Decision

Propose three source-ownership layers: generic GPU core, display contracts, and
the NVIDIA-specific NVX driver. Keep shared contracts in narrow public headers
and let per-layer Makefiles own implementation source lists. NVX consumes
existing PCI/MMIO/DMA/interrupt facilities rather than duplicating them.

Discovery must not be confused with working acceleration. Capabilities are
advertised only after implementation and validation exist. The boot framebuffer
remains the fallback when NVX is absent or fails.

## Alternatives considered

- Put NVIDIA register programming into the existing framebuffer renderer:
  rejected because it couples presentation to one device family and complicates
  failure recovery.
- Build a vendor-specific driver before defining generic interfaces: rejected
  because future drivers would inherit NVX assumptions.
- Import a Linux driver wholesale: rejected as the initial architecture because
  Linux kernel APIs differ from VantaOS and code reuse requires license and
  dependency review.

## Consequences

This establishes source ownership and proposed contracts without claiming that
the driver works. It adds build-manifest structure while leaving existing boot
and graphics behavior unchanged. Future changes must implement each layer, test
failure paths and keep hardware-support claims specific to verified devices.
