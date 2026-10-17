# User-mode programs

## Execution model

VantaOS runs supported static ELF64 `ET_EXEC` programs at x86-64 CPL3 with a private address space. The loader validates bounded `PT_LOAD` segments, copies file-backed bytes into owned frames, zero-fills segment tails, rejects writable/executable segments and overlapping pages, and maps a four-page user stack. User mappings use PML4 slot 1; kernel mappings remain supervisor-only. The kernel enables NX when supported and maps the user stack non-executable.

The scheduler enters user mode with `CS=0x23` and `SS=0x1B`. Each process has a dedicated kernel stack and kernel/user GS separation. User exits and unhandled user exceptions switch away before the exiting process's stack, page tables, and owned frames are released.

## Freestanding C syscall interface

Include `<vanta/syscall.h>` from `userspace/include`. It provides inline wrappers for the current syscall ABI and can be used without libc or a runtime startup object. The ABI version macro is `VANTA_USER_ABI_VERSION` (currently `6`).

```c
#include <vanta/syscall.h>

static const char message[] = "hello from ring 3\n";

void user_main(void)
{
    (void)vanta_console_write(message, sizeof(message) - 1U);
    vanta_exit(0);
}
```

`userspace/crt/crt0.S` provides a minimal `_start` for C programs: it reads `argc`/`argv` from the kernel-built initial stack, aligns the stack for the System V AMD64 calling convention, calls `main(argc, argv)`, and exits with `main`'s return value. It is not a full C runtime: there is no heap, environment support beyond the empty `envp`, dynamic linker, TLS setup, constructors, or libc.

## Syscall ABI

Userspace enters through `int 0x80`. The syscall number is in `RAX`, arguments are in `RDI`, `RSI`, and `RDX`, and results are returned in `RAX`. Results are non-negative on success or negative error numbers on failure; wrappers return the raw result and do not set `errno`.

| RAX | Wrapper | Arguments | Result |
|---:|---|---|---|
| 0 | `vanta_exit(status)` | RDI: signed exit status | Does not return |
| 1 | `vanta_write(buffer, length)` | RDI: buffer, RSI: byte count | Bytes written |
| 2 | `vanta_console_write(buffer, length)` | RDI: buffer, RSI: byte count | Bytes written |
| 3 | `vanta_open(path, flags)` | RDI: path, RSI: read/write/truncate flags | Descriptor |
| 4 | `vanta_read(fd, buffer, length)` | RDI: descriptor, RSI: destination, RDX: maximum bytes | Bytes read |
| 5 | `vanta_close(fd)` | RDI: descriptor | Zero on success |
| 6 | `vanta_spawn(path, argv, argc)` | RDI: path, RSI: argv, RDX: argument count | Child PID |
| 7 | `vanta_waitpid(pid, status)` | RDI: direct-child PID, RSI: optional writable status pointer | Collected child PID |
| 8 | `vanta_read_char()` | None | Character byte or zero |
| 9 | `vanta_yield()` | None | Zero |
| 10 | `vanta_listdir(path, buffer, capacity)` | RDI: path, RSI: output buffer, RDX: capacity | Bytes in newline-delimited list |
| 11–13 | Terminal input/backspace/clear | No arguments | Zero |
| 14 | `vanta_file_write(fd, buffer, length)` | RDI: descriptor, RSI: source, RDX: length | Bytes written |
| 15 | `vanta_create_file(path)` | RDI: path | Zero |
| 16 | `vanta_gui_create_window(request)` | RDI: window request | Window ID |
| 17 | `vanta_gui_close_window(id)` | RDI: window ID | Zero |
| 18 | `vanta_gui_draw(id, commands, count)` | RDI: window ID, RSI: command array, RDX: count | Commands retained |
| 19 | `vanta_gui_poll_event(id, event)` | RDI: window ID, RSI: writable event pointer | One event, zero if empty |
| 25 | `vanta_kernel_command(command, output, capacity)` | RDI: command, RSI: writable output, RDX: capacity | Captured output from the built-in kernel shell; restricted to the active built-in Terminal process |

The `kernel <command>` Terminal command runs the existing kernel command dispatcher with ring-0 privileges; use it carefully. The syscall is limited to the active built-in Terminal PID and validates both user buffers. The current limits are 4096 bytes per read/write syscall, FAT32's 32-bit file-size ceiling (4 GiB minus 1 byte) subject to available volume capacity, eight per-process file descriptors beginning at 3, 16 arguments, 512 total argument bytes, and a 1 MiB executable-file limit in the shell launch path. User-mode writes currently support regular files with conservative 8.3 names. The GUI ABI is VantaOS-specific: eight windows, 24 retained draw commands per window, 31-byte titles, 63-byte text commands, and a 32-event queue that drops the oldest event on overflow. Only filled rectangles and text are drawable; window move/resize/minimize/maximize are not implemented. These GUI calls are implemented on `master`; CI covers the current smoke path and dead-owner cleanup, but not every focus/input edge case. `vanta_waitpid` accepts a null status pointer. A child with a live user parent remains a resource-free zombie until its exit status is collected.

The syscall numbers and semantics are an **initial VantaOS ABI**, not a POSIX interface and not yet a long-term compatibility promise. Keep the public header, kernel dispatch, tests, and documentation synchronized when changing it.

## Current limitations

- Static ELF64 `ET_EXEC` only; no `ET_DYN`, dynamic linker, relocations, or shared libraries.
- No libc/startup runtime, standard streams, environment variables, heap allocator, or general-purpose thread API.
- No directory creation from user mode, files larger than FAT32's 32-bit size ceiling, rename/delete, pipes, process groups, signals, or POSIX-compatible descriptor semantics.
- Settings and File Explorer have not yet been migrated to independent ring-3 GUI executables.
- No demand paging, copy-on-write, or per-process memory growth.
- Kernel faults are not generally recoverable; only faults attributed to recognized user processes are contained at process level.

## Validation

CI compiles the public freestanding syscall header with strict warnings, builds static ELF64 smoke programs, and runs user-mode, syscall, filesystem, process-reaping, and malformed-image tests under QEMU at multiple RAM/CPU configurations. UEFI/OVMF boot handoff is checked separately. These tests are regression coverage for the implemented path, not proof of arbitrary ELF compatibility or complete security.
