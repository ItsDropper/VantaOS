# ADR-002: Subsystem-owned initialization coordinators

- Status: proposed
- Date: 2026-10-19

## Context

The kernel startup function had accumulated direct dependencies on individual
network protocols, Ethernet controllers, USB host-controller code, and storage
drivers. The source manifests were already subsystem-oriented, but startup
ownership did not consistently follow that layout. Adding a driver therefore
required editing a high-level boot file and made the order of unrelated device
families harder to review.

## Decision

Split startup responsibilities into explicit layers:

1. `kernel/kernel.c` adapts bootloader handoff data and enters OS startup.
2. `kernel/init.c` owns ordered platform bring-up and calls
   `drivers_initialize()` only after PCI enumeration and heap initialization.
3. `drivers/init.c` coordinates the networking, USB, and storage subsystems,
   but does not call concrete controller initializers itself.
4. `net/init.c`, `drivers/usb/init.c`, and `drivers/storage/init.c` own
   initialization order inside their respective subsystems.
5. Each initializer is listed in its subsystem source manifest. Subsystem-owned
   regression tests are listed in the owning subsystem's test manifest; the
   networking self-test is therefore owned by `net/selftest/`, not the kernel.
   The architecture regression check rejects accidental reintroduction of
   concrete device dependencies into kernel entry or the cross-subsystem coordinator.

Preserve the existing runtime sequence: platform prerequisites, PCI and heap,
network protocol/driver registration and network PCI probing, xHCI, then ATA and
NVMe. This change does not imply that a registered driver supports every device.

## Alternatives considered

- Keep all calls in `kernel/kernel.c`: simple initially, but the kernel entry
  accumulates subsystem dependencies and driver additions keep crossing layers.
- Create a generic dependency-injection framework: unnecessary for this
  freestanding kernel and would obscure a small, explicit startup graph.
- Initialize devices independently or in parallel: unsafe until each driver's
  prerequisites, ordering constraints, and concurrency guarantees are explicit.

## Consequences

- The boot path is easier to audit and subsystem additions have a clear owner.
- Registration order is visible in one subsystem-specific source file.
- Networking regression code follows the same ownership boundary and is excluded
  from production builds unless `VANTAOS_SELF_TEST` is enabled.
- The lightweight architecture check protects source boundaries, while CI
  runtime tests are still required to verify boot behavior.
- Initialization remains sequential; this ADR does not introduce dynamic
  module loading, rollback of partially initialized subsystems, or hotplug.
