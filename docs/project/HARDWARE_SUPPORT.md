# Hardware support and limitations

VantaOS is an experimental x86-64 OS. Source code for a driver is not proof that the driver works on all devices using that class of hardware.

## Current verification

| Area | Current evidence | Status |
| --- | --- | --- |
| x86-64 boot path | GRUB/Multiboot QEMU regressions plus a separate OVMF UEFI loader/kernel handoff workflow | Verified in tested QEMU/OVMF configurations; physical firmware still unverified |
| Serial diagnostics | CI captures COM1 output and checks boot/test markers | Verified in QEMU |
| Timer/interrupt path | PIT frequency bounds, tick wrap arithmetic, scheduler probe and process reaping are covered by regression tests | Narrow path tested; not exhaustive |
| Kernel watchdog | Fixed-size watchdog registry, heartbeat API, latched expiry, stale-handle rejection and tick-wrap behavior have host regression tests wired into CI | No production subsystem client or safe recovery observer is registered yet; software expiry only, no hardware watchdog |
| Graphics/framebuffer | UEFI GOP and Multiboot framebuffer handoff validate 32-bit RGB layout, physical range, pitch, and mapping bounds; non-Bochs scanout uses uncached MMIO mappings and accepts framebuffer addresses below the architectural 52-bit physical limit | Software layout tests and CI boot/screenshot paths; high-address GOP hardware and physical UHD 620 scanout remain unverified |
| Intel UHD 620 identification | Intel PCI ID 8086:5917 is classified as Kaby Lake Gen9.5; the Skylake-only Gen9 runtime is explicitly not started for this device | Read-only identity only; no UHD 620 display modesetting, acceleration, power management, or physical validation |
| Keyboard/mouse | PS/2 and USB HID paths exist; bounded HID-over-I2C report transport supports standard keyboard, mouse, and absolute touchpad pointer movement and two-contact vertical scrolling | Host parser/PIO tests cover software paths; GPIO interrupt delivery is not implemented; ACPI trigger/polarity metadata and read-only GPI status/enable snapshots are available, while physical Intel Serial IO compatibility, contacts beyond two, horizontal gestures, tap-to-click, and device-specific firmware remain unverified |
| Audio (HDA) | Passive controller inventory plus bounded vendor/revision GET_PARAMETER queries when firmware has already left the controller active; no reset, DMA, bus-master changes, or stream setup | Host tests cover stale status, busy/response timeouts, and register faults; PCM streams, DMA rings, playback, and physical codec validation remain unimplemented |
| AHCI/SATA groundwork | Host-tested PRDT builder and ABAR validator; read-only PCI discovery probes CAP/PI only for controllers whose I/O decode, memory decode, and bus mastering are disabled | Not hardware support; active-controller handoff, HBA startup, DMA command engine, interrupts, recovery, and block-device integration remain unimplemented |
| PCI/legacy ATA PIO | IDE-compatible primary/secondary channel probing, bounded LBA28 reads/writes, and shared host-tested request-range validation | Physical controller compatibility and broad error-recovery behavior unverified; this is not an AHCI/SATA driver |
| PCIe extended configuration | ACPI MCFG region validation and read-only extended-capability traversal are implemented; malformed links are host-tested | Real ECAM firmware tables and physical PCIe devices are not yet hardware-validated |
| Hardware identification | CPU capability/topology reporting, PCI BDF/vendor/device/class/revision/subsystem inventory, checksummed ACPI root-table inventory, SMBIOS 2.x/3.x system/board/BIOS/CPU/memory inventory, and USB device VID:PID/class logging | Software paths and synthetic SMBIOS parser tests; physical firmware tables and laptop USB devices still require device testing. SMBIOS tables above the current 1 GiB identity-map window are intentionally not dereferenced |
| FAT32 | Mount validates BPB geometry against partition bounds; cluster and FAT accesses are volume-bounded; ATA PIO sector-write primitive and persistent file operations are tested in QEMU | UEFI hardware-test boots force all registered block devices read-only; physical storage and filesystem compatibility remain unverified |
| USB | xHCI host path and USB HID support have QEMU regression coverage; Bluetooth HCI command state has a bounded software watchdog; RTL8188EU USB Wi-Fi driver code has host tests | Physical device compatibility and end-to-end Bluetooth/Wi-Fi networking are not established |
| Other Wi-Fi adapters / router hardware | No additional chipset-specific drivers validated | Not supported by evidence; router firmware is outside the current OS-driver scope |
| NVIDIA GA10x (NVX) | Five-component firmware bundle/preflight; VBIOS/FWSEC parsing and staging; platform coordinator for WPR, DMA queue/init-args, bus-master gating and composed FWSEC/FRTS → SEC2 → GSP-RM startup; RM memory/CTXDMA/DMA-map and copy-engine software components; mocked coordinator CI | No platform adapter/caller or real SEC2/GSP quiesce implementation. Physical SEC2 authentication, WPR/VRAM placement, GSP-RM RPC, GPU-visible memory, GPU-produced copy and acceleration remain unverified |
| NVIDIA GeForce MX130/MX150 (NVX identification/probe path) | PCI IDs 10DE:174D (MX130/GM108), 10DE:1D10 and 10DE:1D12 (MX150/GP108) are classified; PMC_BOOT_0/1 identity parsing and Maxwell/Pascal architecture checks reuse NVX PCI/resource/MMIO/probe code | Identification only. No Maxwell/Pascal firmware boot, Falcon/engine initialization, GPUVM setup, display, compute, or acceleration backend; GA10x SEC2/GSP path is explicitly rejected |

## Hardware inventory diagnostics

The serial log uses stable `VANTAOS_HW_*` markers:

- `VANTAOS_HW_ACPI_ROOT` and `VANTAOS_HW_ACPI_TABLE` report the validated ACPI root and checksum-valid root tables.
- `VANTAOS_HW_SMBIOS_*` reports firmware, system product, baseboard, processor, and installed memory information. System UUIDs and serial-number fields are deliberately never emitted.
- `VANTAOS_HW_PCI` reports BDF, vendor/device IDs, revision, header type, subsystem IDs for type-0 functions, and class/programming interface.
- `VANTAOS_HW_USB_DEVICE` reports enumerated root-port USB device IDs and class information.
- Existing CPU and GPU diagnostics report processor capabilities and discovered display controllers. Intel UHD 620 (8086:5917) is identified as Gen9.5, but is deliberately not passed to the Skylake Gen9 execution backend. NVX additionally classifies MX130 (GM108/Maxwell) and MX150 (GP108/Pascal) from PCI IDs and supports bounded read-only PMC_BOOT_0/1 identity probes; neither GPU is initialized.
- `VANTAOS_HW_NETWORK_CONTROLLER` reports PCI network-class functions (class 02), including candidate laptop Wi-Fi PCIe adapters.
- `VANTAOS_HW_AUDIO_HDA_CANDIDATE` reports PCI High Definition Audio functions (class 04:03). `VANTAOS_HW_HDA_CONTROLLER` passively reads GCAP, version, GCTL and STATESTS and reports codec-presence bits without resetting the controller, clearing write-one-to-clear status, changing PCI decode, or starting DMA. This is controller/codec discovery, not PCM playback or codec-widget support.
- `VANTAOS_HW_I2C_SERIAL_IO_CANDIDATE` reports PCI class 0C:80 serial-bus candidates. The I2C driver binds only whitelisted Intel DesignWare Serial IO IDs after matching PCI BDF through ACPI root-bridge _BBN/_SEG and controller _ADR; it then requires a valid DesignWare signature, FIFO depths, and firmware timing registers. HID children are matched by exact ACPI I2C resource-source path and require a static _DSD hid-descr-addr.
- `VANTAOS_GPIO_SPTLP_READ_ONLY_MMIO_READY` means the active `INT344B` ACPI controller exposed exactly three valid, non-overlapping memory windows and the driver completed bounded reads of PAD_OWN, PADCFGLOCK, HOSTSW_OWN, and every GPI_IS/GPI_IE group register (seven groups total). `VANTAOS_GPIO_SPTLP_MMIO_PROBE_SKIPPED` means the resource set did not pass validation or mapping. Neither marker means GPIO pins were configured or interrupts were enabled.
- `VANTAOS_HW_PCI_RESOURCES` reports PCI command and firmware-programmed interrupt line/pin for display, audio, network, and serial-bus controllers. `VANTAOS_HW_PCI_BAR` reports only already-assigned BAR base/type/width; inventory never sizes or rewrites a BAR. These records support later driver matching and MMIO validation, not permission to map or activate a device.

These are identification records, not driver-binding or functionality claims. SMBIOS and ACPI parsing are bounded and fail closed on invalid checksums, ranges, or structure lengths. The current SMBIOS reader only dereferences firmware tables within the kernel's existing low-memory identity-mapped window. UEFI GOP framebuffer validation is separate from the 4 GiB kernel-image allocation ceiling and supports validated scanout addresses below the CPU-reported physical-address limit (and the 52-bit page-table format), subject to the 16 MiB graphics window. ACPI now has a bounded, non-executing AML subset parser for static Device/Scope objects, `_HID`/`_CID`/`_UID`/`_STA`, static `_CRS` resource templates (including I2C SerialBus descriptors), and the standard `_DSD` `hid-descr-addr` property. It does not execute methods or evaluate dynamic `_STA`, `_CRS`, or `_DSM`; the whitelisted Intel DesignWare Serial IO and HID-over-I2C polling paths consume this static data; GPIO interrupt delivery and physical laptop validation remain outstanding.

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
assigned, page-aligned BAR0 and firmware-enabled memory decoding. Its passive
inventory maps the first 4 KiB and reads GCAP, VMIN/VMAJ, GCTL and STATESTS
without writing registers. A separate bounded query path may issue HDA
GET_PARAMETER verbs for vendor and revision IDs only when firmware has already
left the controller out of reset. It does not reset the controller, change PCI
command state, enable bus mastering, start CORB/RIRB or stream DMA, or configure
codec widgets. A set STATESTS bit only indicates a codec address reported by
the controller; a successful vendor query still does not establish valid pin
configuration, jack detection, or PCM output. Controllers with disabled MMIO
decode or invalid resources are skipped without modifying device state.

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
Host tests cover namespace paths, static IDs, I2C and GPIO descriptor decoding (including interrupt flags), and malformed package lengths. The Intel Serial IO path consumes static I2C resources. GPIO connections are resolved against an active `INT344B` controller and checked against Sunrise Point-LP's 152-pin community ranges; a separate bounded probe maps the three ACPI-assigned community windows, reads selected ownership/lock registers plus all seven GPI_IS/GPI_IE group pairs, and releases the mappings. It never writes GPIO registers or routes interrupts.

## Intel Serial IO I2C and HID-over-I2C

The current I2C path is a polled DesignWare PIO implementation for explicitly listed Intel Serial IO PCI IDs. It does not enable bus mastering or DMA. It validates the DesignWare component signature and FIFO depths, preserves firmware-programmed SCL timing counts, rejects buses without usable standard/fast timing, waits for idle before taking controller ownership, and bounds every FIFO/enable wait. ACPI controller matching uses a static _ADR beneath a recognized PCI root bridge and its _BBN/_SEG; child matching requires an exact I2C SerialBus resource-source path, 7-bit addressing, a supported bus speed, and a static HID descriptor address. HID-over-I2C reads and validates the HID descriptor and report descriptor, then registers keyboard/pointer devices through the common input queue. The pointer decoder handles standard relative mouse fields and a single absolute touch contact as relative motion; it is not a multitouch gesture engine. Polling is rate-limited by the kernel tick and bounded per desktop iteration. GPIO connection descriptors are parsed with bounded pin tables and controller paths. For `INT344B`, ACPI GPIO interrupt trigger, polarity, sharing and wake flags are validated and mapped to Sunrise Point-LP GPI_IS/GPI_IE group/bit locations; a bounded read-only snapshot reports pending and enabled state for those pins. The driver still does not configure pad mux/direction/electrical settings, enable GPIO interrupts, route them through the IOAPIC, or deliver interrupt callbacks. HID-over-I2C polling remains the active fallback. ACPI power-resource method execution, dynamic AML resource evaluation, controller-specific clock programming, touchpad gestures beyond two-contact vertical scrolling, and physical laptop validation remain outstanding. Unsupported or ambiguous devices are left unbound.
