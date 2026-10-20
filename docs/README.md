# VantaOS documentation

Use this index to find the document that answers a specific engineering question. Feature availability is described conservatively: source presence is not the same as validated behavior.

## Understand the system

- [Desktop UI system](DESKTOP_UI.md) — shared visual rules, rendering/input ownership, and the current GUI process-model limitation.

- [Architecture overview](ARCHITECTURE.md) — boot sequence, subsystem responsibilities, kernel processes, early ring-3 execution, memory, VFS, and diagnostics.
- [User-mode programs](user-mode-programs.md) — current static ELF64 loader, initial stack, syscall interface, and explicit limitations.
- [Engineering model](ENGINEERING_MODEL.md) — evidence levels, subsystem contracts, concurrency review, and test layers.
- [Hardware support](HARDWARE_SUPPORT.md) — current hardware and emulator limitations.
- [Bootloader plan](BOOTLOADER_PLAN.md) — planned work toward an independent boot path; not a description of a completed bootloader.

- [Terminal shell](SHELL.md) — file operations, process commands, force-flag behavior, and current network CLI limits.

## Build and validate

- [Build system](BUILD_SYSTEM.md) — build targets, generated artifacts, and source manifests.
- [Testing](TESTING.md) — automated checks, regression markers, and what CI does and does not prove.

## Plan work

- [Development plan](DEVELOPMENT_PLAN.md) — current project priorities and staged goals.
- [Decision records](decisions/README.md) — rationale for changes that affect long-term architecture.

## Contribute and report issues

- [Contributing guide](../CONTRIBUTING.md) — branch workflow, engineering expectations, validation, and pull-request checklist.
- [Security policy](../SECURITY.md) — security-related reporting and scope.

When a document conflicts with actual code or CI, treat the implementation and reproducible tests as evidence, correct the documentation, and record unresolved discrepancies instead of silently assuming either is right.


## GPU driver framework

The GPU subsystem is split into vendor-neutral PCI classification and lifecycle contracts, with independent NVIDIA (NVX), Intel, and AMD backends. `gpu_classify_pci_function()` performs read-only PCI identity classification and only labels a generation when a curated device-ID entry exists. Unknown device IDs from supported vendors remain valid GPU candidates with an unknown architecture.

The Intel and AMD modules currently provide matching, descriptor reporting, and lifecycle-operation contracts only. Their initialization callbacks intentionally fail closed until the vendor firmware, memory-management, engine, interrupt, and command-submission paths are implemented. No hardware acceleration is advertised by these initial backends. Generic capabilities can only be published while a device is in the running state, and are cleared when it leaves that state.

Host verification: `scripts/test_gpu_vendor_framework.c`, compiled with `-std=c11 -Wall -Wextra -Werror` by the NVX/GPU framework workflow. This validates identity classification, audio-function rejection, unknown-ID handling, vendor driver matching, legal lifecycle transitions, and capability publication guards; it is not a hardware test.
