# Running ELF64 user programs

VantaOS has an early x86-64 user-mode execution path. With the FAT32 volume mounted, use:

```text
exec /path/to/program.elf [args...]
```

The shell accepts single- or double-quoted arguments and preserves whitespace inside quotes. Argument count and string storage are bounded. The loader currently accepts static ELF64 `ET_EXEC` images only; `ET_DYN`, dynamic linking, relocations, and shared libraries are not implemented.

## What currently works

- Read an ELF executable through the filesystem-backed `exec` path.
- Create regular files and write through validated ring-3 syscalls. File writes are limited to 4096 bytes per syscall, use explicit read/write/truncate access flags, and validate the full user buffer before touching the VFS. The ring-3 smoke program creates `/VNTUSER.CFG`, writes a region/timezone-shaped config payload, closes it, reopens it read-only, and verifies the bytes from disk.
- Validate and load its supported ELF64 load segments into a private user address space.
- Enter the program at CPL3 (ring 3) with a writable, non-executable initial stack.
- Supply `argc`, a null-terminated `argv[]`, an empty `envp`, and a minimal `AT_NULL` auxiliary-vector terminator.
- Make the bounded output, file-I/O, and child-spawn syscalls described below.
- Reclaim the process through the existing lifecycle path when it exits.

This is an early execution path, not a complete process environment. There is no dynamic linker, demand paging, stable binary ABI, or POSIX-compatible process environment.

## Freestanding C wrapper header

C user programs can include `<vanta/syscall.h>` from `userspace/include`. The header defines `VANTA_USER_ABI_VERSION`, syscall numbers, read/write/truncate open flags, bounded file creation/writes, and inline wrappers for write, terminal output, open/read/close, child spawn, waitpid, and process exit. It is freestanding and does not require libc. Wrappers return the kernel's raw result: a non-negative value on success or a negative error number on failure; they do not set `errno`. See [user-mode.md](user-mode.md) for the wrapper table and limitations. CI compiles every wrapper with `-Wall -Wextra -Werror` to catch ABI/header drift.

## Bring-up syscall ABI

The current syscall entry is `int 0x80`. Put the syscall number in `RAX`; the first two arguments are in `RDI` and `RSI`. Return values are placed in `RAX`; negative values represent errors for the implemented non-exit calls.

| `RAX` | Operation | Arguments |
| --- | --- | --- |
| `0` | Exit current process | `RDI`: signed 32-bit exit status |
| `1` | Write to serial log | `RDI`: user-buffer address; `RSI`: byte count |
| `2` | Write to terminal | `RDI`: user-buffer address; `RSI`: byte count |
| `3` | Open an existing regular file | `RDI`: NUL-terminated user path; `RSI`: bit flags `1` read, `2` write, `4` truncate (truncate requires write) |
| `4` | Read from an open file | `RDI`: descriptor; `RSI`: writable user-buffer address; `RDX`: maximum byte count |
| `5` | Close an open file | `RDI`: descriptor |
| `6` | Spawn a child ELF64 process | `RDI`: NUL-terminated VFS path; `RSI`: user `argv[]` pointer array; `RDX`: argument count (1–16) |
| `7` | Wait for a direct child to exit | `RDI`: child PID; `RSI`: optional writable `int32_t*` for exit status |
| `8` | Read a terminal character | No arguments; returns a byte or zero when no character is ready |
| `9` | Yield until the next interrupt | No arguments |
| `10` | List a directory | `RDI`: path; `RSI`: writable output buffer; `RDX`: capacity |
| `11`–`13` | Terminal begin-input, backspace, clear | No arguments |
| `14` | Write to a file descriptor | `RDI`: descriptor; `RSI`: source buffer; `RDX`: byte count |
| `15` | Create a regular file (idempotent for existing files) | `RDI`: path |
| `16` | Create a GUI window | `RDI`: pointer to bounded `vanta_gui_window_request_t`; returns server-assigned window ID |
| `17` | Close an owned GUI window | `RDI`: window ID |
| `18` | Replace retained GUI draw commands | `RDI`: window ID; `RSI`: command array; `RDX`: command count |
| `19` | Poll one GUI event | `RDI`: window ID; `RSI`: writable `vanta_gui_event_t*`; returns 1, 0 if empty, or a negative error |

Serial and terminal writes accept at most 4096 bytes per call and validate the supplied range against the current process address space. File reads are limited to 4096 bytes per call and validate the complete destination range before consuming file data. Paths must terminate within `FS_PATH_MAX`; user-mode create/write supports regular files with conservative 8.3 names; each syscall transfers at most 4096 bytes, while total file size is limited by FAT32's 32-bit size field (4 GiB minus 1 byte) and available volume capacity. `vanta_gui` exposes a bounded VantaOS-specific GUI ABI on this branch: eight owned windows, 24 retained draw commands per window, text and filled rectangles, and a bounded event queue. The GUI server is merged into `master`; CI requires the GUI smoke and dead-owner cleanup markers. The ABI remains an early project-specific interface. Each process has eight descriptor slots, numbered from `3`, and descriptors are closed during process reaping. The spawn syscall copies the path, argument vector, and argument strings into kernel-owned memory before accessing the VFS or ELF loader. It accepts read-only ELF files up to 1 MiB and at most 16 arguments / 512 total argument bytes, creates the child in a private address space, and returns its PID. A child with a live user-mode parent remains as a resource-free zombie until `waitpid` collects its status. `waitpid` validates its optional status pointer before blocking and yields through the scheduler interrupt path while waiting, so the child can run on the single-CPU BSP as well as SMP configurations; it returns the child PID; orphaned children are detached and auto-reaped on exit. Kernel-owned shell launches remain auto-reaped. This is not a stable or POSIX-compatible process ABI. Errors are returned as negative values (`-EFAULT`-style `-14`, missing path `-2`, invalid argument `-22`, unsupported/invalid ELF or child-creation failure `-8`, temporary image-buffer allocation failure `-12`, and descriptor exhaustion `-24`). This is a minimal bring-up interface, not a stable or POSIX-compatible ABI.

## Ring-3 Terminal shell

The build now compiles `userspace/apps/terminal.c` as a static ELF64 image and installs it as `/TERMINAL.ELF` on the FAT32 disk. Opening Terminal spawns that ELF as a user process; command parsing and shell built-ins run in ring 3, while the current framebuffer window and terminal-history renderer remain kernel-side. The initial shell supports `help`, `about`, `pwd`, `ls`, `cat`, `echo`, `exec`, `clear`, and `exit`. The `exec` command waits for the child and reports its exit status. Job control, multi-process terminal sessions, and a stable interactive ABI are not implemented yet. User-mode file writes transfer at most 4096 bytes per syscall; total regular-file size is bounded by FAT32's 32-bit size field and available volume capacity.

The extended syscall ABI adds nonblocking keyboard-character reads, an interrupt-backed yield, a bounded newline-delimited directory listing, terminal input/backspace/clear, and bounded file create/write operations. The directory listing syscall validates both the path and the entire writable output range before copying results. CI checks the new wrappers and requires a ring-3 child program to list `/` and find the expected FAT32 fixtures. Settings and File Explorer remain kernel-rendered; the initial process-owned window server now provides a route for future ring-3 apps without exposing framebuffer internals. The server does not yet implement move/resize/minimize/maximize, and no Settings or File Explorer ELF migration is included.

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

The current file-I/O smoke test is not exhaustive. Process groups/signals, concurrent VFS access, broad fault containment, adversarial pointer fuzzing, dynamic linking, files larger than FAT32's 32-bit size ceiling, directory creation from user mode, file deletion/rename, and a stable application ABI remain unimplemented.
