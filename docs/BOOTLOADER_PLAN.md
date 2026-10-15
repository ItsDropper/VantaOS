# VantaOS Bootloader Plan

## Current state

VantaOS does **not** have its own bootloader yet. The current boot path is:

1. GRUB loads the Multiboot-compatible `kernel.bin`.
2. `arch/x86_64/boot/boot.asm` receives the Multiboot 32-bit entry, creates an initial identity map, enables long mode, and calls `kernel_main`.
3. The kernel consumes the Multiboot information structure for the memory map and framebuffer.

Keep this path working until the replacement boots reliably. Do not replace GRUB in-place before the new path passes the same automated checks.

## Target: Vanta Boot Manager (UEFI x86-64 first)

Start with a UEFI application, because that is the standard boot path on modern x86-64 PCs. Treat legacy BIOS support as a later compatibility project, not a requirement for the first milestone.

The custom loader must own the handoff to the kernel; it must not quietly chain back into GRUB.

### Required stages

1. **Define a stable boot contract.** Add a versioned, fixed-width `vanta_boot_info` structure that describes the usable/reserved physical-memory ranges, framebuffer, kernel image bounds, ACPI RSDP address when available, and loader-reclaimable memory. Avoid passing GRUB/Multiboot structures directly into the long-term kernel API.
2. **Separate boot entry paths.** Keep the current Multiboot entry as a fallback and add a distinct x86-64 UEFI entry that begins in the firmware's 64-bit execution environment. Both paths should normalize their inputs into the same kernel boot contract.
3. **Load the kernel safely.** Parse ELF64 program headers, validate every offset/size/address calculation, reject unsupported segments and overlapping destinations, allocate backing pages, zero each segment's BSS, and never trust unchecked file metadata.
4. **Collect firmware information.** Read the UEFI memory map, GOP framebuffer information, and ACPI configuration-table pointer. Keep boot-services allocations reserved until the kernel has consumed the handoff.
5. **Exit boot services correctly.** Obtain the final memory map and its key, call `ExitBootServices`, and retry map acquisition if the key becomes stale. After success, do not call UEFI boot-service functions.
6. **Transfer control.** Pass the versioned boot-info pointer using a documented x86-64 calling convention, establish only the mappings required by the entry contract, and jump to the kernel entry point.
7. **Test both paths.** CI must continue to test the GRUB fallback and add OVMF/QEMU tests for the UEFI loader. Check malformed ELF inputs, memory-map boundaries, missing framebuffer, and loader failures as well as the successful boot marker.

## Non-negotiable safety rules

- Keep the current GRUB ISO bootable until the custom path is proven.
- Do not free loader memory before the kernel has copied or consumed its metadata.
- Validate all ELF ranges, integer additions, alignments, physical-address limits, and destination overlaps before writing memory.
- Make the boot-info structure versioned and size-tagged so future fields can be added safely.
- Keep serial diagnostics available before graphics initialization.
- Test the loader in QEMU/OVMF before trying it on physical hardware.
- Never report the custom loader as working until CI proves it reaches the kernel under OVMF.

## First implementation sequence

1. Stabilize physical-frame allocator accounting and add regression probes.
2. Introduce the versioned boot-info header and a Multiboot-to-boot-info adapter without changing the existing boot path.
3. Add the UEFI entry and ELF loader as a separate build target.
4. Add OVMF CI and only then consider making the custom loader the default.

This sequence deliberately avoids mixing a new firmware handoff with untested memory-management changes in one risky switch.
