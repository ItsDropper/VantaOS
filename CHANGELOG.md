# Changelog

All notable changes to VantaOS are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). VantaOS is
pre-1.0 and has no stable ABI, so versions are not yet tagged; entries are
grouped under **Unreleased** until the first release.

## [Unreleased]

### Added
- NVX (NVIDIA GA10x) diagnostics: `nvx status|survey|vbios|fw`, VBIOS PROM
  reader, GSP firmware SHA-256 manifest verification, WPR sizing policy
  (reference-informed, unverified).
- NVX independence policy and a hardware-knowledge database with an analyzer
  for `nvx survey` logs.
- Documentation overhaul: index, getting started, glossary, style guide, NVX
  overview, VFIO passthrough testing guide, Markdown link checker and CI.
- Repository community files: changelog, support policy, code owners,
  Dependabot configuration, issue-template config.

### Fixed
- Real-hardware input and boot hangs (found by audit; verified under QEMU only):
  firmware ACPI tables above the 1 GiB identity map are now mapped on demand,
  so the MADT/IOAPIC are found; BSP LINT0 is programmed as ExtINT when IOAPIC
  routing is unavailable; a silent timer interrupt triggers automatic fallback
  to virtual-wire PIC delivery; IRQ stubs now EOI the correct controller;
  spurious 8259 IRQ7/IRQ15 have IDT gates; the PS/2 keyboard is sent
  set-defaults/enable-scanning; an unread auxiliary byte can no longer block
  the keyboard; the 8042 is polled every desktop pass; a failed controller
  config read is never written back; timeouts use a TSC-backed clock before
  interrupts are enabled.
- Literal `'\\0'` multi-character constants and `\\n` serial strings in the
  desktop diagnostics, Intel serial I/O driver, scheduler, FAT32 and network
  drivers.

### Changed
- GPU bring-up status consolidated into `docs/gpu/FRAMEWORK.md`.
- `.gitignore` covers disk images, logs, firmware and host-tool artifacts.

### Not yet established
- Any hardware-verified GPU bring-up or accelerated rendering. See the
  [status table](README.md#status).
