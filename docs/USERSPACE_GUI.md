# User-space desktop applications

## Goal

Settings, File Explorer, and Terminal are applications with real executable files and process lifetimes, not labels attached to kernel-rendered panels. The existing static ELF64 loader and ring-3 syscall path are the foundation, but they are not yet a GUI application platform.

## Current verified boundary

- Static ELF64 `ET_EXEC` programs can be loaded from FAT32 and run at CPL3.
- The syscall ABI provides process exit/spawn/wait, bounded output, read-only and bounded writable file descriptors, directory listing, terminal input, and yield. This branch also adds a first GUI ABI for process-owned windows, retained fill-rectangle/text commands, and queued input/focus/close events; the GUI extension is still under review and is not part of the default branch until merged.
- Terminal's shell logic is a ring-3 program at `/TERMINAL.ELF`; the kernel still owns its window, text rendering, and input routing.
- File Explorer and Settings remain kernel-rendered and kernel-hit-tested. File Explorer is not an isolated user-mode GUI executable.
- A bounded FAT32 8.3 create/replace path for files up to 512 bytes is under validation. Long-filename writes and directory creation remain unsupported. Settings persistence must not be claimed until the write/readback and reboot round-trip tests pass.

## Required GUI boundary

The initial GUI syscall implementation now provides a small, validated kernel interface rather than direct framebuffer access or kernel pointers:

1. **Window lifecycle (initial implementation):** create and close a process-owned window with bounded dimensions and a server-assigned window ID. Dead owners are cleaned up. Window move/resize/minimize/maximize are not implemented; the user-mode window chrome only exposes the implemented close action.
2. **Event delivery (initial implementation):** poll a bounded per-window queue for keyboard characters, pointer movement/down, focus gained/lost, and close requests. Pointer clicks are routed to the topmost user window. Queues are bounded and drop the oldest event on overflow. Resize events are not implemented.
3. **Drawing/presentation (initial implementation):** submit a bounded retained command list containing filled rectangles and text. The kernel validates the entire user-memory range and every command before replacing the previous list. No user process receives a raw kernel framebuffer pointer. Limits are eight windows, 24 commands per window, 31-byte titles, and 63-byte text strings.
4. **Compositing ownership:** the kernel-side renderer/input router retains z-order, window chrome, cursor, clipping, and presentation, and dispatches each window's retained draw list. This avoids processes racing to paint the framebuffer.
5. **Process lifecycle:** closing a window terminates or detaches its client predictably; dead clients must not leave stale windows or input ownership behind.
6. **ABI and tests:** ABI version 4 adds GUI syscalls. The ring-3 smoke test covers create/draw/event/close, invalid pointers, malformed commands, and invalid window IDs. CI must still validate the current branch before the extension is considered verified.

The first implementation should deliberately be a small VantaOS-specific GUI ABI, not a claim of POSIX, Wayland, or Win32 compatibility. Use strict limits for windows, event queues, command counts, strings, and framebuffer bounds.

## Persistent settings and time

A settings file requires working filesystem create/write support on the FAT32-backed VFS, safe user-mode write syscalls, and a test proving values survive a reboot. Current groundwork supports only conservative 8.3 paths and a 511-byte write limit; robust settings commits still need a versioned format, defensive parsing, and safe replacement semantics. Do not claim arbitrary configuration files are supported.

Timezone handling must not silently treat CMOS time as local time. The RTC value needs a documented UTC/local policy. Region selection should resolve to an actual timezone identifier; a timezone rule table/service must account for daylight-saving transitions. A fixed UTC offset alone is not a correct long-term implementation for regions with DST. Until those prerequisites exist, the UI must state the limitation rather than claiming local time works.

## Application migration order

1. Finish and merge the storage/syscall PR chain and retain the cross-boot persistence regression.
2. Harden the GUI server with explicit user-mode window cleanup tests and interaction tests for click routing, focus, queue overflow, and close requests.
3. Migrate Settings to a ring-3 ELF first, because it exercises navigation, controls, persistence, and date/time configuration.
4. Migrate File Explorer using VFS metadata/list/read APIs and add launch behavior only for recognized executable formats/manifests.
5. Keep Terminal's shell in ring 3 while moving its window/input/rendering onto the same GUI API.
6. Add proper window move/resize behavior and replace legacy kernel panels only after each replacement passes the full QEMU/UEFI regression suite.

No UI control should be presented as functional before its kernel/user API and persistence behavior exist.
