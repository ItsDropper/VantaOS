#!/usr/bin/env python3
"""Regression guard for the Intel xHCI HCRST quiet-period workaround."""

from pathlib import Path

source = Path("drivers/usb/xhci.c").read_text(encoding="utf-8")

assert "static int xhci_pit_delay_ms(uint32_t milliseconds)" in source
assert '"Nd"(port) : "memory"' in source
assert "if (xhci_reset_issued && !xhci_pit_delay_ms(120U))" in source
assert "XHCI_PIT_MAX_CHUNK_MS  10U" in source
assert "static uint32_t xhci_poll_timeout_ticks(void)" in source
assert source.count("(uint32_t)(timer_get_ticks() - start_tick) >= timeout_ticks") == 4

reset_statement = "mmio_write32(operational, XHCI_USBCMD, command | XHCI_CMD_RESET);"
reset = source.index(reset_statement)
quiet_wait = source.index("if (!xhci_pit_delay_ms(1U))", reset)
first_poll = source.index(
    "if (!wait_register(operational, XHCI_USBCMD, XHCI_CMD_RESET, 0))",
    reset,
)
assert reset < quiet_wait < first_poll

# Intel erratum 20 forbids even a status read during the quiet interval.
quiet_window = source[reset + len(reset_statement):quiet_wait]
assert "mmio_read32(" not in quiet_window
assert "mmio_write32(" not in quiet_window
assert "sleep_ms(" not in quiet_window

desktop = Path("os/desktop/desktop_process.c").read_text(encoding="utf-8")
desktop_main = desktop.split("void desktop_process_main(void)", 1)[1].split(
    "void desktop_process_initialize(void)", 1
)[0]
assert "i2c_initialize();" not in desktop_main
assert "static void desktop_device_io_worker(void)" in desktop
assert '"desktop-device-io"' in desktop
assert "desktop_start_background_workers(current_pid);" in desktop_main
fallback = desktop_main.split(
    "if (interrupt_health_polling_required())", 1
)[1].split("else", 1)[0]
assert "hid_i2c_poll();" in fallback
assert "usb_poll();" in fallback
assert "timer_get_ticks64()" in fallback
assert "timer_tsc_ticks_per_second() == 0U" in fallback
worker = desktop.split("static void desktop_device_io_worker(void)", 1)[1]
assert "hid_i2c_poll();" in worker
assert "usb_poll();" in worker

# UEFI may leave the xHCI controller under firmware ownership. The OS must
# traverse xECP and acquire the legacy-support semaphore before touching HCRST.
assert "static int xhci_firmware_handoff(uint32_t hcc1)" in source
assert "((hcc1 >> 16U) & 0xFFFFU) * 4U" in source
assert "const uint32_t bios_owned = 1U << 16U;" in source
assert "const uint32_t os_owned = 1U << 24U;" in source
assert "mmio_write32(registers, offset + 4U, os_owned);" in source
handoff = source.index("if (!xhci_firmware_handoff(hcc1))")
stop = source.index("command &= ~(XHCI_CMD_RUN | XHCI_CMD_INTERRUPTS);", handoff)
assert handoff < stop
assert '"desktop-watchdog"' in desktop
assert "kernel_watchdog_register(500U)" in desktop
assert "VANTAOS_DESKTOP_WATCHDOG_REGISTERED" in desktop

print("VANTAOS_XHCI_RESET_ERRATUM_OK")
print("VANTAOS_DESKTOP_STARTUP_ISOLATION_OK")
print("VANTAOS_DESKTOP_WATCHDOG_WIRED_OK")
