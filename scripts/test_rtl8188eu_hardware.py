#!/usr/bin/env python3
"""Fail-closed verifier for a *real-hardware* VantaOS RTL8188EU serial capture.

This parses evidence emitted by the running kernel. It does not emulate the
adapter and deliberately does not treat host-mocked unit tests as hardware
success. DNS/TCP/HTTP markers must come from a real on-device network probe.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import sys

STAGES = (
    ("USB transport", "VANTAOS_RTL8188EU_USB_TRANSPORT_READY"),
    ("firmware upload", "VANTAOS_RTL8188EU_FIRMWARE_READY"),
    ("eFuse identity", "VANTAOS_RTL8188EU_EFUSE_READY"),
    ("reference MAC/BB/AGC/RF tables", "VANTAOS_RTL8188EU_REFERENCE_MAC_BB_AGC_RF_TABLES_APPLIED_TX_BLOCKED"),
    ("TX-power registers", "VANTAOS_RTL8188EU_TX_POWER_TABLE_READBACK_OK"),
    ("crystal trim and LC calibration", "VANTAOS_RTL8188EU_CRYSTAL_TRIM_LC_OK_TX_STILL_BLOCKED"),
    ("stable TX/RX IQ calibration", "VANTAOS_RTL8188EU_IQK_STABLE_TX_RX_OK_TX_STILL_BLOCKED"),
    ("radio enabled", "VANTAOS_RTL8188EU_RADIO_PHY_IQK_READY_TX_ENABLED"),
    ("active scan found networks", "VANTAOS_RTL8188EU_SCAN_COMPLETE_RESULTS_PRESENT"),
    ("802.11 authentication", "VANTAOS_RTL8188EU_OPEN_AUTH_ACCEPTED"),
    ("association", "VANTAOS_RTL8188EU_ASSOCIATION_ACCEPTED"),
    ("WPA2 keys installed", "VANTAOS_RTL8188EU_WPA2_KEYS_INSTALLED_AUTHENTICATED"),
    ("DHCP lease", "VANTAOS_RTL8188EU_DHCP_IPV4_LEASE_CONFIGURED"),
    ("DNS lookup", "VANTAOS_RTL8188EU_REAL_DNS_PROBE_OK"),
    ("TCP exchange", "VANTAOS_RTL8188EU_REAL_TCP_PROBE_OK"),
    ("HTTP response", "VANTAOS_RTL8188EU_REAL_HTTP_PROBE_OK"),
)

FAILURE_MARKERS = (
    "VANTAOS_RTL8188EU_FIRMWARE_LOAD_FAILED",
    "VANTAOS_RTL8188EU_EFUSE_ATTEMPT_FAILED_TX_BLOCKED",
    "VANTAOS_RTL8188EU_RF_PREFLIGHT_FAILED_RETRYABLE",
    "VANTAOS_RTL8188EU_SCAN_FAILED",
    "VANTAOS_RTL8188EU_PHY_PROFILE_LOAD_FAILED",
    "VANTAOS_RTL8188EU_PHY_PROFILE_EXECUTION_FAILED",
    "VANTAOS_RTL8188EU_TX_POWER_PROGRAM_FAILED_TX_BLOCKED",
    "VANTAOS_RTL8188EU_IQK_FAILED_TX_BLOCKED",
    "VANTAOS_RTL8188EU_EFUSE_TX_POWER_CALIBRATION_INVALID",
)


def evaluate(log: str) -> tuple[list[tuple[str, bool]], list[str]]:
    """Return stage results and observed failure markers."""
    results = [(name, marker in log) for name, marker in STAGES]
    failures = [marker for marker in FAILURE_MARKERS if marker in log]
    return results, failures


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("serial_log", type=Path, help="captured VantaOS serial output from a physical RTL8188EU run")
    args = parser.parse_args(argv)
    try:
        log = args.serial_log.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        print(f"ERROR: cannot read serial log: {exc}", file=sys.stderr)
        return 2

    results, failures = evaluate(log)
    print("RTL8188EU REAL-HARDWARE ACCEPTANCE")
    for name, passed in results:
        print(f"[{'PASS' if passed else 'FAIL'}] {name}")
    for marker in failures:
        print(f"[FAIL] driver reported {marker}")

    missing = [name for name, passed in results if not passed]
    if failures or missing:
        print("\nRESULT: FAIL / INCOMPLETE — do not claim end-to-end Wi-Fi works.")
        if any(name in missing for name in ("DNS lookup", "TCP exchange", "HTTP response")):
            print("DNS/TCP/HTTP require a real on-device probe to emit their success markers.")
        return 1
    print("\nRESULT: PASS — real hardware log contains evidence for every required stage.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
