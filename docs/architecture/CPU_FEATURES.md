# x86-64 CPU capability inventory

VantaOS must distinguish what a processor implements from what the kernel has
actually enabled. This module records CPUID capabilities; it does not turn on
optional facilities, alter control registers, or select an optimized code path
on its own.

## Inventory contract

- CPUID leaf availability is checked before reading optional basic or extended
  leaves.
- Vendor, signature, family/model/stepping, logical processor count, and cache
  line size are recorded when available.
- Feature bits cover legacy facilities, SSE generations, AES, APIC/x2APIC,
  XSAVE/OSXSAVE, AVX/AVX2/AVX-512, XSAVE state components and optimized save
  variants, PCID, TSC-deadline, FSGSBASE, SMEP/SMAP, UMIP, PKU/OSPKE, LA57,
  cache write optimizations, CET shadow stack, and speculative-execution
  mitigation capabilities, in addition to the existing legacy and extended
  facilities.
- Address widths are populated only when leaf 0x80000008 is advertised.
- Logical topology prefers CPUID leaf 0x1F, falls back to 0x0B, and retains a
  conservative leaf-1 count when topology leaves are absent. It records
  package logical-processor count, threads per core, cores per package, and
  the initial APIC ID when a topology leaf is available. Cache geometry uses
  deterministic leaf 4, with AMD extended leaf 0x8000001D as a fallback, and
  validates sizes before publishing bounded cache records.
- XSAVE reports the CPU-supported XSTATE mask, the OS-enabled XCR0 mask, and
  the required and maximum save-area sizes when those values are available.
  XSAVEOPT/XSAVEC/XGETBV1/XSAVES are reported separately.
- AVX is marked usable only when hardware AVX and OSXSAVE are present and the
  CPU and XCR0 both include XMM/YMM state. AVX-512 additionally requires the
  AVX-512F bit and opmask/ZMM state components. Hardware feature bits alone
  never authorize executing an optional instruction.
- Discovery failure is non-fatal and conservative; existing boot paths remain.

## Integration and limits

The bootstrap processor is inventoried during early initialization. Once the
ACPI topology is validated, VantaOS refreshes that inventory at its MADT logical
CPU index and independently inventories each AP before it is admitted online.
Per-CPU records include vendor, signature, address widths, topology, cache
geometry, XSAVE state, and feature bits. Memory, interrupt, timer,
cryptography, and driver work should query this central inventory instead of
duplicating CPUID bit tests.

This inventory does not enable NXE, OSXSAVE, x2APIC, PCID, SMEP/SMAP, PKU,
CET, or other MSR/control-register features. The privilege, paging, and
speculation-mitigation bits are capability inventory only; they are not proof
that a mitigation is configured or active. Per-CPU compatibility is checked before AP admission. The candidate must
provide the kernel's required x86-64, syscall, APIC, and paging capabilities;
it must also support NX when the BSP's active page tables use NX, and must not
report narrower physical or virtual address widths. Optional ISA features are
not required on every core. `cpu_features_compute_common()` calculates the
intersection across the online logical CPU set; future SIMD or crypto dispatch
must use that intersection rather than the BSP's feature bits. Topology and
cache geometry remain descriptive metadata and do not yet drive scheduler
placement.
