# Boot paths and bootloader roadmap

## Current state

VantaOS has two boot paths under active development:

- **GRUB/Multiboot:** the established ISO path. GRUB loads `kernel.bin`; `arch/x86_64/boot/boot.asm` establishes the initial CPU state and enters the kernel.
- **UEFI:** a separate EFI application at `boot/uefi/` loads the ELF64 kernel, gathers boot information, exits boot services, and transfers control through the UEFI kernel entry.

The UEFI path is built and exercised under QEMU/OVMF in CI. It is not yet a reason to remove the GRUB fallback or claim broad physical-firmware compatibility. The boot-information contract is defined in `include/vanta_boot_info.h`; the Multiboot and UEFI paths must normalize their handoff data to this contract.

## Current architecture

The boot path is responsible for parsing its own loader/firmware data and constructing normalized boot information. Kernel services should consume the versioned contract rather than independently parsing firmware-specific structures.

The contract currently carries memory-region and framebuffer metadata, kernel image bounds, optional firmware information, and reserved legacy metadata ranges. Version, size, pointer, and range validation are important because malformed handoff data can corrupt physical-memory bookkeeping.

## Remaining work

1. **Harden ELF parsing.** Add malformed-ELF regression cases for truncated headers, invalid program-header sizes, integer overflow, out-of-range segments, unsupported types, and overlapping load destinations.
2. **Expand handoff validation.** Test missing optional framebuffer/firmware data, malformed memory-map entries, and boundary/overflow cases.
3. **Reduce legacy dependencies.** Migrate remaining OS-facing consumers away from raw Multiboot structures so the normalized contract is the only general kernel interface.
4. **Exercise firmware variation.** Expand OVMF configurations and failure-path tests before considering the UEFI path a default.
5. **Preserve fallback behavior.** Keep the GRUB ISO independently tested until the UEFI path has enough regression evidence to justify any default-path change.

## Non-negotiable safety rules

- Keep the GRUB path bootable while the UEFI path matures.
- Validate all ELF ranges, arithmetic, alignments, physical-address limits, and destination overlaps before writing to memory.
- Do not release loader-owned memory until the kernel has copied or consumed its metadata.
- After a successful `ExitBootServices`, do not call UEFI boot-service functions.
- Keep serial diagnostics available before graphics initialization.
- Test in QEMU/OVMF before attempting physical hardware.
- Describe a test as passing only when its assertions and expected markers were observed.

## Verification

The current workflow builds the UEFI image and requires serial markers for loader start, ELF loading, `ExitBootServices`, kernel handoff, boot-info readiness, and selected kernel/APIC/SMP initialization. See [Testing and verification](TESTING.md).

Passing that workflow is evidence for the tested OVMF configuration only. It is not proof of compatibility with arbitrary firmware, Secure Boot, or physical machines.
