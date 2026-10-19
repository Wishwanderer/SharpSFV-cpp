#!/usr/bin/env python3
# Part of the SharpSFV C++ port (GPL-3.0). Modified 2026-10-03: fewer, smaller images.
# Modified 2026-10-04: PNGs written without row filters at maximum deflate effort (smaller).
# Generates res/SharpSFV.ico (an original design:
# a document with a check mark). Replace the file with the C# project's SharpSFV.ico to use
# the original icon instead.
import sys
from PIL import Image, ImageDraw

def draw(size):
    s = 4  # supersample
    W = size * s
    img = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    m = W * 0.10
    fold = W * 0.24
    page = [(m, m), (W - m - fold, m), (W - m, m + fold), (W - m, W - m), (m, W - m)]
    d.polygon(page, fill=(236, 242, 250, 255), outline=(52, 92, 160, 255))
    d.line(page + [page[0]], fill=(52, 92, 160, 255), width=max(1, int(W * 0.035)))
    d.polygon([(W - m - fold, m), (W - m - fold, m + fold), (W - m, m + fold)], fill=(180, 200, 230, 255))
    for k in range(3):  # text lines
        y = m + W * (0.30 + 0.11 * k)
        d.line([(m + W * 0.12, y), (W - m - W * (0.30 if k == 0 else 0.12), y)], fill=(150, 170, 200, 255),
               width=max(1, int(W * 0.03)))
    cx, cy, r = W * 0.66, W * 0.68, W * 0.25  # check badge
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(30, 150, 70, 255), outline=(255, 255, 255, 255),
              width=max(1, int(W * 0.03)))
    d.line([(cx - r * 0.50, cy + r * 0.02), (cx - r * 0.12, cy + r * 0.42), (cx + r * 0.55, cy - r * 0.40)],
           fill=(255, 255, 255, 255), width=max(2, int(W * 0.06)), joint="curve")
    return img.resize((size, size), Image.LANCZOS)

# Sizes Windows actually asks for (other sizes are scaled from these). Every image is a 32-bit
# RGBA PNG (the format Windows Vista and later read for icons of any size). The larger ones are
# first reduced to 256 distinct colours, which is invisible here (PSNR ~38 dB) but lets PNG
# compress them to half the size.
import io
import struct

# PNG encoding: no row filter (filter type 0) and the strongest deflate available (zopfli if
# installed, otherwise zlib level 9). On these flat images "no filter" beats the adaptive filters
# Pillow picks, and the icon shrinks from 11,975 to about 7,100 bytes with the same pixels.
# Deflated data cannot be compressed again, so this also shrinks a compressed archive of the
# program by the same amount.
import zlib

def deflate(data):
    try:
        import zopfli.zlib
        return zopfli.zlib.compress(data, numiterations=50)
    except ImportError:
        return zlib.compress(data, 9)

def encode_png(img):
    w, h = img.size
    px = img.tobytes()
    raw = b"".join(b"\0" + px[y * w * 4:(y + 1) * w * 4] for y in range(h))
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", deflate(raw)) + chunk(b"IEND", b""))

sizes = [16, 24, 32, 48, 256]
blobs = []
for s in sizes:
    img = draw(s)
    if s >= 32:
        img = img.quantize(colors=256, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE).convert("RGBA")
    blobs.append(encode_png(img))
header = struct.pack("<HHH", 0, 1, len(sizes))
offset = 6 + 16 * len(sizes)
entries = b""
for s, b in zip(sizes, blobs):
    entries += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(b), offset)
    offset += len(b)
with open(sys.argv[1], "wb") as f:
    f.write(header + entries + b"".join(blobs))
