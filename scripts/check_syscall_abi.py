#!/usr/bin/env python3
# Copyright (c) 2026 ItsDropper
#
# This file is part of VantaOS and is licensed under the MIT License.
# See the LICENSE file in the project root for the full license text.

"""Check that the public ring-3 syscall numbers match kernel dispatch.

This is a source-contract check, not a proof that every syscall implementation
is correct or safe. The kernel remains the authority for runtime semantics.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]


def fail(message: str) -> None:
    print(f"SYSCALL ABI CHECK FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def read(relative: str) -> str:
    path = ROOT / relative
    if not path.is_file():
        fail(f"required source file is missing: {relative}")
    return path.read_text(encoding="utf-8")


def definitions(source: str, prefix: str, label: str) -> dict[str, int]:
    pattern = re.compile(
        rf"^\s*#define\s+{re.escape(prefix)}([A-Z0-9_]+)\s+([0-9]+)U?\s*$",
        re.MULTILINE,
    )
    result: dict[str, int] = {}
    values: dict[int, str] = {}
    for name, raw_value in pattern.findall(source):
        value = int(raw_value)
        if name in result:
            fail(f"{label} defines {name} more than once")
        if value in values:
            fail(
                f"{label} assigns syscall number {value} to both "
                f"{values[value]} and {name}"
            )
        result[name] = value
        values[value] = name
    return result


public_header = read("userspace/include/vanta/syscall.h")
gui_header = read("userspace/include/vanta/gui.h")
kernel_source = read("kernel/syscall.c")

version_match = re.search(
    r"^\s*#define\s+VANTA_USER_ABI_VERSION\s+([0-9]+)U?\s*$",
    public_header,
    re.MULTILINE,
)
if not version_match or int(version_match.group(1)) < 1:
    fail("VANTA_USER_ABI_VERSION is missing or invalid")

public = definitions(public_header + "\n" + gui_header, "VANTA_SYS_", "public headers")
kernel = definitions(kernel_source, "USER_SYSCALL_", "kernel")
if not public:
    fail("no public syscall numbers found")
if not kernel:
    fail("no kernel syscall numbers found")

missing_in_kernel = sorted(set(public) - set(kernel))
missing_in_public = sorted(set(kernel) - set(public))
if missing_in_kernel:
    fail("public syscalls have no kernel number definition: " +
         ", ".join(missing_in_kernel))
if missing_in_public:
    fail("kernel syscalls are absent from public headers: " +
         ", ".join(missing_in_public))

mismatched = sorted(
    (name, public[name], kernel[name])
    for name in public
    if public[name] != kernel[name]
)
if mismatched:
    details = ", ".join(
        f"{name}: public={public_value}, kernel={kernel_value}"
        for name, public_value, kernel_value in mismatched
    )
    fail("syscall number mismatch: " + details)

dispatch_start = kernel_source.find("switch (frame->rax)")
if dispatch_start < 0:
    fail("syscall dispatch switch was not found")
dispatch = kernel_source[dispatch_start:]
for name in sorted(kernel):
    if not re.search(rf"\bcase\s+USER_SYSCALL_{re.escape(name)}\s*:", dispatch):
        fail(f"kernel syscall {name} is defined but has no dispatch case")

print(
    "SYSCALL_ABI_CONTRACT_OK: "
    f"{len(public)} public/kernel syscall numbers match; "
    f"ABI version {int(version_match.group(1))}; all have dispatch cases"
)
