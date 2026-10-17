# User-space desktop applications

## Current state

VantaOS has an initial user-mode GUI boundary and three standalone ring-3 desktop applications. The current syscall ABI is version 6 and is specific to VantaOS; it is not POSIX, Win32, or Wayland compatible.

- Static ELF64 `ET_EXEC` programs can be loaded from FAT32 and run at CPL3 (ring 3) in a private address space.
- The public freestanding header `userspace/include/vanta/syscall.h` exposes the current syscall wrappers; GUI structures live in `userspace/include/vanta/gui.h`.
- The ring-3 GUI smoke program creates a window, submits draw commands, and exercises invalid pointers and malformed commands. CI requires `VANTAOS_USER_GUI_SYSCALL_OK`.
- The kernel owns framebuffer access, window registry, z-order, window chrome, event routing, and presentation. Applications never receive a raw kernel framebuffer pointer.
- Windows are process-owned. Dead-owner cleanup is tested and required by CI through `VANTAOS_GUI_DEAD_OWNER_CLEANUP_OK`.
- **Settings runs as a ring-3 ELF** at `/SYSTEM/SETTINGS.ELF`. It creates its own GUI window, provides four accent colors (Blue, Teal, Amber, Violet), light/dark appearance, compact/comfortable Explorer rows, a larger Settings label size, and high-contrast mode. It saves version 2 preferences to `/SYSTEM/VANTASET.CFG`, accepts existing version 1 files, and CI checks storage readback and persistence across a second boot.
- Accent, light/dark surfaces, contrast, and Explorer row density are applied to the desktop when saved; the text-size option currently changes the Settings controls only. The settings-apply syscall reloads the validated on-disk config without restarting and broadcasts a theme-change event so open apps refresh their own surfaces. Settings does not yet provide timezone-aware local time.
- Terminal's shell, window, text rendering, input editing, and bounded scrollback run in the ring-3 `/TERMINAL.ELF` app. File Explorer runs as `/EXPLORER.ELF` at ring 3. It supports editable absolute-path navigation, per-directory substring search, active sidebar locations, single and Ctrl-click multi-selection, selected-row outlines, right-click actions and properties, create-file/create-folder, rename (including extension changes), recursive delete of non-empty folders, and per-file RLE compression/extraction using the documented Vanta `.VZA` format. It also previews text and launches validated ELF programs. The filesystem supports ASCII long-filename creation through FAT32 LFN records; Unicode filename creation, general content editing, copy/move, and crash-atomic transactions remain unsupported.

## Current GUI ABI

The kernel validates requests and retains a bounded draw list for each window.

| Capability | Current contract |
| --- | --- |
| Window lifecycle | Create/close an owned window with a server-assigned ID; at most 8 windows |
| Drawing | Filled rectangles and text; at most 160 commands per window |
| Text limits | 31-byte title and 63-byte command text |
| Events | Poll keyboard-character, pointer, focus, close-request, and theme-change events from a bounded per-window queue |
| Queue overflow | Oldest event is dropped when the 32-event queue is full |
| Input ownership | Left/right pointer clicks route to the topmost user window; pointer events carry Ctrl state for multi-selection, and keyboard input follows focus |
| Isolation boundary | User buffers are validated; no raw framebuffer or kernel pointer is exposed |

The implementation is intentionally small. General window move/resize/maximize controls, resize events, image surfaces, a system-wide clipboard, a blocking event wait, a general widget toolkit, and mature clipping/accessibility support are not implemented. Explorer provides an app-local path clipboard for Ctrl+C/Ctrl+V in the address field, not an OS-wide clipboard.

## Storage and Settings persistence

User-mode file operations support bounded transfers (up to 4096 bytes per syscall), a small per-process descriptor table, metadata queries, and FAT32 regular-file create/write, directory creation, rename, and deletion. ASCII long names are stored with FAT32 LFN entries and collision-safe 8.3 aliases; the on-disk file-size field remains 32-bit and actual capacity is limited by the volume. CI exercises long-name create/rename/delete and directory lifecycle on the disposable FAT32 image. Settings opens an existing config for truncating writes and creates it only when it is absent.

This is an early filesystem API, not a complete production filesystem. Filename creation currently accepts printable ASCII only; Unicode LFN writes, cross-directory move, crash-atomic replacement, concurrent mutation, and exhaustive corruption/power-loss handling remain unsupported. Explorer's `.VZA` archive format uses bounded-memory RLE packets and is specific to VantaOS, not ZIP-compatible. Settings rejects unsupported or malformed configuration values and falls back to defaults; this format is versioned but remains deliberately minimal.

## Date, time, and region

The Settings UI must not label an RTC value as local time unless its policy is explicit. Region selection should resolve to a timezone identifier and daylight-saving rules; a fixed UTC offset is insufficient for regions with DST. Until the RTC policy and timezone conversion are implemented and tested, local-time correctness is an open task.

## Next implementation sequence

1. Harden user-mode process isolation with fault-containment, invalid-mapping, malformed-ELF, allocation-failure, and adversarial address-space tests.
2. Add a blocking GUI event wait and complete window move/resize/minimize/maximize semantics without weakening process ownership.
3. Implement RTC UTC/local policy and timezone-rule conversion before exposing region-based local time as functional.
4. Expand Explorer only after the filesystem API supports safe create/write/rename/delete operations and their regression coverage.
5. Keep every Settings preference tied to a real effect, validated persistence, and regression coverage; a UI control alone is not evidence of a working system feature.

A control is functional only when its backing API, input path, persistence semantics (where relevant), and regression test exist.
