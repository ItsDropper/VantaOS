#!/usr/bin/env python3
"""Regression checks for legacy 8042 keyboard bring-up and IRQ filtering."""

from pathlib import Path

ps2 = Path("drivers/input/ps/2/ps2.c").read_text(encoding="utf-8")
header = Path("drivers/input/ps/2/ps2.h").read_text(encoding="utf-8")
keyboard = Path("drivers/input/ps/2/keyboard.c").read_text(encoding="utf-8")
os_source = Path("os/os.c").read_text(encoding="utf-8")

assert "bool ps2_controller_initialize(void)" in ps2
assert "PS2_COMMAND_DISABLE_KEYBOARD" in ps2
assert "PS2_COMMAND_DISABLE_AUX" in ps2
assert "PS2_COMMAND_READ_CONFIG" in ps2
assert "PS2_COMMAND_ENABLE_KEYBOARD" in ps2
assert "PS2_WAIT_LIMIT" in ps2
assert "config | 0x61U" in ps2  # IRQ1 + AUX clock disabled + Set-1 translation
assert "~0x12U" in ps2          # IRQ12 and keyboard clock-disable cleared
assert "controller_ready = true;" in ps2
assert "if (!controller_ready)" in ps2
assert "VANTAOS_PS2_KEYBOARD_REGISTERED" in ps2

assert "ps2_controller_initialize" in header
assert "ps2_controller_is_ready" in header
assert "ps2_controller_initialize();" in keyboard
assert "status & 0x01U" in keyboard
assert "status & 0x20U" in keyboard
assert keyboard.index("status & 0x01U") < keyboard.index("keyboard_read_data();")

# The hardware controller must be configured before the PS/2 device registry
# is populated; the real boot path must not rely on QEMU's preconfigured 8042.
assert os_source.index("keyboard_initialize();") < os_source.index(
    "input_register_ps2_devices();"
)
assert os_source.index("keyboard_initialize();") < os_source.index(
    "mouse_initialize();"
)

print("VANTAOS_PS2_CONTROLLER_INIT_OK")
print("VANTAOS_PS2_IRQ_FILTERING_OK")
print("VANTAOS_PS2_DEVICE_REGISTRATION_GATED_OK")
