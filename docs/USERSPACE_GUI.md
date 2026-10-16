# User-space desktop applications

## Current state

VantaOS has an initial user-mode GUI boundary, but not yet a complete user-space desktop platform. The current ABI is version 4 and is specific to VantaOS; it is not POSIX, Win32, or Wayland compatible.

- Static ELF64 `ET_EXEC` programs can be loaded from FAT32 and run at CPL3 (ring 3) in a private address space.
- The public freestanding header `userspace/include/vanta/syscall.h` exposes the current syscall wrappers; GUI structures live in `userspace/include/vanta/gui.h`.
- A ring-3 smoke program creates a window, submits draw commands, and exercises invalid pointers and malformed commands. CI requires `VANTAOS_USER_GUI_SYSCALL_OK`.
- The kernel owns framebuffer access, window registry, z-order, window chrome, event routing, and presentation. Applications never receive a raw kernel framebuffer pointer.
- Windows are process-owned. Dead-owner cleanup is tested and required by CI through `VANTAOS_GUI_DEAD_OWNER_CLEANUP_OK`.
- Terminal's shell is a ring-3 executable at `/TERMINAL.ELF`, but its visible window and text rendering remain kernel-side. Settings and File Explorer also remain kernel-rendered; they are not yet independent ring-3 GUI applications.

## Current GUI ABI

The kernel validates requests and retains a bounded draw list for each window.

| Capability | Current contract |
| --- | --- |
| Window lifecycle | Create/close an owned window with a server-assigned ID; at most 8 windows |
| Drawing | Filled rectangles and text; at most 24 commands per window |
| Text limits | 31-byte title and 63-byte command text |
| Events | Poll keyboard-character, pointer, focus, and close-request events from a bounded per-window queue |
| Queue overflow | Oldest event is dropped when the 32-event queue is full |
| Input ownership | Pointer clicks route to the topmost user window; keyboard input follows focus |
| Isolation boundary | User buffers are validated; no raw framebuffer or kernel pointer is exposed |

The implementation is intentionally small. Window move/resize/minimize/maximize, resize events, image surfaces, clipboard, a blocking event wait, a general widget toolkit, and mature clipping/accessibility support are not implemented. Do not present those as working features.

## Storage and Settings persistence

User-mode file operations support bounded transfers (up to 4096 bytes per syscall), a small per-process descriptor table, and conservative FAT32 8.3 regular-file create/write behavior. FAT32 range I/O supports multi-cluster files; the on-disk file-size field remains 32-bit and actual capacity is limited by the volume. The regression workflow checks file readback and a second boot using the same disposable image.

This is enough groundwork for a small local configuration file, not a full filesystem API. Long-filename writes, directory creation, rename/delete, crash-atomic replacement, and exhaustive corruption/power-loss handling remain unsupported. A Settings application must parse a versioned format defensively, reject malformed values, preserve defaults on invalid data, and verify that saved values survive a reboot before persistence is described as complete.

## Date, time, and region

The Settings UI must not label an RTC value as local time unless its policy is explicit. Region selection should resolve to a timezone identifier and daylight-saving rules; a fixed UTC offset is insufficient for regions with DST. Until the RTC policy and timezone conversion are implemented and tested, local-time correctness is an open task.

## Next implementation sequence

1. Expand GUI regression coverage for focus changes, click routing, queue overflow, close requests, and invalid event buffers.
2. Implement Settings as a real static ELF64 ring-3 application using the GUI syscall ABI, not as another kernel-rendered panel.
3. Add a versioned local configuration format and tests for defaults, malformed data, read/write failure, and reboot persistence.
4. Implement and document RTC UTC/local policy and timezone conversion before exposing region-based local time as functional.
5. Migrate File Explorer to ring 3 using directory listing and file APIs; keep launch restricted to validated executable formats.
6. Move Terminal's visible window, drawing, and input onto the same GUI ABI while retaining its shell logic in ring 3.

A control is functional only when its backing API, input path, persistence semantics (where relevant), and regression test exist.
