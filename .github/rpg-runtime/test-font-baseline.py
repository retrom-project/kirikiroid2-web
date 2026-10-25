#!/usr/bin/env python3
"""Exercise production font baseline seams against real FreeType bitmaps."""
import argparse
import os
from pathlib import Path
import re
import shlex
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument("--heavy-font", type=Path)
parser.add_argument("--body-font", type=Path)
args = parser.parse_args()


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def baseline_declaration(source, signature):
    body = function(source, signature)
    body = re.sub(r"/\*.*?\*/|//[^\n]*", "", body, flags=re.S)
    statements = re.findall(r"\bint baseline\s*=[^;]+;", body)
    assert len(statements) == 1, (signature, statements)
    return statements[0]


def base_table():
    # BASE 1.0 / horizontal axis / DFLT / explicit 256-unit ideographic box.
    # Its declared baseline fits the original Droid glyphs without rescaling.
    return bytes.fromhex(
        "0001 0000 0008 0000 "
        "0004 000e "
        "0002 6964656f 69647470 "
        "0001 44464c54 0008 "
        "0006 0000 0000 "
        "0000 0002 0008 000c "
        "0001 ffe6 0001 00e6"
    )


def fixture_font(source, output, table):
    original = source.read_bytes()
    signature, count = struct.unpack_from(">4sH", original)
    tables = {}
    for at in range(12, 12 + 16 * count, 16):
        tag, _checksum, offset, size = struct.unpack_from(">4sIII", original, at)
        assert offset + size <= len(original)
        tables[tag] = original[offset:offset + size]
    assert b"BASE" not in tables
    tables[b"BASE"] = table
    head = bytearray(tables[b"head"])
    head[8:12] = bytes(4)
    tables[b"head"] = head
    power = 1 << (len(tables).bit_length() - 1)
    header = struct.pack(">4sHHHH", signature, len(tables), power * 16,
                         power.bit_length() - 1, len(tables) * 16 - power * 16)
    directory = bytearray()
    payload = bytearray()
    for tag, data in sorted(tables.items()):
        offset = 12 + 16 * len(tables) + len(payload)
        padded = data + bytes((-len(data)) % 4)
        checksum = sum(struct.unpack(">" + "I" * (len(padded) // 4), padded)) & 0xffffffff
        directory += struct.pack(">4sIII", tag, checksum, offset, len(data))
        payload += padded
    font = bytearray(header + directory + payload)
    checksum = sum(struct.unpack(">" + "I" * (len(font) // 4), font)) & 0xffffffff
    for at in range(12, 12 + 16 * len(tables), 16):
        tag, _checksum, offset, _size = struct.unpack_from(">4sIII", font, at)
        if tag == b"head":
            struct.pack_into(">I", font, offset + 8, (0xb1b0afba - checksum) & 0xffffffff)
    output.write_bytes(font)


header = (ROOT / "cpp/core/visual/FreeType.h").read_text()
driver = (ROOT / "cpp/core/visual/FreeType.cpp").read_text()
constructor = function(driver, "tFreeTypeFace::tFreeTypeFace(")
initialization = re.findall(r"\bLayoutAscent\s*=[^;]+;", constructor)
assert len(initialization) <= 1
methods = [function(header, name) for name in (
    "[[nodiscard]] tjs_int GetAscent() const", "void GetUnderline(", "void GetStrikeOut(")]
bitmap = baseline_declaration(driver, "tTVPCharacterData *tFreeTypeFace::GetGlyphFromCharcode(")
rect = baseline_declaration(driver, "bool tFreeTypeFace::GetGlyphRectFromCharcode(")
has_helper = (ROOT / "cpp/core/visual/FontBaseline.h").exists()
scratch_root = ROOT / "out/font-validation"
scratch_root.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=scratch_root) as directory:
    scratch = Path(directory)
    (scratch / "production-font-baseline.h").write_text(
        ('#include "cpp/core/visual/FontBaseline.h"\n#define HAS_FONT_BASELINE_HELPER 1\n' if has_helper else "") +
        "using tjs_int = int;\nclass ProductionFace {\npublic:\nFT_Face FTFace;\nint LayoutAscent;\n"
        "explicit ProductionFace(FT_Face face): FTFace(face), LayoutAscent(face->ascender) {\n" +
        "\n".join(initialization) + "\n}\n"
        "static int FT_PosToInt(int value) { return (value+32)>>6; }\n" +
        "\n".join(methods) +
        "\nint bitmapBaseline() const { " + bitmap + " return baseline; }\n"
        "int rectBaseline() const { " + rect + " return baseline; }\n};\n"
    )
    generated_font = scratch / "declared-em-box.ttf"
    bundled = ROOT / "ui/cocos-studio/DroidSansFallback.ttf"
    fixture_font(bundled, generated_font, base_table())
    invalid = bytearray(base_table())
    invalid[4:6] = b"\xff\xff"
    fixture_font(bundled, scratch / "invalid-offset.ttf", invalid)
    invalid = bytearray(base_table())
    invalid[-4:-2] = b"\x00\x03"
    fixture_font(bundled, scratch / "unsupported-coordinate.ttf", invalid)
    fixture_font(bundled, scratch / "oversized-base.ttf", base_table() + bytes(65537))
    (scratch / "base-table.bin").write_bytes(base_table())
    pkg_flags = shlex.split(subprocess.check_output(
        ["pkg-config", "--cflags", "--libs", "freetype2"], text=True))
    executable = scratch / "font-baseline-test"
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror", "-D__EMSCRIPTEN__",
        "-I", str(ROOT), "-I", str(scratch),
        str(ROOT / "tests/web/font_baseline.test.cpp"), "-o", str(executable), *pkg_flags,
    ], check=True)
    subprocess.run([
        str(executable), str(generated_font), str(bundled), str(scratch / "base-table.bin"),
        str(args.heavy_font.resolve()) if args.heavy_font else "",
        str(args.body_font.resolve()) if args.body_font else "",
        str(ROOT / "ui/cocos-studio/NotoSansSC-Regular.otf"),
    ], check=True)
