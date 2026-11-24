#!/usr/bin/env python3
"""Wrap web/index.html into web_page.h, the string the board serves.

Run after every change to the page:   python3 tools/make_page.py
"""
import pathlib

root = pathlib.Path(__file__).resolve().parent.parent
html = (root / "web" / "index.html").read_text(encoding="utf-8")
assert ")rawpage" not in html
out = (
    "// web_page.h - GENERATED from web/index.html by tools/make_page.py. Do not edit.\n\n"
    "#pragma once\n\n"
    'const char WEB_PAGE[] PROGMEM = R"rawpage(' + html + ')rawpage";\n'
)
(root / "web_page.h").write_text(out, encoding="utf-8")
print(f"web_page.h written ({len(html)} bytes of page)")
