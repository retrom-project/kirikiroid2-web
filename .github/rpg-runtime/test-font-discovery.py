#!/usr/bin/env python3
"""Compile the production discovery call block with directory/stream seams."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "cpp/core/visual/FontImpl.cpp").read_text()
if "// VN_REPAIR_FONT_DISCOVERY_BEGIN" in source:
    body = source.split("// VN_REPAIR_FONT_DISCOVERY_BEGIN", 1)[1].split("// VN_REPAIR_FONT_DISCOVERY_END", 1)[0]
else:
    body = source.split('// check exePath + "/fonts/*.ttf"', 1)[1].split('if(TVPDefaultFontName.IsEmpty())', 1)[0]
headers = ['#include "cpp/core/visual/FontFileCandidates.h"'] if (ROOT / 'cpp/core/visual/FontFileCandidates.h').exists() else []
scratch_root = ROOT / "out/font-validation"
scratch_root.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=scratch_root) as directory:
    scratch = Path(directory)
    (scratch / "font-discovery-production-includes.h").write_text('\n'.join(headers) + '\n')
    (scratch / "font-discovery-production-body.h").write_text(body)
    executable = scratch / "font-discovery-test"
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-I", str(scratch), "-I", str(ROOT),
        str(ROOT / "tests/web/font_file_discovery.test.cpp"), "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)
