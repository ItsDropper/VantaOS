# User-mode test programs

This directory contains small freestanding ELF64 programs used to validate the userspace loader and syscall ABI. They are built by CI and copied into the FAT32 test image; generated object files and ELF binaries are not committed.

The programs use the initial VantaOS syscall ABI:

- RAX = 1: write bytes to the serial console (RDI = buffer, RSI = length).
- RAX = 2: write text to the graphical terminal (RDI = buffer, RSI = length).
- RAX = 0: terminate the current process.

These syscall numbers and register conventions are an early bring-up ABI, not a POSIX or stability guarantee.
