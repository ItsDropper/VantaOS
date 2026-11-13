#!/usr/bin/env python3
# Copyright (c) 2026 ItsDropper
# This file is part of VantaOS and is licensed under the MIT License.
# See the LICENSE file in the project root for the full license text.
"""Check that relative Markdown links and in-page anchors resolve.

External (http/https/mailto) links are not fetched. Links inside fenced code
blocks and inline code are ignored. Exit status 1 if any link is broken.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LINK = re.compile(r"(?<!\!)\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)|!\[[^\]]*\]\(([^)\s]+)\)")
HEADING = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")


def slug(text):
    text = re.sub(r"`([^`]*)`", r"\1", text).strip().lower()
    text = re.sub(r"[^\w\- ]", "", text, flags=re.UNICODE)
    return text.replace(" ", "-")


def anchors(path, cache={}):
    if path in cache:
        return cache[path]
    found, counts, fence = set(), {}, False
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.lstrip().startswith("```"):
                fence = not fence
                continue
            if fence:
                continue
            m = HEADING.match(line)
            if m:
                s = slug(m.group(2))
                n = counts.get(s, 0)
                counts[s] = n + 1
                found.add(s if n == 0 else f"{s}-{n}")
    cache[path] = found
    return found


def markdown_files():
    for base in ("docs",):
        for dp, _, fs in os.walk(os.path.join(ROOT, base)):
            for fn in fs:
                if fn.endswith(".md"):
                    yield os.path.join(dp, fn)
    for fn in os.listdir(ROOT):
        if fn.endswith(".md"):
            yield os.path.join(ROOT, fn)


def main():
    bad = 0
    for path in sorted(markdown_files()):
        fence = False
        with open(path, encoding="utf-8", errors="replace") as f:
            for n, line in enumerate(f, 1):
                if line.lstrip().startswith("```"):
                    fence = not fence
                    continue
                if fence:
                    continue
                scrubbed = re.sub(r"`[^`]*`", "", line)
                for m in LINK.finditer(scrubbed):
                    target = m.group(1) or m.group(2)
                    if re.match(r"^[a-z][a-z0-9+.-]*:", target, re.I) or target.startswith("#") is False and target.startswith("//"):
                        continue
                    ref, _, frag = target.partition("#")
                    dest = path if not ref else os.path.normpath(os.path.join(os.path.dirname(path), ref))
                    rel = os.path.relpath(path, ROOT)
                    if not os.path.exists(dest):
                        print(f"{rel}:{n}: broken link {target}")
                        bad += 1
                    elif frag and dest.endswith(".md") and frag.lower() not in anchors(dest):
                        print(f"{rel}:{n}: missing anchor #{frag} in {os.path.relpath(dest, ROOT)}")
                        bad += 1
    print(f"documentation links checked, {bad} broken")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
