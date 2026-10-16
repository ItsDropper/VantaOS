# Running ELF64 user programs

VantaOS has an early x86-64 user-mode execution path. With the FAT32 volume mounted, use:

```text
exec /path/to/program.elf [args...]
```

The shell accepts single- or double-quoted arguments and preserves whitespace inside quotes. Argument count and string storage are bounded. The loader currently accepts static ELF64 `ET_EXEC` images only; `ET_DYN`, dynamic linking, relocations, and shared libraries are not implemented.

## What currently works

- Read an ELF executable through the filesystem-backed `exec` path.
- Validate and load its supported ELF64 load segments into a private user address space.
- Enter the program at CPL3 (ring 3) with a writable, non-executable initial stack.
- Supply `argc`, a null-terminated `argv[]`, an empty `envp`, and a minimal `AT_NULL` auxiliary-vector terminator.
- Make the bounded output/exit syscalls described below.
- Reclaim the process through the existing lifecycle path when it exits.

This is an early execution path, not a complete process environment. There is no dynamic linker, demand paging, stable binary ABI, or user-level file-descriptor API.

## Bring-up syscall ABI

The current syscall entry is `int 0x80`. Put the syscall number in `RAX`; the first two arguments are in `RDI` and `RSI`. Return values are placed in `RAX`; negative values represent errors for the implemented non-exit calls.

| `RAX` | Operation | Arguments |
| --- | --- | --- |
| `0` | Exit current process | No arguments |
| `1` | Write to serial log | `RDI`: user-buffer address; `RSI`: byte count |
| `2` | Write to terminal | `RDI`: user-buffer address; `RSI`: byte count |

The write calls accept at most 4096 bytes per call and validate the supplied range against the current process address space before reading it. A zero-length write succeeds. Unsupported syscall numbers return an error. This ABI is internal and may change; it is not POSIX-compatible and is not yet versioned.

## Loader and memory constraints

The loader rejects malformed or out-of-image ELF headers/segments, unsupported ELF types, out-of-range load segments, writable-executable segments, and entry points outside executable segments. It requires CPU NX support. User image pages are subject to a fixed page budget, and the initial stack has a fixed size. These checks reduce obvious invalid inputs but do not substitute for exhaustive malformed-image, fault-containment, or security testing.

## Automated smoke test

CI builds `tests/user/hello.S`, installs the ELF as `/HELLO.ELF` in the disposable FAT32 test image, and launches it with:

```text
exec /HELLO.ELF alpha "two words"
```

The program checks the argument count and both argument values from ring 3, emits `VANTAOS_ARGV_STACK_OK`, exercises serial and terminal output, and emits `VANTAOS_FILESYSTEM_EXEC_OK` before exiting. The workflow requires the expected markers in the QEMU regression matrix and separately exercises the UEFI/OVMF boot path.

A passing smoke test verifies only this test program and execution path on the CI configurations. It does not prove arbitrary ELF compatibility, hostile-program isolation, real-hardware compatibility, or general syscall correctness.

## Next steps

The next userspace milestone is a per-process file-descriptor table and bounded `open`/`read`/`close` syscalls, with ownership cleanup on process exit and an end-to-end test that reads and verifies a second file from FAT32. Persistent filesystem creation/writes, dynamic linking, and a stable application ABI remain later work.
