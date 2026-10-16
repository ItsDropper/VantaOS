# Running ELF64 user programs

VantaOS has an early x86-64 user-mode execution path. With a FAT32 volume mounted, use `exec /path/to/program.elf` from the shell. The loader currently accepts static `ET_EXEC` images only; `ET_DYN`, dynamic linking, relocations, and shared libraries are not implemented.

## Initial ABI (bring-up only)

The current syscall entry is `int 0x80`. Put the syscall number in `RAX`; arguments are in `RDI` and `RSI`.

- `RAX = 0`: terminate the current process.
- `RAX = 1`: write `RSI` bytes from the user buffer at `RDI` to the serial log.
- `RAX = 2`: write `RSI` bytes from the user buffer at `RDI` to the terminal.

Output buffers are validated against the current process address space before the kernel reads them. The ELF loader rejects malformed/out-of-range load segments, writable-executable segments, and entry points outside executable segments. The initial user stack is writable and non-executable; the loader requires CPU NX support.

This is not yet a stable userspace ABI or a POSIX environment. The current implementation has a bounded page budget, no arguments/environment, no dynamic linker, no demand paging, and no user-level file descriptors. Do not assume compatibility across revisions.

## Smoke test

CI builds `tests/user/hello.S`, installs the resulting ELF as `/HELLO.ELF` in the FAT32 test image, and launches it during a self-test boot. The test checks that user code reaches CPL3, writes `VANTAOS_FILESYSTEM_EXEC_OK`, uses terminal output, and exits through the syscall path without taking down the kernel.
