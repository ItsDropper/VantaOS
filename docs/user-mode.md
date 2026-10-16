# User-mode programs

## Current execution path

VantaOS can create an isolated user process from a flat test image or a validated ELF64 executable. The ELF loader accepts little-endian x86-64 `ET_EXEC` images with bounded `PT_LOAD` segments in the user virtual-address window. It copies file-backed bytes into owned frames, zero-fills the remainder of each segment, rejects writable+executable segments and overlapping pages, maps a four-page user stack, and creates a minimal initial stack. User mappings use PML4 slot 1; the kernel mapping remains supervisor-only. When supported, EFER.NXE is enabled and writable stack pages are marked non-executable.

The scheduler enters CPL3 using the user selectors (`CS=0x23`, `SS=0x1B`) and the per-process kernel stack configured in the TSS. `SWAPGS` separates the kernel's per-CPU GS base from the user GS base. User exits and unhandled user exceptions switch to another process before releasing the exiting process's kernel stack, page tables, and owned frames.

## Initial syscall ABI

Userspace enters the syscall gate with `int 0x80`:

| RAX | Operation | Arguments | Result |
|---:|---|---|---|
| 0 | Exit current process | None | Does not return |
| 1 | Write bytes to serial | RDI = user buffer, RSI = length | Byte count or negative error |
| 2 | Write text to graphical terminal | RDI = user buffer, RSI = length | Byte count or negative error |

Writes are limited to 4096 bytes per call and validate the complete user buffer before reading it. The syscall numbers and register conventions are an early VantaOS ABI, not POSIX and not yet a stability promise.

## Running a program

Place a supported ELF64 executable on the FAT32 volume, then run:

`exec /path/to/program.elf`

The shell reads the file through the VFS, validates/loads the ELF, and queues a user process. The current VFS and ELF loader are still early implementations; the loader has a 64-page total mapping budget and the filesystem-backed shell currently caps executable files at 1 MiB.

## Not implemented yet

- ELF `ET_DYN`, dynamic linking, relocations, shared libraries, and a libc/runtime.
- Passing command-line arguments or environment variables; the initial stack currently has `argc=0`.
- User-facing file descriptors, filesystem syscalls, process wait/join, signals, or a stable POSIX interface.
- Demand paging, copy-on-write, per-process memory growth, and a general-purpose executable mapping API.
- Fine-grained recovery for kernel faults; only faults originating in a recognized user process are contained at process level.

## CI coverage

The QEMU matrix checks a flat ring-3 program, an ELF64 ring-3 program, malformed ELF rejection, NX stack-fault containment, process resource reclamation, and launching an ELF executable read from the FAT32 test image. These tests run with 1, 2, and 4 virtual CPUs, followed by the UEFI/OVMF handoff test.
