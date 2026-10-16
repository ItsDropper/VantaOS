# User-mode test programs

This directory contains small freestanding ELF64 programs used to validate the userspace loader and syscall ABI. CI builds the parent fixture in assembly and the child fixture as C using `userspace/crt/crt0.S` plus `userspace/include/vanta/syscall.h`; generated object files and ELF binaries are not committed.

The programs use the initial VantaOS syscall ABI:

- RAX = 1: write bytes to the serial console (RDI = buffer, RSI = length).
- RAX = 2: write text to the graphical terminal (RDI = buffer, RSI = length).
- RAX = 0: terminate the current process.
- RAX = 3: open an existing file read-only (RDI = NUL-terminated path, RSI = 1).
- RAX = 4: read bytes (RDI = descriptor, RSI = writable buffer, RDX = maximum length).
- RAX = 5: close a descriptor (RDI = descriptor).

The CI parent ELF reads and verifies `/USERDATA.TXT` from the disposable FAT32 image, then exercises bad-pointer, invalid-descriptor, oversized-read, missing-file, and descriptor-exhaustion errors. It spawns the C child with `argv[1] = "spawned-child"`; the C runtime validates `argc`/`argv`, writes `VANTAOS_USER_CHILD_PROGRAM_OK`, returns status 23 from `main`, and the parent collects that status through `waitpid`. The parent exits with descriptors still open to exercise reaper cleanup. Success is reported by `VANTAOS_USER_FILE_IO_OK` and `VANTAOS_USER_CHILD_PROGRAM_OK`.

The freestanding C wrappers live in `userspace/include/vanta/syscall.h`; `tests/user/abi_compile.c` is compiled with strict warnings in CI to verify the wrapper signatures and register argument order. These syscall numbers and register conventions are an early bring-up ABI, not a POSIX or stability guarantee.
