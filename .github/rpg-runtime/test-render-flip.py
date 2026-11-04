#!/usr/bin/env python3
"""Run production software copy code against the existing PWA image pixels."""
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[2]


def icon_pixels():
    data = (ROOT / "platforms/web/pwa/icon-192.png").read_bytes()
    width, height, depth, color, _, _, interlace = struct.unpack(
        ">IIBBBBB", data[16:29])
    assert (depth, color, interlace) == (8, 2, 0)
    compressed = bytearray()
    offset = 8
    while offset < len(data):
        size = int.from_bytes(data[offset:offset + 4], "big")
        if data[offset + 4:offset + 8] == b"IDAT":
            compressed.extend(data[offset + 8:offset + 8 + size])
        offset += size + 12
    packed = zlib.decompress(compressed)
    stride = width * 3
    previous = bytearray(stride)
    rgba = bytearray()
    for y in range(height):
        start = y * (stride + 1)
        kind = packed[start]
        row = bytearray(packed[start + 1:start + 1 + stride])
        for x in range(stride):
            left = row[x - 3] if x >= 3 else 0
            above = previous[x]
            upper_left = previous[x - 3] if x >= 3 else 0
            p = left + above - upper_left
            distances = (abs(p - left), abs(p - above), abs(p - upper_left))
            paeth = (left, above, upper_left)[distances.index(min(distances))]
            predictor = (0, left, above, (left + above) // 2, paeth)[kind]
            row[x] = (row[x] + predictor) & 255
        for x in range(0, stride, 3):
            rgba.extend((*row[x:x + 3], 255))
        previous = row
    return width, height, rgba


source = (ROOT / "cpp/core/visual/RenderManager.cpp").read_text()
copy_class = source.split("class tTVPRenderMethod_DirectCopy :", 1)[1].split(
    "class tTVPRenderMethod_DoGrayScale", 1)[0]
width, height, pixels = icon_pixels()
with tempfile.TemporaryDirectory(prefix="kirikiri-render-flip-") as directory:
    scratch = Path(directory)
    (scratch / "production-copy.h").write_text(
        "class tTVPRenderMethod_DirectCopy :" + copy_class)
    fixture = scratch / "icon.rgba"
    fixture.write_bytes(pixels)
    executable = scratch / "render-flip-test"
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra",
        "-Wno-unused-parameter", "-Werror", "-I", str(scratch),
        str(ROOT / "tests/web/software_render_flip.test.cpp"),
        "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable), str(fixture), str(width), str(height)],
                   check=True)
print("Software vertical copies preserve source pixels and lifetime")
