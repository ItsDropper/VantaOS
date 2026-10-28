# ADR-004: Native userspace ABI compatibility policy

- Status: proposed
- Date: 2026-10-28

## Context

VantaOS has a working early ring-3 path and a public freestanding syscall header. The current interface uses the project-specific VANTA_USER_ABI_VERSION value 10 and syscall entry through int 0x80. It supports static ELF64 ET_EXEC applications, bounded file descriptors, child process operations, IPC, and a project-specific GUI API. The repository explicitly does not promise a stable long-term ABI or POSIX compatibility.

Long-lived compatibility work—including any future Windows userspace runtime—would be fragile if syscall numbers, return conventions, memory ownership, or public structure layouts changed without policy. A numeric version macro alone does not tell an application which optional services exist.

## Proposed decision

1. **Keep the native kernel ABI generic.** Windows semantics belong in a userspace compatibility runtime, not in special kernel paths for one launcher or game.
2. **Never reuse an assigned syscall number.** Removed operations remain reserved. New operations are additive and must be documented in the public header, kernel dispatch, and contract tests.
3. **Preserve existing semantics.** An additive change must not silently change argument interpretation, error meanings, ownership, blocking behavior, or privilege requirements of an existing operation.
4. **Make error conventions explicit.** Syscalls return non-negative results on success and negative documented error numbers on failure. The kernel must validate every userspace pointer, length, and arithmetic operation before dereferencing or mutating state.
5. **Version public structures.** Any structure intended to evolve should begin with a size/version field, define field widths and alignment, reserve extension fields, and specify how unknown versions/sizes are rejected. Do not pass kernel pointers to userspace.
6. **Separate revision from capabilities.** Keep VANTA_USER_ABI_VERSION as the compile-time ABI revision until its exact semantics are documented. Before calling the ABI stable, add a runtime ABI/feature query that reports supported capabilities without requiring applications to guess from the kernel build.
7. **Use explicit compatibility gates.** Breaking changes require a deliberate revision change, migration notes, updated fixtures, and tests. Do not advertise POSIX or Win32 compatibility unless the corresponding contract and conformance suite exist.
8. **Keep evidence layered.** Static header/dispatch consistency checks catch numeric drift. Host tests validate pure contracts. QEMU tests exercise syscall integration. None alone proves the entire userspace security boundary.

## Alternatives considered

- **Freeze the current ABI immediately:** rejected for now because pointer validation, lifecycle semantics, error coverage, and tests still need broader audit.
- **Treat the syscall number table as an undocumented internal detail:** rejected because userspace applications already consume the public header and long-term compatibility is an explicit project goal.
- **Implement Win32 calls directly in the kernel:** rejected because it couples OS internals to one compatibility environment, enlarges the privileged attack surface, and makes independent testing and evolution harder.
- **Promise POSIX compatibility now:** rejected because the current runtime lacks a complete libc/process environment, dynamic linking, and many required interfaces.

## Consequences

- The current ABI remains explicitly experimental until this proposal is reviewed and the policy is implemented.
- The new scripts/check_syscall_abi.py check compares public syscall IDs with kernel constants and dispatch cases. It is a useful drift detector, not a substitute for semantic or runtime tests.
- Before stabilizing the ABI, inventory all syscalls, document pointer/length/error contracts, add negative tests, and design a versioned runtime feature-query structure.
- A future Windows compatibility runtime should sit above this native ABI and expose only APIs with tested semantics. Vendor anti-cheat integration remains a separate publisher/vendor approval gate.

## Acceptance criteria

This ADR should not be marked accepted until the project has:
- a reviewed versioning and deprecation policy;
- a complete syscall contract table with documented errors and memory ownership;
- automated checks for number drift and public structure/header consistency;
- negative pointer, overflow, invalid-handle, and process-cleanup tests;
- a documented runtime capability-query design and migration path.
