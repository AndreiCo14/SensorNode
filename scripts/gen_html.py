#!/usr/bin/env python3
"""
gen_html.py — gzip-compress data/index.html and write it as a PROGMEM
byte array to src/index_html.h.

Run manually:  python3 scripts/gen_html.py
Or add as a PlatformIO pre: script.
"""

import gzip
import os
import sys

ROOT = os.path.join(os.path.dirname(__file__), "..")
SRC  = os.path.join(ROOT, "data", "index.html")
DST  = os.path.join(ROOT, "src",  "index_html.h")

with open(SRC, "rb") as f:
    raw = f.read()

compressed = gzip.compress(raw, compresslevel=9)

COLS = 16
hex_lines = []
for i in range(0, len(compressed), COLS):
    chunk = compressed[i:i + COLS]
    hex_lines.append("  " + ", ".join(f"0x{b:02x}" for b in chunk))

array_body = ",\n".join(hex_lines)

header = f"""\
// Auto-generated from data/index.html — do not edit manually
// Source: {len(raw)} bytes  →  gzip: {len(compressed)} bytes
#pragma once
#include <pgmspace.h>

static const size_t INDEX_HTML_GZ_LEN = {len(compressed)};
static const uint8_t INDEX_HTML_GZ[] PROGMEM = {{
{array_body}
}};
"""

with open(DST, "w") as f:
    f.write(header)

print(f"gen_html.py: {len(raw)} → {len(compressed)} bytes ({len(compressed)*100//len(raw)}%)")
