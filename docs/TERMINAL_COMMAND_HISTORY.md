/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

# Course lesson: bounded command history in a ring-3 terminal

## Goal

Implement Up/Down command recall without adding a kernel syscall or allocating memory dynamically. The VantaOS terminal is a ring-3 application; command history is private application state, not a kernel service.

## Design

The terminal keeps 16 fixed-size commands in `history[HISTORY_ENTRIES][LINE_CAPACITY]`. The fixed bound is deliberate: a small shell must not grow memory without limit just because the user types commands.

- `history_start` identifies the oldest retained command.
- `history_count` is the number of valid entries, up to `HISTORY_ENTRIES`.
- `history_position` is the logical selection. The value `history_count` means the user is editing a new command, not recalling history.
- `history_browsing` records whether Up/Down currently selects a saved command.

For a logical entry `position`, its physical slot is:

```c
size_t slot = (history_start + position) % HISTORY_ENTRIES;
```

When the history is not full, append at `(history_start + history_count) % HISTORY_ENTRIES`, then increment the count. When full, advance `history_start` and overwrite the slot that became the newest logical entry. This is O(1) bookkeeping and preserves chronological order.

## Input integration

The GUI ABI already represents arrow keys with `VANTA_GUI_KEY_UP` and `VANTA_GUI_KEY_DOWN`. The event loop handles those values before treating events as printable characters:

```c
if (key == VANTA_GUI_KEY_UP) {
    if (history_count != 0U && history_position > 0U)
        history_recall(history_position - 1U);
    continue;
}
if (key == VANTA_GUI_KEY_DOWN) {
    if (history_browsing && history_position < history_count)
        history_recall(history_position + 1U);
    continue;
}
```

`history_recall()` erases the currently displayed input, clears the input buffer, copies the selected command into the editable buffer, echoes it, updates the selection state, and renders. Selecting the position equal to `history_count` restores an empty input line.

Only non-empty commands are stored. Repeated commands are currently retained; deduplication is a policy choice and should be tested if added later.

## Failure cases and invariants

1. Every stored command is NUL-terminated and fits within `LINE_CAPACITY`.
2. History count never exceeds `HISTORY_ENTRIES`.
3. Up at the oldest command does nothing; it must not underflow an unsigned index.
4. Down at the newest command returns to a blank input line; it must not read slot `history_count`.
5. Empty Enter presses do not consume history capacity.
6. History is lost when the terminal process exits. Persistent history would require an explicit file policy and should not be silently introduced.
7. Command recall must not execute the command. Enter remains the only execution action.

## Verification

The strict kernel build compiles `userspace/apps/terminal.c` into the terminal ELF. The GUI ABI and existing boot/QEMU regression workflows should be checked after changing this event loop. The current CI does not simulate a human pressing arrow keys, so build success proves compilation, not interactive behavior; add a GUI event-loop host test before treating recall behavior as fully regression-tested.
