#!/usr/bin/env python3
# Copyright (c) 2026 ItsDropper
# This file is part of VantaOS and is licensed under the MIT License.
# See the LICENSE file in the project root for the full license text.
"""Structural regression checks for real-hardware interrupt and input paths.

Each assertion encodes a failure that cannot appear under QEMU but freezes
real laptops: firmware tables above the 1 GiB identity map, a masked LINT0,
EOI sent to the wrong controller, spurious 8259 vectors without IDT gates, an
unread auxiliary byte blocking the keyboard, and idle loops that sleep with no
interrupt source.
"""
import re
from pathlib import Path

def read(p): return Path(p).read_text(encoding="utf-8")

isr = read("arch/x86_64/isr.asm")
pic = read("arch/x86_64/pic.c")
apic = read("arch/x86_64/apic.c")
acpi = read("arch/x86_64/acpi.c")
paging = read("mm/paging.c")
timer = read("kernel/time/timer.c")
kbd = read("drivers/input/ps/2/keyboard.c")
mouse = read("drivers/input/ps/2/mouse.c")
ps2 = read("drivers/input/ps/2/ps2.c")
loop = read("os/desktop/desktop_process.c")
osrc = read("os/os.c")
ints = read("arch/x86_64/interrupts.c")

# 1. ACPI tables above 1 GiB must be reachable (real UEFI places them there).
assert "paging_map_firmware_range" in acpi
assert "ACPI_IDENTITY_LIMIT 0xC0000000ULL" in acpi
assert "int paging_map_firmware_range" in paging

# 2. IRQ stubs must pick the EOI target through pic_send_eoi, never hard-code
#    the LAPIC: virtual-wire/ExtINT delivery needs an 8259 EOI.
for stub in ("irq0_stub", "irq1_stub", "irq12_stub"):
    body = isr[isr.index(stub + ":"):]
    body = body[:body.index("iretq")]
    assert "call pic_send_eoi" in body, stub
    assert "call apic_send_eoi" not in body, stub

# 3. Spurious 8259 vectors need gates, and the handler must not EOI a spurious
#    IRQ7.
assert "pic_unexpected_stub_47" in ints and "pic_unexpected_stub_39" in ints
assert "pic_handle_unexpected_irq" in pic and "PIC_READ_ISR" in pic

# 4. Without a usable IOAPIC setup, LINT0 must be programmed as ExtINT.
assert "7U << 8U" in apic and "apic_virtual_wire_enable" in apic
assert "apic_fall_back_to_virtual_wire" in apic
assert "interrupt_health_verify();" in osrc
assert osrc.index('__asm__ volatile ("sti");') < osrc.index("interrupt_health_verify();")

# 5. Timeouts must not depend on IRQ0: the TSC provides time before sti.
assert "calibrate_tsc_per_tick" in timer and "synthetic_ticks" in timer

# 6. The 8042 must be drained by polling, aux bytes must never stay unread, and
#    a failed config read must not be written back (it sets keyboard clock off).
assert "keyboard_poll_input();" in loop
assert "interrupt_health_polling_required" in loop
assert "mouse_handle_interrupt();" in kbd
assert "ps2_io_lock" in kbd and "ps2_io_lock" in mouse
assert "MOUSE_CONFIG_READ_TIMEOUT" in mouse and "~0x30" in mouse
assert "PS2_KEYBOARD_ENABLE_SCANNING" in ps2

# 7. No literal escape text in C sources (multi-character constants like '\\0').
for path in Path(".").rglob("*.c"):
    if path.parts[0] in ("scripts", "tests"):
        continue
    text = path.read_text(encoding="utf-8", errors="replace")
    assert not re.search(r"'\\\\0'", text), path
    assert not re.search(r'\\\\n"', text), path

print("VANTAOS_HARDWARE_INPUT_ROBUSTNESS_OK")
