# User-space desktop applications

## Current state

VantaOS has an initial user-mode GUI boundary and five standalone ring-3 desktop applications. The current userspace syscall ABI is version 10 and the GUI event ABI is version 2; both are VantaOS-specific, not POSIX, Win32, or Wayland compatible.

- Static ELF64 `ET_EXEC` programs can be loaded from FAT32 and run at CPL3 (ring 3) in a private address space.
- The public freestanding header `userspace/include/vanta/syscall.h` exposes the current syscall wrappers; GUI structures live in `userspace/include/vanta/gui.h`.
- The ring-3 GUI smoke program creates a window, submits draw commands, and exercises invalid pointers and malformed commands. CI requires `VANTAOS_USER_GUI_SYSCALL_OK`.
- The kernel owns framebuffer access, window registry, z-order, window chrome, event routing, and presentation. Applications never receive a raw kernel framebuffer pointer.
- Windows are process-owned. Dead-owner cleanup is tested and required by CI through `VANTAOS_GUI_DEAD_OWNER_CLEANUP_OK`.
- **Settings runs as a ring-3 ELF** at `/SYSTEM/SETTINGS.ELF`. Its window has Appearance, Accessibility, and About sections; appearance controls cover four accent colors (Blue, Teal, Amber, Violet), light/dark surfaces, and compact/comfortable Explorer rows, while accessibility controls cover Settings text size and high contrast. The About section documents the app and explicitly identifies unsupported time/region settings. It saves version 2 preferences to `/SYSTEM/VANTASET.CFG`, accepts existing version 1 files, and CI checks storage readback and persistence across a second boot.
- Accent, light/dark surfaces, contrast, and Explorer row density are applied to the desktop when saved; the text-size option currently changes the Settings controls only. The settings-apply syscall reloads the validated on-disk config without restarting and broadcasts a theme-change event so open apps refresh their own surfaces. Settings does not yet provide timezone-aware local time.
- The ring-3 `/SYSTEM/CALCULATOR.ELF` app is pinned in Start. It evaluates signed 64-bit integer expressions with `+`, `-`, `*`, `/`, `%`, unary signs, parentheses and standard arithmetic precedence; it detects overflow, malformed input and division by zero rather than silently returning a bogus value. Enter evaluates, Backspace edits and Escape clears. Decimal/floating-point calculations are not supported.
- Terminal's shell, window, text rendering, input editing, and bounded scrollback run in the ring-3 `/TERMINAL.ELF` app. Terminal output supports mouse drag selection, double-click word selection, Ctrl+A selection, and system-wide Ctrl+C/Ctrl+V. The ring-3 `/SYSTEM/NOTES.ELF` Notes app edits `/HOME/USER/NOTES.TXT` by default and can open text files passed as arguments from Explorer. It supports keyboard editing, mouse drag selection, double-click word selection, Ctrl+A/Ctrl+C/Ctrl+V, and Ctrl+S save. Saving is restricted to `/HOME/USER/`; files opened elsewhere are read-only. Notes is pinned in Start and is installed by the build/UEFI setup path. Terminal and Notes now use the same bounded 4 KiB kernel clipboard, so text can be copied in one app and pasted into the other. File Explorer runs as `/EXPLORER.ELF` at ring 3. It supports editable absolute-path navigation, per-directory substring search, active sidebar locations, single and Ctrl-click multi-selection, selected-row outlines, right-click actions and properties, create-file/create-folder, rename (including extension changes), recursive delete/copy/move of folder trees, Ctrl+C/Ctrl+X/Ctrl+V file operations, and per-file RLE compression/extraction using the documented Vanta `.VZA` format. It opens `.TXT`, `.MD`, `.LOG`, and `.CFG` files in Notes (read-only outside `/HOME/USER/`), previews other files as text, and launches validated ELF programs. The filesystem supports ASCII long-filename creation through FAT32 LFN records; Unicode filename creation, general content editing, cross-directory rename, and crash-atomic transactions remain unsupported.

## IPC message contract

Userspace applications that need typed request/response messages should use the versioned envelope in [IPC architecture](IPC.md). The kernel endpoint transport remains byte-oriented, capability-based, bounded, and non-blocking; the envelope is a userspace convention and does not imply that a system service is already connected to it.

## Input responsiveness

The per-window event queue is bounded. Consecutive pointer-motion events are coalesced to the newest coordinates, and when the queue fills the oldest pending pointer-motion event is discarded before key, focus, button, or close events. Motion is only coalesced when adjacent in FIFO order, so it never moves across a semantic event. This reduces stale pointer work without changing click/key ordering.

## Current GUI ABI

The kernel validates requests and retains a bounded draw list for each window.

| Capability | Current contract |
| --- | --- |
| Window lifecycle | Create/close an owned window with a server-assigned ID; at most 8 windows |
| Drawing | Filled rectangles and text; at most 160 commands per window |
| Text limits | 31-byte title and 63-byte command text |
| Events | Poll keyboard-character, pointer down/move/up, focus, close-request, resize, and theme-change events from a bounded per-window queue; key events carry Ctrl modifier state |
| Queue overflow | Oldest event is dropped when the 32-event queue is full |
| Input ownership | Left/right pointer clicks route to the topmost user window; pointer events carry Ctrl state for multi-selection, and keyboard input follows focus |
| Isolation boundary | User buffers are validated; no raw framebuffer or kernel pointer is exposed |

The implementation is intentionally small. A blocking event wait, a general widget toolkit, and mature clipping/accessibility support are not implemented. Window move/resize/maximize and resize notifications exist, but interaction testing remains limited. Explorer's file-operation clipboard remains app-local; the shared text clipboard is available to Terminal and Notes.

## Storage and Settings persistence

User-mode file operations support bounded transfers (up to 4096 bytes per syscall), a small per-process descriptor table, metadata queries, and FAT32 regular-file create/write, directory creation, rename, and deletion. ASCII long names are stored with FAT32 LFN entries and collision-safe 8.3 aliases; the on-disk file-size field remains 32-bit and actual capacity is limited by the volume. CI exercises long-name create/rename/delete and directory lifecycle on the disposable FAT32 image. Settings opens an existing config for truncating writes and creates it only when it is absent.

This is an early filesystem API, not a complete production filesystem. Filename creation currently accepts printable ASCII only; Unicode LFN writes, cross-directory move, crash-atomic replacement, concurrent mutation, and exhaustive corruption/power-loss handling remain unsupported. Explorer's `.VZA` archive format uses bounded-memory RLE packets and is specific to VantaOS, not ZIP-compatible. Settings rejects unsupported or malformed configuration values and falls back to defaults; this format is versioned but remains deliberately minimal.

## Date, time, and region

The Settings UI must not label an RTC value as local time unless its policy is explicit. Region selection should resolve to a timezone identifier and daylight-saving rules; a fixed UTC offset is insufficient for regions with DST. Until the RTC policy and timezone conversion are implemented and tested, local-time correctness is an open task.

## Next implementation sequence

1. Harden user-mode process isolation with fault-containment, invalid-mapping, malformed-ELF, allocation-failure, and adversarial address-space tests.
2. Add a blocking GUI event wait and complete window move/resize/minimize/maximize semantics without weakening process ownership.
3. Implement RTC UTC/local policy and timezone-rule conversion before exposing region-based local time as functional.
4. Add end-to-end automated interaction tests for Explorer selection, context menus, create/rename/delete, recursive copy/move, and archive round-trips; add Unicode LFN creation and crash-atomic filesystem transactions.
5. Keep every Settings preference tied to a real effect, validated persistence, and regression coverage; a UI control alone is not evidence of a working system feature.

A control is functional only when its backing API, input path, persistence semantics (where relevant), and regression test exist.
