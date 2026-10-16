# User-mode programs

## Execution model

VantaOS runs supported static ELF64 `ET_EXEC` programs at x86-64 CPL3 with a private address space. The loader validates bounded `PT_LOAD` segments, copies file-backed bytes into owned frames, zero-fills segment tails, rejects writable/executable segments and overlapping pages, and maps a four-page user stack. User mappings use PML4 slot 1; kernel mappings remain supervisor-only. The kernel enables NX when supported and maps the user stack non-executable.

The scheduler enters user mode with `CS=0x23` and `SS=0x1B`. Each process has a dedicated kernel stack and kernel/user GS separation. User exits and unhandled user exceptions switch away before the exiting process's stack, page tables, and owned frames are released.

## Freestanding C syscall interface

Include `<vanta/syscall.h>` from `userspace/include`. It provides inline wrappers for the current syscall ABI and can be used without libc or a runtime startup object. The ABI version macro is `VANTA_USER_ABI_VERSION` (currently `1`).

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
| 3 | `vanta_open(path, flags)` | RDI: path, RSI: `VANTA_OPEN_READ` | Descriptor |
| 4 | `vanta_read(fd, buffer, length)` | RDI: descriptor, RSI: destination, RDX: maximum bytes | Bytes read |
| 5 | `vanta_close(fd)` | RDI: descriptor | Zero on success |
| 6 | `vanta_spawn(path, argv, argc)` | RDI: path, RSI: argv, RDX: argument count | Child PID |
| 7 | `vanta_waitpid(pid, status)` | RDI: direct-child PID, RSI: optional writable status pointer | Collected child PID |

The current limits are 4096 bytes per read/write call, eight per-process file descriptors beginning at 3, 16 arguments, 512 total argument bytes, and a 1 MiB executable-file limit in the shell launch path. Open currently supports read-only access to existing files. `vanta_waitpid` accepts a null status pointer. A child with a live user parent remains a resource-free zombie until its exit status is collected.

The syscall numbers and semantics are an **initial VantaOS ABI**, not a POSIX interface and not yet a long-term compatibility promise. Keep the public header, kernel dispatch, tests, and documentation synchronized when changing it.

## Current limitations

- Static ELF64 `ET_EXEC` only; no `ET_DYN`, dynamic linker, relocations, or shared libraries.
- No libc/startup runtime, standard streams, environment variables, heap allocator, or general-purpose thread API.
- No file creation/writes, pipes, process groups, signals, or POSIX-compatible descriptor semantics.
- No demand paging, copy-on-write, or per-process memory growth.
- Kernel faults are not generally recoverable; only faults attributed to recognized user processes are contained at process level.

## Validation

CI compiles the public freestanding syscall header with strict warnings, builds static ELF64 smoke programs, and runs user-mode, syscall, filesystem, process-reaping, and malformed-image tests under QEMU at multiple RAM/CPU configurations. UEFI/OVMF boot handoff is checked separately. These tests are regression coverage for the implemented path, not proof of arbitrary ELF compatibility or complete security.
