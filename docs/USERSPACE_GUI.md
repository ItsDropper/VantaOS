# User-space desktop applications

## Goal

Settings, File Explorer, and Terminal are applications with real executable files and process lifetimes, not labels attached to kernel-rendered panels. The existing static ELF64 loader and ring-3 syscall path are the foundation, but they are not yet a GUI application platform.

## Current verified boundary

- Static ELF64 `ET_EXEC` programs can be loaded from FAT32 and run at CPL3.
- The syscall ABI provides process exit/spawn/wait, bounded output, read-only file descriptors, directory listing, terminal input, and yield.
- Terminal's shell logic is a ring-3 program at `/TERMINAL.ELF`; the kernel still owns its window, text rendering, and input routing.
- File Explorer and Settings remain kernel-rendered and kernel-hit-tested. File Explorer is not an isolated user-mode GUI executable.
- FAT32 file/directory creation and writes are currently unsupported. A local settings file cannot honestly be described as persistent until storage writes work and a reboot round-trip is tested.

## Required GUI boundary

A user-space GUI needs a small, validated kernel interface rather than direct framebuffer access or kernel pointers:

1. **Window lifecycle:** create, configure, and close a process-owned window with bounded dimensions and a server-assigned window ID.
2. **Event delivery:** retrieve queued keyboard, pointer, focus, close, and resize events. Events must be routed to the foreground window; a client must not consume another application's input.
3. **Drawing/presentation:** submit a bounded draw-command buffer (text and basic filled shapes) or a client-owned pixel buffer through a syscall. Validate the entire user-memory range and every command before rendering. No user process receives a raw kernel framebuffer pointer.
4. **Compositing ownership:** retain a single kernel-side compositor/input router initially. It owns z-order, window chrome, cursor, clipping, and presentation, and dispatches per-window draw data. This avoids multiple processes racing to paint the framebuffer.
5. **Process lifecycle:** closing a window terminates or detaches its client predictably; dead clients must not leave stale windows or input ownership behind.
6. **ABI and tests:** version the public userspace header, reject malformed command buffers and invalid pointers, and add QEMU tests for window create/draw/event/close plus process-exit cleanup.

The first implementation should deliberately be a small VantaOS-specific GUI ABI, not a claim of POSIX, Wayland, or Win32 compatibility. Use strict limits for windows, event queues, command counts, strings, and framebuffer bounds.

## Persistent settings and time

A settings file requires working filesystem create/write support on the FAT32-backed VFS, safe user-mode write syscalls, and a test proving values survive a reboot. The config should be versioned, bounded, parsed defensively, and written without overwriting unrelated files.

Timezone handling must not silently treat CMOS time as local time. The RTC value needs a documented UTC/local policy. Region selection should resolve to an actual timezone identifier; a timezone rule table/service must account for daylight-saving transitions. A fixed UTC offset alone is not a correct long-term implementation for regions with DST. Until those prerequisites exist, the UI must state the limitation rather than claiming local time works.

## Application migration order

1. Establish and test the GUI syscall/event/presentation contract.
2. Add persistent VFS writes and a versioned settings config with reboot round-trip tests.
3. Migrate Settings to a ring-3 ELF first, because it exercises navigation, controls, persistence, and date/time configuration.
4. Migrate File Explorer using VFS metadata/list/read APIs and add launch behavior only for recognized executable formats/manifests.
5. Keep Terminal's shell in ring 3 while moving its window/input/rendering onto the same GUI API.
6. Remove legacy kernel GUI handlers only after each replacement passes the full QEMU/UEFI regression suite.

No UI control should be presented as functional before its kernel/user API and persistence behavior exist.
