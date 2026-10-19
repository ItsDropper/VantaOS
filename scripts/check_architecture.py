#!/usr/bin/env python3
# Copyright (c) 2026 ItsDropper
#
# This file is part of VantaOS and is licensed under the MIT License.
# See the LICENSE file in the project root for the full license text.

"""Enforce source ownership and boot-coordinator boundaries.

This is a lightweight architecture regression test, not a C static analyzer.
It guards subsystem initialization ownership and keeps test implementations
out of kernel/OS coordinators.
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
platform_init = read("kernel/init.c")
drivers_init = read("drivers/init.c")
network_init = read("net/init.c")
network_test = read("net/selftest/network_framework_selftest.c")
network_test_make = read("net/selftest/Makefile")
storage_init = read("drivers/storage/init.c")
usb_init = read("drivers/usb/init.c")
os_source = read("os/os.c")
kernel_make = read("kernel/Makefile")
kernel_test_make = read("kernel/selftest/Makefile")
drivers_make = read("drivers/Makefile")
network_make = read("net/Makefile")
storage_make = read("drivers/storage/Makefile")
usb_make = read("drivers/usb/Makefile")
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

if "kernel_boot_info_selftest" not in kernel:
    fail("kernel entry must validate the normalized boot-info contract")
if "kernel_platform_selftest" not in platform_init:
    fail("platform initialization must invoke its platform self-test")

# Entry points should adapt bootloader state and enter the OS, not own device
# composition or pull in concrete controller/protocol implementation headers.
for forbidden in (
    '"ata.h"', '"apic.h"', '"e1000.h"', '"virtio_net.h"',
    '"virtio_net_modern.h"', '"rtl8139.h"', '"net_arp.h"',
    '"net_ipv4.h"', '"net_ipv6.h"', '"net_tcp.h"', '"nvme.h"',
    '"xhci.h"', '"pci.h"', '"storage_init.h"', '"usb_init.h"',
):
    if forbidden in kernel:
        fail(f"kernel/kernel.c directly depends on subsystem implementation header {forbidden}")
for forbidden in (
    "net_initialize(", "net_probe_pci_devices(", "e1000_initialize(",
    "virtio_net_initialize(", "rtl8139_initialize(", "xhci_initialize(",
    "ata_initialize(", "nvme_initialize(",
):
    if forbidden in kernel:
        fail(f"kernel/kernel.c directly initializes a concrete subsystem: {forbidden}")

if "drivers_initialize();" not in platform_init:
    fail("platform initialization must hand off to drivers_initialize()")
if "pci_initialize();" not in platform_init or "heap_initialize();" not in platform_init:
    fail("PCI and heap prerequisites must be initialized before device subsystems")

for call in ("net_stack_initialize();", "usb_host_controller_initialize();", "storage_initialize();"):
    if call not in drivers_init:
        fail(f"driver coordinator is missing subsystem handoff {call}")

for forbidden in ("e1000_initialize(", "virtio_net_initialize(", "xhci_initialize(", "ata_initialize(", "nvme_initialize("):
    if forbidden in drivers_init:
        fail(f"drivers/init.c bypasses subsystem ownership with {forbidden}")

for required in (
    "net_initialize();", "net_arp_initialize()", "net_ipv4_initialize()",
    "net_ipv6_initialize()", "net_tcp_initialize()", "net_probe_pci_devices()",
    "e1000_initialize()", "virtio_net_initialize()", "rtl8139_initialize()",
):
    if required not in network_init:
        fail(f"network initializer is missing required registration/probe step {required}")
if "ata_initialize();" not in storage_init or "nvme_initialize();" not in storage_init:
    fail("storage initializer must own ATA and NVMe startup")
if "xhci_initialize();" not in usb_init:
    fail("USB initializer must own xHCI startup")
if "network_framework_selftest();" not in network_init or "net_selftest.h" not in network_init:
    fail("network initializer must invoke its test-only regression through the public test interface")
if "net/selftest/network_framework_selftest.c" not in network_test_make:
    fail("network test manifest must own network_framework_selftest.c")
if "net_selftest.h" not in network_test or "#ifdef VANTAOS_SELF_TEST" not in network_test:
    fail("network regression must use the network test interface and be test-build gated")
if "network_framework_selftest.c" in kernel_test_make:
    fail("kernel self-test manifest must not own networking regression code")

manifest_requirements = (
    (kernel_make, "kernel/init.c", "kernel source manifest"),
    (drivers_make, "drivers/init.c", "driver source manifest"),
    (network_make, "net/init.c", "network source manifest"),
    (storage_make, "drivers/storage/init.c", "storage source manifest"),
    (usb_make, "drivers/usb/init.c", "USB source manifest"),
)
for manifest, source, description in manifest_requirements:
    if source not in manifest:
        fail(f"{description} does not register {source}")

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
