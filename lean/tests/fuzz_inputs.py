#!/usr/bin/env python3
# Part of the SharpSFV C++ port (GPL-3.0). Test tooling, added 2026-10-03.
"""Generates random inputs for the probes' "fuzz" command (differential testing of the lean
build against the standard build). Each line: op<TAB>hex(arg)<TAB>hex(arg)...

Usage: fuzz_inputs.py <count> <seed> > inputs.txt
"""
import random
import sys

count = int(sys.argv[1])
rng = random.Random(int(sys.argv[2]))

ATOMS = [
    "a", "B", "z", "Z", "0", "9", "x", "file", "name", "Data", ".", "..", " ", "  ", "\t", "\\", "/", ":", "*", "?",
    ";", ",", '"', "<", ">", "|", "-", "_", "(", ")", "\r", "\n", "\r\n", "\x00", "\u00a0", "\u2000", "\u3000",
    "\u0085", "\u00e9", "\u00c9", "\u00df", "\u0130", "\u0131", "\u03a3", "\u03c3", "\u4e2d", "\U0001F600",
    "deadbeef", "CBF43926", "d41d8cd98f00b204e9800998ecf8427e", "*.txt", ".sfv", ".TXT", "ab*cd", "?",
]
BAD_BYTES = [b"\xff", b"\xc3", b"\xed\xa0\x80", b"\xf0\x9f\x98", b"\xef\xbf\xbd", b"\xfe\xff", b"\xff\xfe"]
PREFIXES = ["", "", "", "C:", "C:\\", "c:/", "D:\\", "\\", "/", "\\\\srv\\share", "\\\\srv\\share\\", "//srv/share/",
            "\\\\?\\C:\\", "\\\\.\\X\\", "\\\\?\\UNC\\srv\\share\\", "/tmp/", "./", "../"]


def rand_text(maxlen=8):
    b = b""
    for _ in range(rng.randint(0, maxlen)):
        if rng.random() < 0.08:
            b += rng.choice(BAD_BYTES)
        else:
            b += rng.choice(ATOMS).encode("utf-8")
    return b


def rand_path():
    b = rng.choice(PREFIXES).encode()
    for i in range(rng.randint(0, 5)):
        seg = rng.choice(["a", "B", "dir", "Dir", "x.y", ".", "..", "", " ", "f.tar.gz", "\u00e9t\u00e9", "\u00c9T\u00c9",
                          "file.", ".hidden", "c:", "a b", "\u4e2d"])
        b += seg.encode() + rng.choice([b"\\", b"/", b"\\\\", b""]) if i < 4 else seg.encode()
    return b


def hexs(b):
    return b.hex()


def line(op, *args):
    return op + "".join("\t" + hexs(a) for a in args)


ops = []
for _ in range(count):
    k = rng.random()
    if k < 0.08:
        ops.append(line("trim", rand_text()))
    elif k < 0.14:
        ops.append(line("fold", rand_text()))
    elif k < 0.22:
        a = rand_text(4)
        ops.append(line("eqic", a, rng.choice([a, a.upper(), a.lower(), rand_text(4), a[:2]])))
    elif k < 0.27:
        ops.append(line("split", rand_text(), rng.choice([b";,", b";", b"\\", b" ", b""])))
    elif k < 0.31:
        ops.append(line("lines", rand_text(12)))
    elif k < 0.40:
        h = rng.choice([b"CBF43926", b"cbf43926", b"d41d8cd98f00b204e9800998ecf8427e", b"0123456789abcdefg", b"ABC"])
        name = rand_text(4)
        forms = [h + b" *" + name, h + b"  " + name, name + b" " + h, b"  " + name + b"\t" + h + b"  ", h, name,
                 rand_text(6), b" " + h + b" *"]
        ops.append(line("match", rng.choice(forms)))
    elif k < 0.45:
        lines = []
        for _ in range(rng.randint(0, 6)):
            lines.append(rng.choice([b"; c", b"// shown " + rand_text(3), b"file.bin CBF43926",
                                     b"d41d8cd98f00b204e9800998ecf8427e *x", rand_text(5), b"", b"  ",
                                     rand_text(3) + b" 2639F4CB", b"ABC *odd"]))
        sep = rng.choice([b"\r\n", b"\n", b"\r"])
        ops.append(line("parse", rng.choice([b"Crc32", b"MD5", b"SHA1", b"XXHASH3", b"SHA256"]), sep.join(lines)))
    elif k < 0.55:
        name = rand_text(5)
        masks = [b"*.txt", b"*.TXT", b"a*", b"*a*b*", b"?", b"*", b"*.*", b"", b"x?.y", rand_text(3), b"*" + name[-2:]]
        ops.append(line("mask", name, rng.choice(masks) + rng.choice([b"", b";*.bin", b", *.iso"]),
                        rng.choice([b"", b"*.bin", rand_text(2), b"a*"])))
    elif k < 0.62:
        ops.append(line("combine", rand_path(), rand_path()))
    elif k < 0.76:
        ops.append(line("pathparts", rand_path() if rng.random() < 0.8 else rand_text()))
    elif k < 0.84:
        base = rand_path()
        ops.append(line("rel", rng.choice([base, rand_path()]), rng.choice([base + b"/x/y.txt", base + b"\\z", rand_path()])))
    elif k < 0.90:
        ps = [rand_path() for _ in range(rng.randint(0, 4))]
        if ps and rng.random() < 0.5:
            ps = [ps[0] + s for s in [b"\\a", b"\\b\\c", b"/d"]]
        ops.append(line("common", *ps))
    elif k < 0.94:
        bom = rng.choice([b"", b"\xef\xbb\xbf", b"\xff\xfe", b"\xfe\xff", b"\xff\xfe\x00\x00"])
        body = bytes(rng.randrange(256) for _ in range(rng.randint(0, 12))) if rng.random() < 0.5 else rand_text()
        ops.append(line("decode", bom + body))
    elif k < 0.97:
        ops.append(line("int", rng.choice([b"42", b" -42 ", b"+7", b"2147483647", b"2147483648", b"-2147483648",
                                           b"-2147483649", b"12x", b"", b" ", b"\t5\n", b"5\x00", b"--1", rand_text(2)])))
    elif k < 0.985:
        ops.append(line("f1", rng.choice([b"0", b"0.25", b"0.05", b"1.15", b"99.95", b"12.34", b"1e6", b"-0.04",
                                          b"-3.25", b"123456.789", b"0.15"])))
    else:
        ops.append(line("enum", rng.choice([b"crc32", b"CRC32", b"1", b"5", b"-1", b"md5", b"Sha256", b"ssd", b" hdd ",
                                            b"auto", b"QuickSFV", b"little-endian", b".net", b"reversed", b"x"])))
print("\n".join(ops))
