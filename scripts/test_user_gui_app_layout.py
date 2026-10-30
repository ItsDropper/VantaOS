#!/usr/bin/env python3
"""Check fixed geometry in built-in GUI apps against the kernel client area."""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
APPS = {
    "notes": ("userspace/apps/notes.c", 760, 460),
    "calculator": ("userspace/apps/calculator.c", 536, 280),
}
INSET_X = 8
INSET_Y = 50
BORDER_X = 8
BORDER_Y = 8
FONT_WIDTH = 8
FONT_HEIGHT = 16


def render_body(source: str) -> str:
    match = re.search(r"\bstatic\s+void\s+render\s*\(\s*void\s*\)\s*\{", source)
    if not match:
        raise ValueError("render(void) function not found")
    start = match.end()
    depth = 1
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index]
    raise ValueError("unterminated render(void) function")


def check_app(name: str, path: str, window_width: int, window_height: int) -> list[str]:
    source = (ROOT / path).read_text(encoding="utf-8")
    body = render_body(source)
    client_width = window_width - 2 * INSET_X
    client_height = window_height - INSET_Y - 8
    errors = []

    rect_pattern = re.compile(
        r"\badd_rect\s*\(\s*(-?\d+)\s*,\s*(-?\d+)\s*,"
        r"\s*(-?\d+)\s*,\s*(-?\d+)\s*,"
    )
    for match in rect_pattern.finditer(body):
        x, y, width, height = map(int, match.groups())
        if x < 0 or y < 0 or width <= 0 or height <= 0 or x + width > client_width or y + height > client_height:
            errors.append(
                f"{name}: rectangle ({x},{y},{width},{height}) exceeds "
                f"{client_width}x{client_height} client area"
            )

    text_pattern = re.compile(
        r"\badd_text\s*\(\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*"
        r'"((?:\\.|[^"\\])*)"'
    )
    for match in text_pattern.finditer(body):
        x, y = int(match.group(1)), int(match.group(2))
        text = bytes(match.group(3), "utf-8").decode("unicode_escape")
        width = len(text) * FONT_WIDTH
        if x < 0 or y < 0 or x + width > client_width or y + FONT_HEIGHT > client_height:
            errors.append(
                f"{name}: text at ({x},{y}) with estimated width {width} "
                f"exceeds {client_width}x{client_height} client area: {text!r}"
            )
    return errors


def main() -> int:
    errors = []
    for name, (path, width, height) in APPS.items():
        errors.extend(check_app(name, path, width, height))
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("VANTAOS_USER_GUI_LAYOUT_OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
