#!/usr/bin/env python3
"""Regression guard for the Intel xHCI HCRST quiet-period workaround."""

from pathlib import Path

source = Path("drivers/usb/xhci.c").read_text(encoding="utf-8")

assert "static int xhci_pit_delay_ms(uint32_t milliseconds)" in source
assert "if (xhci_reset_issued && !xhci_pit_delay_ms(120U))" in source
assert "XHCI_PIT_MAX_CHUNK_MS  50U" in source

reset = source.index(
    "mmio_write32(operational, XHCI_USBCMD, command | XHCI_CMD_RESET);"
)
quiet_wait = source.index("if (!xhci_pit_delay_ms(1U))", reset)
first_poll = source.index(
    "if (!wait_register(operational, XHCI_USBCMD, XHCI_CMD_RESET, 0))",
    reset,
)
assert reset < quiet_wait < first_poll

# Intel erratum 20 forbids even a status read during the quiet interval.
quiet_window = source[reset:quiet_wait]
assert "mmio_read32(" not in quiet_window
assert "mmio_write32(" not in quiet_window
assert "sleep_ms(" not in quiet_window

print("VANTAOS_XHCI_RESET_ERRATUM_OK")
