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
- Make the bounded output, file-I/O, and child-spawn syscalls described below.
- Reclaim the process through the existing lifecycle path when it exits.

This is an early execution path, not a complete process environment. There is no dynamic linker, demand paging, stable binary ABI, or POSIX-compatible process environment.

## Freestanding C wrapper header

C user programs can include `<vanta/syscall.h>` from `userspace/include`. The header defines `VANTA_USER_ABI_VERSION`, syscall numbers, the read-only open flag, and inline wrappers for write, terminal output, open/read/close, child spawn, waitpid, and process exit. It is freestanding and does not require libc. Wrappers return the kernel's raw result: a non-negative value on success or a negative error number on failure; they do not set `errno`. See [user-mode.md](user-mode.md) for the wrapper table and limitations. CI compiles every wrapper with `-Wall -Wextra -Werror` to catch ABI/header drift.

## Bring-up syscall ABI

The current syscall entry is `int 0x80`. Put the syscall number in `RAX`; the first two arguments are in `RDI` and `RSI`. Return values are placed in `RAX`; negative values represent errors for the implemented non-exit calls.

| `RAX` | Operation | Arguments |
| --- | --- | --- |
| `0` | Exit current process | `RDI`: signed 32-bit exit status |
| `1` | Write to serial log | `RDI`: user-buffer address; `RSI`: byte count |
| `2` | Write to terminal | `RDI`: user-buffer address; `RSI`: byte count |
| `3` | Open an existing file for reading | `RDI`: NUL-terminated user path; `RSI`: `1` (`VFS_OPEN_READ`) |
| `4` | Read from an open file | `RDI`: descriptor; `RSI`: writable user-buffer address; `RDX`: maximum byte count |
| `5` | Close an open file | `RDI`: descriptor |
| `6` | Spawn a child ELF64 process | `RDI`: NUL-terminated VFS path; `RSI`: user `argv[]` pointer array; `RDX`: argument count (1–16) |
| `7` | Wait for a direct child to exit | `RDI`: child PID; `RSI`: optional writable `int32_t*` for exit status |

Serial and terminal writes accept at most 4096 bytes per call and validate the supplied range against the current process address space. File reads are limited to 4096 bytes per call and validate the complete destination range before consuming file data. Paths must terminate within `FS_PATH_MAX`; user programs can only open existing files for reading. Each process has eight descriptor slots, numbered from `3`, and descriptors are closed during process reaping. The spawn syscall copies the path, argument vector, and argument strings into kernel-owned memory before accessing the VFS or ELF loader. It accepts read-only ELF files up to 1 MiB and at most 16 arguments / 512 total argument bytes, creates the child in a private address space, and returns its PID. A child with a live user-mode parent remains as a resource-free zombie until `waitpid` collects its status. `waitpid` validates its optional status pointer before blocking and yields through the scheduler interrupt path while waiting, so the child can run on the single-CPU BSP as well as SMP configurations; it returns the child PID; orphaned children are detached and auto-reaped on exit. Kernel-owned shell launches remain auto-reaped. This is not a stable or POSIX-compatible process ABI. Errors are returned as negative values (`-EFAULT`-style `-14`, missing path `-2`, invalid argument `-22`, unsupported/invalid ELF or child-creation failure `-8`, temporary image-buffer allocation failure `-12`, and descriptor exhaustion `-24`). This is a minimal bring-up interface, not a stable or POSIX-compatible ABI.

## Ring-3 Terminal shell

The build now compiles `userspace/apps/terminal.c` as a static ELF64 image and installs it as `/TERMINAL.ELF` on the FAT32 disk. Opening Terminal spawns that ELF as a user process; command parsing and shell built-ins run in ring 3, while the current framebuffer window and terminal-history renderer remain kernel-side. The initial shell supports `help`, `about`, `pwd`, `ls`, `cat`, `echo`, `exec`, `clear`, and `exit`. The `exec` command waits for the child and reports its exit status. Argument quoting, writable files, job control, and a stable interactive ABI are not implemented yet.

The extended syscall ABI adds nonblocking keyboard-character reads, an interrupt-backed yield, a bounded newline-delimited directory listing, and terminal input/backspace/clear operations. The directory listing syscall validates both the path and the entire writable output range before copying results. CI checks the new wrappers and requires a ring-3 child program to list `/` and find the expected FAT32 fixtures. The GUI remains kernel-rendered; moving Files into ring 3 still requires a window/event IPC boundary rather than direct access to framebuffer internals.

## Loader and memory constraints

The loader rejects malformed or out-of-image ELF headers/segments, unsupported ELF types, out-of-range load segments, writable-executable segments, and entry points outside executable segments. It requires CPU NX support. User image pages are subject to a fixed page budget, and the initial stack has a fixed size. These checks reduce obvious invalid inputs but do not substitute for exhaustive malformed-image, fault-containment, or security testing.

## Automated smoke test

CI builds `tests/user/hello.S`, installs the ELF as `/HELLO.ELF` in the disposable FAT32 test image, and launches it with:

```text
exec /HELLO.ELF alpha "two words"
```

The user ELF checks its argument stack, then exercises spawn's invalid path/vector/count/string-length boundaries before launching `/CHILD.ELF` with an argv vector. The child validates the copied argument, writes `VANTAOS_USER_CHILD_PROGRAM_OK`, and exits with status 23. The parent verifies invalid `waitpid` pointers and IDs, blocks until the child exits, validates the returned PID and exit status, and emits `VANTAOS_USER_WAITPID_OK`. The parent also opens `/USERDATA.TXT`, verifies fixture bytes, closes the descriptor, and checks invalid-pointer, invalid-descriptor, oversized-I/O, and missing-path errors. It fills all eight descriptor slots, verifies exhaustion, and exits with handles open so process reaping exercises descriptor cleanup. CI installs both ELF fixtures and requires the spawn and child markers in the QEMU regression matrix; UEFI/OVMF boot handoff remains a separate regression.

A passing smoke test verifies only this test program and execution path on the CI configurations. It does not prove arbitrary ELF compatibility, hostile-program isolation, real-hardware compatibility, or general syscall correctness.

## Next steps

The current file-I/O smoke test is not exhaustive. Process groups/signals, concurrent VFS access, broad fault containment, adversarial pointer fuzzing, dynamic linking, persistent file creation/writes, and a stable application ABI remain unimplemented.
