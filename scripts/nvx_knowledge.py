#!/usr/bin/env python3
# Copyright (c) 2026 ItsDropper
# This file is part of VantaOS and is licensed under the MIT License.
# See the LICENSE file in the project root for the full license text.
"""NVX hardware-knowledge database tooling.

The database (docs/gpu/nvx/knowledge/facts.tsv) records every hardware fact NVX
relies on, where it came from, and whether it has been independently
established. See docs/gpu/nvx/INDEPENDENCE.md.

  check              validate schema, code references and fact tags in sources
  report             print the independence debt (facts not yet established)
  gen-survey         (re)generate include/nvx_survey_table.h from the database
  gen-survey --check fail if the generated header is out of date
  analyze LOG [--write-status]
                     compare a serial log from `nvx survey` with expectations
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DB = os.path.join(ROOT, "docs", "gpu", "nvx", "knowledge", "facts.tsv")
TABLE = os.path.join(ROOT, "include", "nvx_survey_table.h")
COLUMNS = ["id", "domain", "name", "offset", "mask", "expect", "source_class",
           "source_ref", "status", "code_ref", "survey", "writes", "note"]
SOURCE_CLASSES = {"official-doc", "public-spec", "external-impl", "experiment"}
STATUSES = {"unverified", "observed-consistent", "contradicted", "hardware-verified"}
ID_RE = re.compile(r"^NVX-F-\d{4}$")


def load(path=DB):
    with open(path, encoding="utf-8") as f:
        lines = [l.rstrip("\n") for l in f if l.strip()]
    if lines[0].split("\t") != COLUMNS:
        raise SystemExit("facts.tsv: header does not match expected columns")
    rows = []
    for n, line in enumerate(lines[1:], start=2):
        cells = line.split("\t")
        if len(cells) != len(COLUMNS):
            raise SystemExit(f"facts.tsv:{n}: expected {len(COLUMNS)} columns, got {len(cells)}")
        rows.append(dict(zip(COLUMNS, cells)))
    return rows


def parse_u32(text):
    value = int(text, 0)
    if not 0 <= value <= 0xFFFFFFFF:
        raise ValueError(text)
    return value


def validate_expect(expect):
    if expect in ("", "none", "nonzero", "not-pri-error"):
        return True
    kind, _, arg = expect.partition(":")
    if kind in ("eq", "ne"):
        parse_u32(arg)
        return True
    if kind == "in":
        return bool([parse_u32(a) for a in arg.split(",")])
    return False


def check(rows):
    errors = []
    seen = set()
    for r in rows:
        i = r["id"]
        if not ID_RE.match(i):
            errors.append(f"{i}: bad id format")
        if i in seen:
            errors.append(f"{i}: duplicate id")
        seen.add(i)
        if r["source_class"] not in SOURCE_CLASSES:
            errors.append(f"{i}: unknown source_class {r['source_class']!r}")
        if r["status"] not in STATUSES:
            errors.append(f"{i}: unknown status {r['status']!r}")
        if not r["source_ref"]:
            errors.append(f"{i}: missing source_ref")
        if not os.path.exists(os.path.join(ROOT, r["code_ref"])):
            errors.append(f"{i}: code_ref {r['code_ref']} does not exist")
        if r["survey"] not in ("0", "1") or r["writes"] not in ("0", "1"):
            errors.append(f"{i}: survey/writes must be 0 or 1")
        try:
            if r["offset"]:
                off = parse_u32(r["offset"])
                if off % 4:
                    errors.append(f"{i}: offset not dword aligned")
            if r["mask"]:
                parse_u32(r["mask"])
            if not validate_expect(r["expect"]):
                errors.append(f"{i}: bad expect {r['expect']!r}")
        except ValueError:
            errors.append(f"{i}: unparsable number")
        if r["survey"] == "1" and not (r["offset"] and r["mask"] and r["expect"]):
            errors.append(f"{i}: surveyed facts need offset, mask and expect")
        if r["status"] in ("observed-consistent", "hardware-verified") and r["source_class"] == "experiment":
            pass
    # Fact tags used in source/test files must exist in the database.
    tag = re.compile(r"NVX-F-\d{4}")
    for base in ("drivers", "include", "scripts"):
        for dirpath, _, files in os.walk(os.path.join(ROOT, base)):
            for fn in files:
                if not fn.endswith((".c", ".h", ".py")) or fn == "nvx_knowledge.py":
                    continue
                with open(os.path.join(dirpath, fn), encoding="utf-8", errors="replace") as f:
                    for m in tag.finditer(f.read()):
                        if m.group(0) not in seen:
                            errors.append(f"{os.path.relpath(os.path.join(dirpath, fn), ROOT)}: unknown fact {m.group(0)}")
    return errors


def report(rows):
    total = len(rows)
    by = {}
    for r in rows:
        by.setdefault((r["source_class"], r["status"]), 0)
        by[(r["source_class"], r["status"])] += 1
    print(f"NVX knowledge base: {total} facts")
    for (sc, st), n in sorted(by.items()):
        print(f"  {sc:14s} {st:20s} {n}")
    debt = [r for r in rows if r["source_class"] == "external-impl" and r["status"] in ("unverified", "contradicted")]
    print(f"\nIndependence debt: {len(debt)} facts rest only on an external implementation")
    for r in debt:
        flag = "WRITES" if r["writes"] == "1" else "      "
        print(f"  {r['id']} {flag} {r['name']}  [{r['source_ref']}]")
    risky = [r for r in debt if r["writes"] == "1"]
    print(f"\n{len(risky)} of them gate register writes and must not be trusted on hardware before an experiment establishes them.")
    return risky


def gen_table(rows):
    out = ["/*",
           " * Copyright (c) 2026 ItsDropper",
           " * This file is part of VantaOS and is licensed under the MIT License.",
           " * See the LICENSE file in the project root for the full license text.",
           " *",
           " * GENERATED by scripts/nvx_knowledge.py gen-survey from",
           " * docs/gpu/nvx/knowledge/facts.tsv. Do not edit by hand.",
           " */",
           "#ifndef VANTA_NVX_SURVEY_TABLE_H",
           "#define VANTA_NVX_SURVEY_TABLE_H",
           "",
           "#include <stdint.h>",
           "",
           "typedef struct nvx_survey_entry {",
           "    const char *fact_id;",
           "    uint32_t offset;",
           "} nvx_survey_entry_t;",
           "",
           "static const nvx_survey_entry_t NVX_SURVEY_TABLE[] = {"]
    for r in rows:
        if r["survey"] == "1":
            out.append(f'    {{ "{r["id"]}", {r["offset"].upper().replace("0X", "0x")}U }},')
    out += ["};", "", "#define NVX_SURVEY_TABLE_COUNT (sizeof(NVX_SURVEY_TABLE) / sizeof(NVX_SURVEY_TABLE[0]))",
            "", "#endif", ""]
    return "\n".join(out)


def evaluate(expect, mask, value):
    """Return True/False, or None when the fact is observation-only."""
    if expect in ("", "none"):
        return None
    if expect == "not-pri-error":
        return (value >> 16) != 0xBADF and value != 0xFFFFFFFF
    masked = value & mask
    kind, _, arg = expect.partition(":")
    if expect == "nonzero":
        return masked != 0 and (value >> 16) != 0xBADF and value != 0xFFFFFFFF
    if kind == "eq":
        return masked == parse_u32(arg)
    if kind == "ne":
        return masked != parse_u32(arg)
    if kind == "in":
        return masked in [parse_u32(a) for a in arg.split(",")]
    raise ValueError(expect)


OBS = re.compile(r"NVXOBS id=(NVX-F-\d{4}) off=(0x[0-9A-Fa-f]{8}) (val=(0x[0-9A-Fa-f]{8})|error=(\S+))")
BEGIN = re.compile(r"NVXSURVEY begin pci=([0-9a-fA-F]{4}:[0-9a-fA-F]{4})")
END = re.compile(r"NVXSURVEY end count=(\d+)")


def analyze(rows, text):
    by_id = {r["id"]: r for r in rows}
    begins = BEGIN.findall(text)
    ends = END.findall(text)
    if not begins or not ends:
        return None, ["log has no complete NVXSURVEY begin/end block; refusing to judge a truncated run"]
    notes, results = [], {}
    start = text.rindex("NVXSURVEY begin")
    block = text[start:]
    for m in OBS.finditer(block):
        fid, off, _, val, err = m.groups()
        r = by_id.get(fid)
        if r is None:
            notes.append(f"{fid}: not in database, ignored")
            continue
        if parse_u32(off) != parse_u32(r["offset"]):
            notes.append(f"{fid}: logged offset {off} differs from database {r['offset']}; survey table out of sync, ignored")
            continue
        if err:
            results[fid] = ("READ-FAILED", None)
            continue
        value = parse_u32(val)
        verdict = evaluate(r["expect"], parse_u32(r["mask"]), value)
        results[fid] = ("OBSERVED" if verdict is None else ("CONFIRMED" if verdict else "CONTRADICTED"), value)
    expected_count = int(ends[-1])
    if expected_count != len(results):
        notes.append(f"log declares {expected_count} observations but {len(results)} were parsed")
    for r in rows:
        if r["survey"] == "1" and r["id"] not in results:
            notes.append(f"{r['id']}: no observation in this run")
    return (begins[-1], results), notes


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("check")
    sub.add_parser("report")
    g = sub.add_parser("gen-survey")
    g.add_argument("--check", action="store_true")
    a = sub.add_parser("analyze")
    a.add_argument("log")
    a.add_argument("--write-status", action="store_true")
    args = ap.parse_args(argv)
    rows = load()
    if args.cmd == "check":
        errs = check(rows)
        for e in errs:
            print("ERROR:", e)
        print(f"{len(rows)} facts checked, {len(errs)} errors")
        return 1 if errs else 0
    if args.cmd == "report":
        report(rows)
        return 0
    if args.cmd == "gen-survey":
        text = gen_table(rows)
        if args.check:
            current = open(TABLE, encoding="utf-8").read() if os.path.exists(TABLE) else ""
            if current != text:
                print("include/nvx_survey_table.h is out of date; run scripts/nvx_knowledge.py gen-survey")
                return 1
            return 0
        with open(TABLE, "w", encoding="utf-8") as f:
            f.write(text)
        return 0
    if args.cmd == "analyze":
        with open(args.log, encoding="utf-8", errors="replace") as f:
            text = f.read()
        res, notes = analyze(rows, text)
        if res is None:
            for n in notes:
                print("ERROR:", n)
            return 2
        pci, results = res
        print(f"Survey of PCI device {pci}")
        by_id = {r["id"]: r for r in rows}
        for fid in sorted(results):
            verdict, value = results[fid]
            r = by_id[fid]
            shown = "-" if value is None else f"0x{value:08X}"
            print(f"  {fid} {verdict:12s} {shown}  {r['name']}  (expect {r['expect'] or 'none'}, source {r['source_class']})")
        for n in notes:
            print("  note:", n)
        contradicted = [f for f, (v, _) in results.items() if v == "CONTRADICTED"]
        if args.write_status:
            for r in rows:
                v = results.get(r["id"], (None,))[0]
                if v == "CONFIRMED" and r["status"] == "unverified":
                    r["status"] = "observed-consistent"
                elif v == "CONTRADICTED":
                    r["status"] = "contradicted"
            with open(DB, "w", encoding="utf-8") as f:
                f.write("\t".join(COLUMNS) + "\n")
                for r in rows:
                    f.write("\t".join(r[c] for c in COLUMNS) + "\n")
            print("database statuses updated")
        return 1 if contradicted else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
