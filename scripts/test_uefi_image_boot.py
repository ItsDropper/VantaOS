#!/usr/bin/env python3
"""Strict raw-image UEFI boot and USB-HID integration regression.

Boots the exact GPT image as USB mass storage through OVMF. Each profile gets
fresh firmware variables. The guest must reach the desktop, prove timer and
xHCI health, enumerate USB keyboard/mouse devices, and process injected input.
"""
from __future__ import annotations

import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

IMAGE = Path("VantaOS-UEFI.img")
PROFILES = (
    ("single-core-512m", 1, "512M"),
    ("dual-core-1g", 2, "1G"),
    ("quad-core-2g", 4, "2G"),
)
REQUIRED = (
    "VANTAOS_UEFI_LOADER_START",
    "VANTAOS_UEFI_ELF_LOADED",
    "VANTAOS_UEFI_STORAGE_POLICY_READ_ONLY",
    "VANTAOS_UEFI_EXIT_BOOT_SERVICES_OK",
    "VANTAOS_UEFI_KERNEL_HANDOFF",
    "VANTAOS_UEFI_KERNEL_ENTRY",
    "VANTAOS_UEFI_BOOT_INFO_READY",
    "VANTAOS_XHCI_OS_OWNERSHIP_ACQUIRED",
    "VANTAOS_XHCI_CONTROLLER_ONLINE",
    "VANTAOS_USB_HID_KEYBOARD_ONLINE",
    "VANTAOS_USB_HID_MOUSE_ONLINE",
    "VANTAOS_DESKTOP_INPUT_LOOP_ONLINE",
    "VANTAOS_BOOT_OK",
    "VANTAOS_IRQ_TIMER_OK",
    "VANTAOS_DEVICE_IO_WORKER_READY",
    "VANTAOS_USB_HID_KEYBOARD_INPUT_OK",
    "VANTAOS_USB_HID_MOUSE_INPUT_OK",
)
FORBIDDEN = (
    "VANTAOS_UEFI_LOADER_FAIL",
    "VANTAOS_UEFI_BOOT_INFO_FAIL",
    "VANTAOS_BOOT_INFO_NORMALIZATION_FAIL",
    "VANTAOS_SMP_INIT_FATAL_FAIL",
    "VANTAOS_SMP_SCHEDULER_INIT_FATAL_FAIL",
    "VANTAOS_PROCESS_INIT_FATAL_FAIL",
    "VANTAOS_PROCESS_BOOT_ATTACH_FATAL_FAIL",
    "VANTAOS_DEVICE_IO_WORKER_CREATE_FAILED",
    "VANTAOS_DEVICE_IO_WORKER_PARENT_INVALID",
    "VANTAOS_XHCI_CONTROLLER_INIT_FAILED",
    "VANTAOS_XHCI_CONTROLLER_NOT_FOUND",
    "VANTAOS_XHCI_BIOS_OWNERSHIP_TIMEOUT",
    "VANTAOS_IRQ_TIMER_SILENT",
    "VANTAOS_IRQ_TIMER_DEAD_POLLING_MODE",
    "VANTAOS_DESKTOP_WATCHDOG_EXPIRED",
    "VANTAOS_DESKTOP_WATCHDOG_CREATE_FAILED",
    "VANTAOS_DESKTOP_WATCHDOG_REGISTER_FAILED",
)
ORDERED = (
    "VANTAOS_UEFI_LOADER_START",
    "VANTAOS_UEFI_ELF_LOADED",
    "VANTAOS_UEFI_EXIT_BOOT_SERVICES_OK",
    "VANTAOS_UEFI_KERNEL_HANDOFF",
    "VANTAOS_UEFI_KERNEL_ENTRY",
    "VANTAOS_UEFI_BOOT_INFO_READY",
    "VANTAOS_XHCI_CONTROLLER_ONLINE",
    "VANTAOS_USB_HID_KEYBOARD_ONLINE",
    "VANTAOS_USB_HID_MOUSE_ONLINE",
    "VANTAOS_BOOT_OK",
    "VANTAOS_IRQ_TIMER_OK",
    "VANTAOS_DEVICE_IO_WORKER_READY",
)


def fail(message: str, *logs: Path) -> None:
    print(f"UEFI STRICT TEST FAILURE: {message}", file=sys.stderr)
    for path in logs:
        if path.exists():
            print(f"\n===== {path} =====", file=sys.stderr)
            print(path.read_text(errors="replace")[-30000:], file=sys.stderr)
    raise SystemExit(1)


def wait_for_text(path: Path, needle: str, process: subprocess.Popen,
                  timeout: float, logs: tuple[Path, ...]) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        text = path.read_text(errors="replace") if path.exists() else ""
        if needle in text:
            return text
        if process.poll() is not None:
            fail(f"QEMU exited ({process.returncode}) before {needle}", *logs)
        time.sleep(0.05)
    fail(f"timed out waiting for {needle}", *logs)
    raise AssertionError("unreachable")


def qmp_command(stream, command: str, arguments: dict | None = None,
                ident: int = 1) -> dict:
    request = {"execute": command, "id": ident}
    if arguments is not None:
        request["arguments"] = arguments
    stream.write((json.dumps(request) + "\r\n").encode())
    while True:
        line = stream.readline()
        if not line:
            raise RuntimeError(f"QMP closed while executing {command}")
        message = json.loads(line)
        if message.get("id") == ident:
            if "error" in message:
                raise RuntimeError(f"QMP {command} failed: {message['error']}")
            return message


def inject_input(qmp_path: Path) -> None:
    deadline = time.monotonic() + 10
    connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    while True:
        try:
            connection.connect(str(qmp_path))
            break
        except (FileNotFoundError, ConnectionRefusedError):
            if time.monotonic() >= deadline:
                raise RuntimeError("QEMU QMP socket did not become available")
            time.sleep(0.05)
    connection.settimeout(5)
    stream = connection.makefile("rwb", buffering=0)
    greeting = json.loads(stream.readline())
    if "QMP" not in greeting:
        raise RuntimeError(f"invalid QMP greeting: {greeting}")
    qmp_command(stream, "qmp_capabilities", ident=1)
    qmp_command(stream, "input-send-event", {
        "events": [
            {"type": "key", "data": {"key": {"type": "qcode", "data": "a"}, "down": True}},
            {"type": "key", "data": {"key": {"type": "qcode", "data": "a"}, "down": False}},
            {"type": "rel", "data": {"axis": "x", "value": 48}},
            {"type": "rel", "data": {"axis": "y", "value": 24}},
            {"type": "btn", "data": {"button": "left", "down": True}},
            {"type": "btn", "data": {"button": "left", "down": False}},
        ]
    }, ident=2)
    stream.close()
    connection.close()


def run_profile(name: str, cpus: int, memory: str,
                code: Path, vars_template: Path) -> None:
    with tempfile.TemporaryDirectory(prefix=f"vanta-uefi-{name}-") as temp:
        root = Path(temp)
        vars_file = root / "OVMF_VARS.fd"
        shutil.copyfile(vars_template, vars_file)
        serial_log = root / "serial.log"
        debug_log = root / "debugcon.log"
        ovmf_log = root / "ovmf.log"
        qemu_log = root / "qemu.log"
        qmp_socket = root / "qmp.sock"
        command = [
            "qemu-system-x86_64",
            "-machine", "q35,accel=tcg",
            "-cpu", "max",
            "-m", memory,
            "-smp", str(cpus),
            "-drive", f"if=pflash,format=raw,readonly=on,file={code}",
            "-drive", f"if=pflash,format=raw,file={vars_file}",
            "-drive", f"if=none,id=vantadisk,format=raw,file={IMAGE.resolve()}",
            "-device", "qemu-xhci,id=xhci",
            "-device", "usb-storage,bus=xhci.0,drive=vantadisk,bootindex=1",
            "-device", "usb-kbd,bus=xhci.0",
            "-device", "usb-mouse,bus=xhci.0",
            "-boot", "menu=on,strict=on",
            "-display", "none",
            "-serial", f"file:{serial_log}",
            "-chardev", f"file,id=ovmfdebug,path={ovmf_log}",
            "-device", "isa-debugcon,iobase=0x402,chardev=ovmfdebug",
            "-chardev", f"file,id=vantadebug,path={debug_log}",
            "-device", "isa-debugcon,iobase=0xe9,chardev=vantadebug",
            "-qmp", f"unix:{qmp_socket},server=on,wait=off",
            "-monitor", "none",
            "-no-reboot",
            "-no-shutdown",
            "-d", "guest_errors,cpu_reset",
            "-D", str(qemu_log),
        ]
        print(f"[{name}] starting exact raw image ({cpus} CPU, {memory}, TCG)")
        with open(root / "qemu.stdout", "wb") as output:
            process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT)
            logs = (debug_log, serial_log, ovmf_log, qemu_log, root / "qemu.stdout")
            try:
                wait_for_text(debug_log, "VANTAOS_BOOT_OK", process, 30, logs)
                wait_for_text(debug_log, "VANTAOS_IRQ_TIMER_OK", process, 5, logs)
                wait_for_text(debug_log, "VANTAOS_DEVICE_IO_WORKER_READY", process, 10, logs)
                wait_for_text(debug_log, "VANTAOS_USB_HID_KEYBOARD_ONLINE", process, 10, logs)
                wait_for_text(debug_log, "VANTAOS_USB_HID_MOUSE_ONLINE", process, 10, logs)
                inject_input(qmp_socket)
                wait_for_text(debug_log, "VANTAOS_USB_HID_KEYBOARD_INPUT_OK", process, 8, logs)
                wait_for_text(debug_log, "VANTAOS_USB_HID_MOUSE_INPUT_OK", process, 8, logs)
                time.sleep(1)
                output_text = debug_log.read_text(errors="replace")
                for marker in REQUIRED:
                    if marker not in output_text:
                        fail(f"missing required marker {marker}", *logs)
                for marker in FORBIDDEN:
                    if marker in output_text:
                        fail(f"forbidden failure marker observed: {marker}", *logs)
                positions = [output_text.index(marker) for marker in ORDERED]
                if positions != sorted(positions):
                    fail("boot milestones appeared out of order", *logs)
                if output_text.count("VANTAOS_BOOT_OK") != 1:
                    fail("boot completion marker was not emitted exactly once", *logs)
                if output_text.count("VANTAOS_USB_HID_KEYBOARD_INPUT_OK") != 1:
                    fail("keyboard input proof was not exactly once", *logs)
                if output_text.count("VANTAOS_USB_HID_MOUSE_INPUT_OK") != 1:
                    fail("mouse input proof was not exactly once", *logs)
                if process.poll() is not None:
                    fail(f"QEMU exited after boot ({process.returncode})", *logs)
                print(f"[{name}] PASS: boot, timer, xHCI, USB HID enumeration and injected keyboard/mouse input")
            except Exception as error:
                fail(str(error), *logs)
            finally:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)


def main() -> None:
    if not IMAGE.is_file() or IMAGE.stat().st_size == 0:
        fail(f"missing image {IMAGE}")
    code = Path(sys.argv[1]).resolve()
    vars_template = Path(sys.argv[2]).resolve()
    if not code.is_file() or not vars_template.is_file():
        fail("OVMF code or variable template is missing")
    for profile in PROFILES:
        run_profile(*profile, code, vars_template)
    print("ALL STRICT RAW-IMAGE UEFI PROFILES PASSED")


if __name__ == "__main__":
    main()
