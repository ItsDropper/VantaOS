# Hardware support and limitations

VantaOS is an experimental x86-64 OS. Source code for a driver is not proof that the driver works on all devices using that class of hardware.

## Current verification

| Area | Current evidence | Status |
| --- | --- | --- |
| x86-64 boot path | Kernel/ISO build and headless QEMU boot in CI | Verified in the tested QEMU configuration |
| Serial diagnostics | CI captures COM1 output and checks boot/test markers | Verified in QEMU |
| Timer/interrupt path | PIT frequency bounds, tick wrap arithmetic, scheduler probe and process reaping are covered by regression tests | Narrow path tested; not exhaustive |
| Kernel watchdog | Fixed-size watchdog registry, heartbeat API, latched expiry, stale-handle rejection and tick-wrap behavior have host regression tests wired into CI | No production subsystem client or safe recovery observer is registered yet; software expiry only, no hardware watchdog |
| Graphics/framebuffer | Initialization code exists and boot logs report framebuffer details | Pixel correctness and broad firmware support unverified |
| Intel UHD 620 identification | Intel PCI ID 8086:5917 is classified as Kaby Lake Gen9.5; the Skylake-only Gen9 runtime is explicitly not started for this device | Read-only identity only; no UHD 620 display modesetting, acceleration, power management, or physical validation |
| Keyboard/mouse | Driver code exists | Device compatibility and full interaction unverified |
| AHCI/SATA groundwork | Host-tested PRDT builder and ABAR validator; read-only PCI discovery probes CAP/PI only for controllers whose I/O decode, memory decode, and bus mastering are disabled | Not hardware support; active-controller handoff, HBA startup, DMA command engine, interrupts, recovery, and block-device integration remain unimplemented |
| PCI/legacy ATA PIO | IDE-compatible primary/secondary channel probing, bounded LBA28 reads/writes, and shared host-tested request-range validation | Physical controller compatibility and broad error-recovery behavior unverified; this is not an AHCI/SATA driver |
| PCIe extended configuration | ACPI MCFG region validation and read-only extended-capability traversal are implemented; malformed links are host-tested | Real ECAM firmware tables and physical PCIe devices are not yet hardware-validated |
| Hardware identification | CPU capability/topology reporting, PCI BDF/vendor/device/class/revision/subsystem inventory, checksummed ACPI root-table inventory, SMBIOS 2.x/3.x system/board/BIOS/CPU/memory inventory, and USB device VID:PID/class logging | Software paths and synthetic SMBIOS parser tests; physical firmware tables and laptop USB devices still require device testing. SMBIOS tables above the current 1 GiB identity-map window are intentionally not dereferenced |
| FAT32 | Mount validates BPB geometry against partition bounds; cluster and FAT accesses are volume-bounded; ATA PIO sector-write primitive is implemented | Persistent allocation, directory updates, and file write/create remain unimplemented; write path is not end-to-end tested |
| USB | xHCI host path and USB HID support have QEMU regression coverage; Bluetooth HCI command state has a bounded software watchdog; RTL8188EU USB Wi-Fi driver code has host tests | Physical device compatibility and end-to-end Bluetooth/Wi-Fi networking are not established |
| Other Wi-Fi adapters / router hardware | No additional chipset-specific drivers validated | Not supported by evidence; router firmware is outside the current OS-driver scope |
| NVIDIA GA10x (NVX) | Bounded VBIOS/FWSEC and booter parsers; FWSEC interface command-window parser; synthetic BAR0 tests for Falcon reset, DMA, BROM, launch, mailbox and ownership sequencing; CI host tests | Firmware parsing and software sequencing are host-tested; physical SEC2 secure boot, authenticated WPR placement, GSP RISC-V activation, first GSP-RM RPC, and acceleration remain unverified |

## Hardware inventory diagnostics

The serial log uses stable `VANTAOS_HW_*` markers:

- `VANTAOS_HW_ACPI_ROOT` and `VANTAOS_HW_ACPI_TABLE` report the validated ACPI root and checksum-valid root tables.
- `VANTAOS_HW_SMBIOS_*` reports firmware, system product, baseboard, processor, and installed memory information. System UUIDs and serial-number fields are deliberately never emitted.
- `VANTAOS_HW_PCI` reports BDF, vendor/device IDs, revision, header type, subsystem IDs for type-0 functions, and class/programming interface.
- `VANTAOS_HW_USB_DEVICE` reports enumerated root-port USB device IDs and class information.
- Existing CPU and GPU diagnostics report processor capabilities and discovered display controllers. Intel UHD 620 (8086:5917) is identified as Gen9.5, but is deliberately not passed to the Skylake Gen9 execution backend.
- `VANTAOS_HW_NETWORK_CONTROLLER` reports PCI network-class functions (class 02), including candidate laptop Wi-Fi PCIe adapters.
- `VANTAOS_HW_AUDIO_HDA_CANDIDATE` reports PCI High Definition Audio functions (class 04:03). `VANTAOS_HW_HDA_CONTROLLER` passively reads GCAP, version, GCTL and STATESTS and reports codec-presence bits without resetting the controller, clearing write-one-to-clear status, changing PCI decode, or starting DMA. This is controller/codec discovery, not PCM playback or codec-widget support.
- `VANTAOS_HW_I2C_SERIAL_IO_CANDIDATE` reports PCI class 0C:80 serial-bus candidates. This class is not sufficient to prove an I2C controller or discover a touchpad; ACPI namespace evaluation and I2C-HID support remain required.
- `VANTAOS_HW_PCI_RESOURCES` reports PCI command and firmware-programmed interrupt line/pin for display, audio, network, and serial-bus controllers. `VANTAOS_HW_PCI_BAR` reports only already-assigned BAR base/type/width; inventory never sizes or rewrites a BAR. These records support later driver matching and MMIO validation, not permission to map or activate a device.

These are identification records, not driver-binding or functionality claims. SMBIOS and ACPI parsing are bounded and fail closed on invalid checksums, ranges, or structure lengths. The current SMBIOS reader only dereferences firmware tables within the kernel's existing low-memory identity-mapped window. ACPI now has a bounded, non-executing AML subset parser for static Device/Scope objects, `_HID`/`_CID`/`_UID`/`_STA`, static `_CRS` resource templates (including I2C SerialBus descriptors), and the standard `_DSD` `hid-descr-addr` property. It does not execute methods or evaluate dynamic `_STA`, `_CRS`, or `_DSM`; this is discovery groundwork, not I2C controller or HID-over-I2C support.

## Safe testing rules

- Use QEMU first and disposable disk images.
- Do not point experiments at a host system disk or any disk containing important data.
- Do not install this prototype to a physical disk or replace a working bootloader.
- Keep personal files and the host operating system separate from test images.
- When testing real hardware eventually, start with non-destructive live media and a recovery plan; do not claim support after one successful boot.

## Reporting a device test

Record the device model/revision, firmware settings, boot method, kernel commit, exact test steps, expected result, actual result, serial/panic logs, and whether the test was repeated. Redact serial numbers, UUIDs, MAC addresses, usernames, and other personal identifiers before publishing logs.

A useful support claim is specific, e.g. “boots to the desktop in QEMU with 256 MiB RAM under this workflow.” A claim such as “supports all PCs” is not justified by the current evidence.


## High Definition Audio (HDA) bring-up boundary

The HDA probe accepts only PCI class 04:03 programming interface 00 with an
assigned, page-aligned BAR0 and firmware-enabled memory decoding. It maps the
first 4 KiB of the HDA register aperture and reads GCAP, VMIN/VMAJ, GCTL and
STATESTS. It never changes PCI command state, writes HDA registers, resets the
controller, starts CORB/RIRB or stream DMA, or claims that a codec is usable.
A set STATESTS bit only indicates a codec address reported by the controller;
it does not establish valid pin configuration, jack detection, or PCM output.
Controllers with disabled MMIO decode or invalid resources are skipped without
modifying device state.

## ACPI namespace table discovery

The inventory validates FADT, resolves X_DSDT with a checked fallback to the
legacy DSDT pointer, and collects checksum-valid SSDTs in a bounded array
while retaining the total count. FADT revision and SCI interrupt are exposed
for later power-management work. This is read-only table discovery: AML is
not interpreted and firmware control methods are never executed. Host tests
cover valid tables, extended-pointer fallback, corrupt checksums, malformed
roots and null arguments.

## Non-executing AML namespace subset

The kernel now has a bounded AML table walker for common Scope and Device
objects and static Name values. It extracts _HID, static _CID package entries,
_UID, static _STA, and I2C SerialBus descriptors from a static _CRS Buffer.
The I2C descriptor reports bus speed, slave address, flags, and resource
source path. Device/method packages, AML recursion, device count, identifiers,
and resource counts are bounded. Methods and conditional/control-flow
packages are skipped and never evaluated. This is deliberately not a full AML
interpreter: dynamic _STA/_CRS methods, arbitrary AML expressions, namespace
alias resolution, and resource-source path resolution are not supported.
Unknown term opcodes fail the table parse rather than scanning opaque bytes.
Host tests cover namespace paths, static IDs, I2C descriptor decoding, and
malformed package lengths. No I2C controller is activated by this inventory.
