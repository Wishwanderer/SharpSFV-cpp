#!/usr/bin/env python3
# Part of the SharpSFV C++ port (GPL-3.0). Test tooling.
"""Independent SFV checker: verifies an .sfv file the way QuickSFV/RapidCRC/7-Zip do, using
Python's zlib CRC-32 printed in the conventional (Standard, most-significant byte first)
order. Accepts "name CRC" and "CRC *name" lines. Exit code 0 only if every entry is OK.

Usage: zlib_sfv_check.py <file.sfv>
"""
import os
import re
import sys
import zlib

sfv = sys.argv[1]
base = os.path.dirname(os.path.abspath(sfv))
ok = bad = missing = 0
with open(sfv, encoding="utf-8-sig") as f:
    for line in f:
        line = line.rstrip("\r\n")
        if not line.strip() or line.lstrip().startswith(";"):
            continue
        m = re.match(r"^\s*(.*?)\s+([0-9A-Fa-f]{8})\s*$", line) or None
        if m:
            name, crc = m.group(1), m.group(2)
        else:
            m = re.match(r"^\s*([0-9A-Fa-f]{8})\s+\*?(.*?)\s*$", line)
            if not m:
                continue
            crc, name = m.group(1), m.group(2)
        p = os.path.join(base, name.replace("\\", os.sep))
        if not os.path.isfile(p):
            missing += 1
            continue
        with open(p, "rb") as fh:
            actual = f"{zlib.crc32(fh.read()) & 0xFFFFFFFF:08X}"
        if actual == crc.upper():
            ok += 1
        else:
            bad += 1
print(f"zlib check: OK={ok} BAD={bad} MISSING={missing}")
sys.exit(0 if bad == 0 and missing == 0 and ok > 0 else 1)
