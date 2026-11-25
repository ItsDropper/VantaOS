#!/usr/bin/env python3
# Copyright (c) 2026 ItsDropper
#
# This file is part of VantaOS and is licensed under the MIT License.
# See the LICENSE file in the project root for the full license text.

"""Check the end-to-end source contract for blocking ring-3 GUI event waits."""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    file = ROOT / path
    if not file.is_file():
        raise AssertionError(f"missing required file: {path}")
    return file.read_text(encoding="utf-8")


def function_body(source: str, name: str) -> str:
    match = re.search(rf"\b{name}\s*\([^;]*?\)\s*\{{", source, re.S)
    if not match:
        raise AssertionError(f"function not found: {name}")
    start = match.end()
    depth = 1
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index]
    raise AssertionError(f"unterminated function: {name}")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    gui = read("kernel/graphics/gui_server.c")
    syscall = read("kernel/syscall.c")
    public_gui = read("userspace/include/vanta/gui.h")
    public_syscall = read("userspace/include/vanta/syscall.h")
    keyboard = read("drivers/input/ps/2/keyboard.c")
    desktop = read("os/desktop/desktop_process.c")
    selftest = read("os/selftest/process_selftest.c")
    workflow = read(".github/workflows/build.yml")

    wait_body = function_body(gui, "gui_server_wait_event")
    unlock_body = function_body(gui, "gui_unlock")
    poll_body = function_body(gui, "gui_server_poll_event")
    queue_body = function_body(gui, "gui_queue_event_locked")
    close_body = function_body(gui, "gui_server_close_window")

    require("process_wait_queue_lock" in wait_body and
            "process_wait_queue_sleep_locked" in wait_body and
            "process_wait_queue_sleep_until_locked" in wait_body,
            "GUI wait must use the process wait-queue API for infinite and timed waits")
    require(wait_body.count("gui_server_poll_event") >= 2,
            "GUI wait must recheck its event predicate after locking the wait queue")
    require("spinlock_unlock_irqrestore(&gui_lock, flags)" in poll_body and
            "gui_unlock(flags)" not in poll_body,
            "polling under a wait-queue lock must not recursively wake that same queue")
    require(unlock_body.find("spinlock_unlock_irqrestore") <
            unlock_body.find("process_wait_queue_wake_all"),
            "event waiters must be woken only after gui_lock is released")
    require("__atomic_fetch_or(&gui_pending_wake_mask" in queue_body and
            "__atomic_fetch_or(&gui_pending_wake_mask" in close_body,
            "queued events and window closure must publish a slot wake")
    require("gui_server_notify_input_available" in gui and
            "process_wait_queue_wake_all" in function_body(
                gui, "gui_server_notify_input_available"),
            "keyboard input must wake the focused window's waiter")

    require("#define USER_SYSCALL_GUI_WAIT_EVENT 45U" in syscall and
            "case USER_SYSCALL_GUI_WAIT_EVENT:" in syscall and
            "gui_server_wait_event" in syscall and
            "__asm__ volatile (\"sti\"" in syscall,
            "ring-3 wait syscall must validate, enable interrupts, and enter the blocking server path")
    require("#define VANTA_SYS_GUI_WAIT_EVENT    45U" in public_gui and
            "vanta_gui_wait_event_timeout" in public_gui and
            "vanta_gui_wait_event(" in public_gui,
            "public GUI header must expose blocking and timed waits")
    require("#define VANTA_USER_ABI_VERSION 12U" in public_syscall,
            "userspace ABI version must be incremented")
    ring3_test = read("tests/user/child.c")
    require("vanta_gui_wait_event_timeout((uint32_t)window, &event, 30U) !=" in
            ring3_test and "VANTA_GUI_EVENT_FOCUS_GAINED" in ring3_test and
            "vanta_gui_wait_event_timeout((uint32_t)window, &event, 30U) != 0L" in
            ring3_test,
            "ring-3 test must verify event copyout and an actual timed sleep")

    require("keyboard_input_generation" in keyboard and
            "keyboard_input_generation()" in desktop and
            "gui_server_notify_input_available()" in desktop and
            "keyboard_has_char() || keyboard_has_event()" in desktop,
            "desktop input loop must notify GUI waiters for new and pre-existing keyboard input")

    for app in ("calculator", "notes", "terminal", "explorer"):
        source = read(f"userspace/apps/{app}.c")
        require("vanta_gui_wait_event(" in source,
                f"{app} must block on its GUI event queue")
        require("vanta_gui_poll_event(window_id" not in source,
                f"{app} still polls its main event loop")
        require("vanta_yield()" not in source,
                f"{app} still yields in its former polling loop")

    settings = read("userspace/apps/settings.c")
    require("vanta_gui_wait_event_timeout(" in settings and
            "selected_page == 3U ? 100U : 0U" in settings and
            "bluetooth_refresh();" in settings,
            "Settings must retain periodic Bluetooth result refresh without busy polling")

    require("gui_blocking_wait_regression_probe" in selftest and
            "gui_server_test_queue_event" in selftest and
            "os_selftest_start_gui_wait_probe" in selftest and
            "VANTAOS_GUI_BLOCKING_WAIT_OK" in selftest and
            "VANTAOS_GUI_BLOCKING_WAIT_OK" in workflow,
            "QEMU must require the asynchronous blocking GUI wait wake/timeout regression")

    print("VANTAOS_GUI_BLOCKING_WAIT_SOURCE_OK")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"VANTAOS_GUI_BLOCKING_WAIT_SOURCE_FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
