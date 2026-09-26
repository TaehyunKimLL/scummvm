#!/usr/bin/env python3
"""XWD (X Window Dump) -> PNG. bytes_per_line(stride)와 RGB 마스크를 올바로 처리."""
import sys, struct
from PIL import Image

src, dst = sys.argv[1], sys.argv[2]
d = open(src, 'rb').read()

# XWDFileHeader (모두 big-endian uint32)
(header_size, version, pixmap_format, pixmap_depth,
 width, height, xoffset, byte_order,
 bitmap_unit, bitmap_bit_order, bitmap_pad, bits_per_pixel,
 bytes_per_line, visual_class, red_mask, green_mask, blue_mask,
 bits_per_rgb, colormap_entries, ncolors) = struct.unpack('>20I', d[:80])

off = header_size + ncolors * 12
px = d[off:]

def shift_of(mask):
    if mask == 0:
        return 0
    s = 0
    while not (mask >> s) & 1:
        s += 1
    return s

rs, gs, bs = shift_of(red_mask), shift_of(green_mask), shift_of(blue_mask)
Bpp = bits_per_pixel // 8

img = Image.new('RGB', (width, height))
out = bytearray(width * height * 3)

for y in range(height):
    row = px[y * bytes_per_line:(y + 1) * bytes_per_line]
    base = y * width * 3
    for x in range(width):
        p = row[x * Bpp:x * Bpp + Bpp]
        if len(p) < Bpp:
            break
        v = int.from_bytes(p, 'big' if byte_order else 'little')
        o = base + x * 3
        out[o]     = (v & red_mask) >> rs
        out[o + 1] = (v & green_mask) >> gs
        out[o + 2] = (v & blue_mask) >> bs

img.frombytes(bytes(out))
img.save(dst)
print(f"saved {dst} {width}x{height} bpp={bits_per_pixel} stride={bytes_per_line}")
