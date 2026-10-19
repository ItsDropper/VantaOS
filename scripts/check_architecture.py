#!/usr/bin/env python3
# Copyright (c) 2026 ItsDropper
#
# This file is part of VantaOS and is licensed under the MIT License.
# See the LICENSE file in the project root for the full license text.

"""Enforce a few intentional source-boundary rules for VantaOS.

This is a lightweight architecture regression test, not a C static analyzer.
It protects the boot coordinators from accumulating test implementations again.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]


def fail(message: str) -> None:
    print(f"ARCHITECTURE CHECK FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def read(relative: str) -> str:
    path = ROOT / relative
    if not path.is_file():
        fail(f"required source file is missing: {relative}")
    return path.read_text(encoding="utf-8")


kernel = read("kernel/kernel.c")
os_source = read("os/os.c")
kernel_make = read("kernel/Makefile")
kernel_test_make = read("kernel/selftest/Makefile")
os_make = read("os/Makefile")
workflow = read(".github/workflows/build.yml")

kernel_probe_markers = re.compile(
    r"VANTAOS_(?:BOOT_INFO_ADAPTER|PMM_BOOT_INFO|SPINLOCK_PRIMITIVE|"
    r"TLB_SHOOTDOWN|USER_MAPPING|PER_CPU_ADDRESS_SPACE)_(?:OK|FAIL)"
)
if kernel_probe_markers.search(kernel):
    fail("kernel/kernel.c contains implementation-owned self-test markers")

if re.search(r"^static\s+void\s+\w*probe\w*\s*\(", kernel, re.MULTILINE):
    fail("kernel/kernel.c contains a self-test probe implementation")

if re.search(r"^static\s+void\s+\w*probe\w*\s*\(", os_source, re.MULTILINE):
    fail("os/os.c contains a self-test probe implementation")

for marker in ("kernel_boot_info_selftest", "kernel_platform_selftest"):
    if marker not in kernel:
        fail(f"kernel boot coordinator does not call {marker}")

for path in (
    "kernel/selftest/boot_info_selftest.c",
    "kernel/selftest/platform_selftest.c",
):
    source = read(path)
    if "#ifdef VANTAOS_SELF_TEST" not in source:
        fail(f"{path} must gate test implementation behind VANTAOS_SELF_TEST")

for source in ("kernel/selftest/boot_info_selftest.c", "kernel/selftest/platform_selftest.c"):
    if source not in kernel_test_make:
        fail(f"kernel self-test manifest does not register {source}")

if "include kernel/selftest/Makefile" not in kernel_make:
    fail("kernel/Makefile must delegate self-test sources to their subsystem manifest")

if "os/selftest/process_selftest.c" not in os_make or "os/selftest/smp_selftest.c" not in os_make:
    fail("OS self-test modules are missing from the OS source manifest")

if "python3 scripts/check_architecture.py" not in workflow:
    fail("CI must run the architecture boundary check")

print("ARCHITECTURE CHECK OK")
