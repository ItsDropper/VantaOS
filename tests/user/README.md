# User-mode test programs

This directory contains small freestanding ELF64 programs used to validate the userspace loader and syscall ABI. They are built by CI and copied into the FAT32 test image; generated object files and ELF binaries are not committed.

The programs use the initial VantaOS syscall ABI:

- RAX = 1: write bytes to the serial console (RDI = buffer, RSI = length).
- RAX = 2: write text to the graphical terminal (RDI = buffer, RSI = length).
- RAX = 0: terminate the current process.
- RAX = 3: open an existing file read-only (RDI = NUL-terminated path, RSI = 1).
- RAX = 4: read bytes (RDI = descriptor, RSI = writable buffer, RDX = maximum length).
- RAX = 5: close a descriptor (RDI = descriptor).

The CI ELF reads and verifies `/USERDATA.TXT` from the disposable FAT32 image, then exercises bad-pointer, invalid-descriptor, oversized-read, missing-file, and descriptor-exhaustion errors. It exits with descriptors still open to exercise reaper cleanup. Success is reported by `VANTAOS_USER_FILE_IO_OK`.

These syscall numbers and register conventions are an early bring-up ABI, not a POSIX or stability guarantee.
