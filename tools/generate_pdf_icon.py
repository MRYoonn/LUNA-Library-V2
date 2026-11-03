#!/usr/bin/env python3
"""Deterministic LUNA PDF association ICO (stdlib only; no user documents).

The app's blue LUNA-Moon.ico resource is deliberately unchanged. This red
document-fold/moon/PDF artwork is a separate Windows Explorer file icon.
"""
from __future__ import annotations
import argparse
import math
from pathlib import Path
import struct
import zlib

SIZES = (16, 24, 32, 48, 64, 128, 256)
PNG_MAGIC = b"\x89PNG\r\n\x1a\n"
FONT = {
    "P": ("11110", "10001", "10001", "11110", "10000", "10000", "10000"),
    "D": ("11110", "10001", "10001", "10001", "10001", "10001", "11110"),
    "F": ("11111", "10000", "10000", "11110", "10000", "10000", "10000"),
}


def mix(previous, next_color, amount):
    return tuple(round(a * (1-amount) + b * amount)
                 for a, b in zip(previous, next_color))


def rounded_distance(x, y, left, top, right, bottom, radius):
    cx, cy = (left+right)/2, (top+bottom)/2
    dx = abs(x-cx) - (right-left)/2 + radius
    dy = abs(y-cy) - (bottom-top)/2 + radius
    return math.hypot(max(dx, 0), max(dy, 0)) + min(max(dx, dy), 0) - radius


def polygon(x, y, points):
    inside = False
    for i in range(len(points)):
        x1, y1 = points[i]
        x2, y2 = points[(i + 1) % len(points)]
        if ((y1 > y) != (y2 > y)) and x < (x2-x1)*(y-y1)/(y2-y1)+x1:
            inside = not inside
    return inside


def letter_pixel(x, y):
    start_x, start_y, cell_width, cell_height, advance = 59, 186, 7.0, 6.5, 47
    if y < start_y or y >= start_y + 7*cell_height:
        return False
    n = int((x-start_x)//advance)
    if n < 0 or n > 2:
        return False
    column = int((x-start_x-n*advance)//cell_width)
    row = int((y-start_y)//cell_height)
    return 0 <= column < 5 and 0 <= row < 7 and FONT["PDF"[n]][row][column] == "1"


def render(size):
    source_scale = 256 / size
    image = bytearray()
    for py in range(size):
        for px in range(size):
            sums = [0., 0., 0., 0.]
            for sy in (0.25, 0.75):
                y = (py+sy)*source_scale
                for sx in (0.25, 0.75):
                    x = (px+sx)*source_scale
                    color = (255, 255, 255)
                    alpha = 0.
                    shadow = rounded_distance(x, y, 23, 17, 232, 247, 23)
                    if shadow < 7:
                        alpha = max(0, 0.20*(1-max(shadow, 0)/7))
                        color = (176, 56, 68)
                    page = rounded_distance(x, y, 22, 10, 231, 242, 23)
                    if page <= 1:
                        color = (255, 247, 249)
                        alpha = 1
                        if page > -2.3:
                            color = (243, 177, 185)
                        if x < 32 or y > 232:
                            color = mix(color, (255, 223, 228), 0.27)
                    if polygon(x, y, ((179, 11), (230, 62),
                                      (230, 38), (204, 11))):
                        color = (211, 9, 34) if x > 209 else (239, 33, 53)
                        alpha = 1
                    if polygon(x, y, ((181, 12), (181, 53), (229, 65))):
                        color, alpha = (255, 184, 191), 1
                    moon = math.hypot(x-125, y-116)
                    hollow = math.hypot(x-150, y-86)
                    if moon <= 67 and hollow >= 60:
                        light = max(0, min(1, (x-67)/113))
                        color = (int(181+67*light), int(4+30*light),
                                 int(25+36*light))
                        alpha = 1
                    if 63 < moon <= 67 and hollow >= 60 and y < 141:
                        color = mix(color, (251, 117, 125), 0.32)
                    if letter_pixel(x, y):
                        color, alpha = (204, 10, 35), 1
                    sums[3] += alpha
                    for i in range(3):
                        sums[i] += color[i]*alpha
            opacity = round(sums[3]/4*255)
            pixel = tuple(round(v/sums[3]) if sums[3] else 0 for v in sums[:3])
            image.extend(pixel+(opacity,))
    return bytes(image)


def chunk(kind, value):
    return (struct.pack(">I", len(value)) + kind + value +
            struct.pack(">I", zlib.crc32(kind+value)&0xffffffff))


def png(size, pixels):
    rows = b"".join(b"\0"+pixels[y*size*4:(y+1)*size*4]
                    for y in range(size))
    return (PNG_MAGIC + chunk(b"IHDR", struct.pack(">IIBBBBB",size,size,8,6,0,0,0)) +
            chunk(b"IDAT",zlib.compress(rows,9))+chunk(b"IEND",b""))


def make_icon():
    images = [(size, png(size, render(size))) for size in SIZES]
    entries = bytearray(struct.pack("<HHH",0,1,len(images)))
    body = bytearray()
    offset = 6+16*len(images)
    for size,image in images:
        entries.extend(struct.pack("<BBBBHHII",
            size if size < 256 else 0,size if size < 256 else 0,
            0,0,1,32,len(image),offset+len(body)))
        body.extend(image)
    return bytes(entries+body)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output",type=Path)
    options = parser.parse_args()
    options.output.parent.mkdir(parents=True,exist_ok=True)
    options.output.write_bytes(make_icon())
    print("LUNA document icon generated:",options.output,SIZES)


if __name__ == "__main__":
    main()
