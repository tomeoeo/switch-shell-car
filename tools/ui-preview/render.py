#!/usr/bin/env python3
"""Render .ans files from preview.c into HTML (80x45 cells, 16x16 px).

Usage: render.py DIR   -> DIR/<name>.html for every DIR/<name>.ans
Understands the subset ui.c emits: CUP (ESC[r;cH), SGR 0, 1, 38;2;r;g;b,
48;2;r;g;b. Screenshot the HTML at 1280x720 (see Makefile ui-preview).
The browser's monospace font differs from the Switch font; layout and
colors are exact, glyph shapes are not.
"""

import html
import pathlib
import re
import sys

COLS, ROWS = 80, 45
ESC = re.compile(r"\x1b\[([0-9;]*)([A-Za-z])")


def render(text: str) -> list[list[tuple[str, str, str, bool]]]:
    grid = [[(" ", "#eeeeee", "#000000", False) for _ in range(COLS)] for _ in range(ROWS)]
    row = col = 0
    fg, bg, bold = "#aaaaaa", "#000000", False
    i = 0
    while i < len(text):
        m = ESC.match(text, i)
        if m:
            args = [int(a) if a else 0 for a in m.group(1).split(";")] if m.group(1) else []
            cmd = m.group(2)
            if cmd in "Hf":
                row = (args[0] if args else 1) - 1
                col = (args[1] if len(args) > 1 else 1) - 1
            elif cmd == "J":  # libnx clears with the current colors
                grid = [[(" ", fg, bg, False) for _ in range(COLS)] for _ in range(ROWS)]
            elif cmd == "m":
                k = 0
                while k < len(args) or (k == 0 and not args):
                    a = args[k] if args else 0
                    if a == 0:
                        fg, bg, bold = "#aaaaaa", "#000000", False
                    elif a == 1:
                        bold = True
                    elif a in (38, 48) and k + 4 < len(args) + 0 and args[k + 1] == 2:
                        r, g, b = args[k + 2 : k + 5]
                        c = f"#{r:02x}{g:02x}{b:02x}"
                        if a == 38:
                            fg = c
                        else:
                            bg = c
                        k += 4
                    k += 1
                    if not args:
                        break
            i = m.end()
            continue
        ch = text[i]
        i += 1
        if ch == "\n":
            row, col = row + 1, 0
            continue
        if 0 <= row < ROWS and 0 <= col < COLS:
            grid[row][col] = (ch, fg, bg, bold)
        col += 1
        if col >= COLS:
            row, col = row + 1, 0
    return grid


def to_html(grid) -> str:
    out = [
        "<!doctype html><meta charset=utf-8><style>"
        "body{margin:0;background:#000}"
        ".s{display:grid;grid-template-columns:repeat(80,16px);grid-auto-rows:16px;width:1280px}"
        ".s span{display:block;width:16px;height:16px;line-height:16px;text-align:center;"
        "font:15px/16px 'DejaVu Sans Mono',monospace;white-space:pre}"
        # The Switch font has 16 px wide glyphs; stretch the browser's narrow ones.
        ".s span i{display:inline-block;font-style:normal;transform:scaleX(1.45)}"
        "</style><div class=s>"
    ]
    for r in grid:
        for ch, fg, bg, _bold in r:  # libnx ignores bold for 24-bit colors
            out.append(f'<span style="color:{fg};background:{bg}"><i>{html.escape(ch)}</i></span>')
    out.append("</div>")
    return "".join(out)


def main() -> int:
    d = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    for ans in sorted(d.glob("*.ans")):
        ans.with_suffix(".html").write_text(to_html(render(ans.read_text(errors="replace"))))
        print(ans.with_suffix(".html"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
