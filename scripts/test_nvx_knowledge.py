#!/usr/bin/env python3
# Copyright (c) 2026 ItsDropper
# This file is part of VantaOS and is licensed under the MIT License.
# See the LICENSE file in the project root for the full license text.
"""Self-tests for scripts/nvx_knowledge.py (no hardware involved)."""
import importlib.util
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("nvx_knowledge", os.path.join(HERE, "nvx_knowledge.py"))
nk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(nk)

rows = nk.load()
assert not nk.check(rows), nk.check(rows)
assert subprocess.run([sys.executable, os.path.join(HERE, "nvx_knowledge.py"), "gen-survey", "--check"]).returncode == 0

# Predicate semantics.
ev = nk.evaluate
assert ev("not-pri-error", 0, 0x12345678) is True
assert ev("not-pri-error", 0, 0xBADF4100) is False
assert ev("not-pri-error", 0, 0xFFFFFFFF) is False
assert ev("nonzero", 0xFFFFFFFF, 0) is False
assert ev("nonzero", 0xFFFFFFFF, 0xBADF0001) is False
assert ev("eq:0x17600000", 0x3FF00000, 0x17600001) is True
assert ev("eq:0x17600000", 0x3FF00000, 0x16500000) is False
assert ev("ne:0x0", 0xFF, 0x100) is False
assert ev("in:0xAA55,0x564E", 0xFFFF, 0x4947564E) is True
assert ev("none", 0, 0x1234) is None

def log(body, end=True):
    s = "NVXSURVEY begin pci=10de:2503\n" + body
    if end:
        s += f"NVXSURVEY end count={body.count('NVXOBS')}\n"
    return s

by = {r["id"]: r for r in rows}
r1 = by["NVX-F-0001"]
good = f"NVXOBS id=NVX-F-0001 off={r1['offset'].replace('0x','0x')[:2]}{int(r1['offset'],16):08X} val=0x172000A1\n"

# Truncated log (no end marker) must be refused rather than judged.
res, notes = nk.analyze(rows, log(good, end=False))
assert res is None and "truncated" in notes[0]

# A complete log judges only what it observed, and says what is missing.
res, notes = nk.analyze(rows, log(good))
assert res[1]["NVX-F-0001"][0] == "CONFIRMED"
assert any("no observation" in n for n in notes)

# Offset mismatch with the database (stale survey table) is ignored, not trusted.
bad_off = "NVXOBS id=NVX-F-0001 off=0x00000004 val=0x172000A1\n"
res, notes = nk.analyze(rows, log(bad_off))
assert "NVX-F-0001" not in res[1] and any("out of sync" in n for n in notes)

# Read failures are reported, never treated as confirmation.
err = f"NVXOBS id=NVX-F-0001 off=0x{int(r1['offset'],16):08X} error=read-failed\n"
res, _ = nk.analyze(rows, log(err))
assert res[1]["NVX-F-0001"][0] == "READ-FAILED"

# Only the last survey block in a long serial log counts.
old = log("NVXOBS id=NVX-F-0001 off=0x00000000 val=0xBADF0000\n")
res, _ = nk.analyze(rows, old + log(good))
assert res[1]["NVX-F-0001"][0] == "CONFIRMED"

# Database validation catches bad rows.
broken = [dict(rows[0], source_class="vibes")]
assert any("source_class" in e for e in nk.check(broken))
broken = [dict(rows[0]), dict(rows[0])]
assert any("duplicate" in e for e in nk.check(broken))
print("nvx_knowledge self-tests passed")
