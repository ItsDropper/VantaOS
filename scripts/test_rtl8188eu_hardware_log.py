#!/usr/bin/env python3
"""Validate a captured VantaOS RTL8188EU hardware-test serial log.

This checks observed driver milestones, not mocked CI output. It does not claim
Internet access: DNS and outbound TCP/HTTPS must be tested separately.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import sys

REQUIRED = (
    "VANTAOS_RTL8188EU_USB_TRANSPORT_READY",
    "VANTAOS_RTL8188EU_FIRMWARE_READY",
    "VANTAOS_RTL8188EU_EFUSE_READY",
    "VANTAOS_RTL8188EU_REFERENCE_MAC_BB_AGC_RF_TABLES_APPLIED_TX_BLOCKED",
    "VANTAOS_RTL8188EU_TX_POWER_TABLE_READBACK_OK",
    "VANTAOS_RTL8188EU_CRYSTAL_TRIM_LC_OK_TX_STILL_BLOCKED",
    "VANTAOS_RTL8188EU_IQK_STABLE_TX_RX_OK_TX_STILL_BLOCKED",
    "VANTAOS_RTL8188EU_RADIO_PHY_IQK_READY_TX_ENABLED",
    "VANTAOS_RTL8188EU_ACTIVE_SCAN_STARTED",
    "VANTAOS_RTL8188EU_SCAN_RESULT_SEEN",
    "VANTAOS_RTL8188EU_AUTH_OK",
    "VANTAOS_RTL8188EU_ASSOC_OK",
    "VANTAOS_RTL8188EU_WPA2_AUTHENTICATED",
    "VANTAOS_RTL8188EU_DHCP_STARTED",
    "VANTAOS_DHCP_LEASE_ACQUIRED",
)
FAILURE_MARKERS = (
    "VANTAOS_RTL8188EU_FIRMWARE_LOAD_FAILED",
    "VANTAOS_RTL8188EU_RF_PREFLIGHT_FAILED_RETRYABLE",
    "VANTAOS_RTL8188EU_SCAN_FAILED",
    "VANTAOS_RTL8188EU_TX_POWER_PROGRAM_FAILED_TX_BLOCKED",
    "VANTAOS_RTL8188EU_IQK_FAILED_TX_BLOCKED",
    "VANTAOS_RTL8188EU_EFUSE_TX_POWER_CALIBRATION_INVALID",
    "VANTAOS_RTL8188EU_UNSUPPORTED_CHIP_CUT_TX_BLOCKED",
)


def validate(log: str) -> tuple[bool, list[str]]:
    problems: list[str] = []
    positions: list[int] = []
    for marker in REQUIRED:
        at = log.find(marker)
        if at < 0:
            problems.append(f"MISSING: {marker}")
        else:
            positions.append(at)
    for marker in FAILURE_MARKERS:
        if marker in log:
            problems.append(f"FAILURE MARKER PRESENT: {marker}")
    if len(positions) == len(REQUIRED) and positions != sorted(positions):
        problems.append("MILESTONES OUT OF ORDER")
    return not problems, problems


def self_test() -> int:
    good = "\n".join(REQUIRED)
    ok, problems = validate(good)
    if not ok:
        print("SELF-TEST FAILED: valid synthetic log rejected", *problems, sep="\n")
        return 1
    ok, problems = validate(good.replace(REQUIRED[6], "MISSING_ASSOC"))
    if ok or not any("MISSING: " + REQUIRED[6] in p for p in problems):
        print("SELF-TEST FAILED: missing association marker was accepted")
        return 1
    ok, problems = validate(good + "\n" + FAILURE_MARKERS[1])
    if ok or not any("FAILURE MARKER PRESENT" in p for p in problems):
        print("SELF-TEST FAILED: failure marker was ignored")
        return 1
    print("Validator self-tests passed (synthetic input only; no hardware was tested).")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("serial_log", nargs="?", help="path to captured VantaOS serial output")
    parser.add_argument("--self-test", action="store_true", help="test the validator with synthetic logs")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if not args.serial_log:
        parser.error("provide a serial log or use --self-test")
    try:
        log = Path(args.serial_log).read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        print(f"ERROR: cannot read serial log: {exc}", file=sys.stderr)
        return 2
    ok, problems = validate(log)
    if not ok:
        print("RTL8188EU HARDWARE ACCEPTANCE: FAIL")
        for problem in problems:
            print(f" - {problem}")
        print("Missing markers mean the corresponding real-hardware stage was not proven.")
        return 1
    print("RTL8188EU HARDWARE ACCEPTANCE: Wi-Fi association/WPA2/DHCP milestones observed.")
    print("NOT PROVEN: DNS resolution, outbound TCP/HTTPS, or general Internet access.")
    print("Run a real DNS lookup and outbound TCP/HTTPS request over wlan0 before declaring Internet support.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
