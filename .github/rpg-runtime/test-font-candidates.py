#!/usr/bin/env python3
"""Run the actual font candidate selector with minimal UTF-16 containers."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


scratch_root = ROOT / "out/font-validation"
scratch_root.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=scratch_root) as directory:
    scratch = Path(directory)
    (scratch / "tjsCommHead.h").write_text('#include "tests/web/font_system_fixture.h"\n')
    (scratch / "tjsHashSearch.h").touch()
    (scratch / "MsgIntf.h").touch()
    (scratch / "ConfigManager").mkdir()
    (scratch / "ConfigManager/IndividualConfigManager.h").touch()
    (scratch / "production-font-real-family.h").write_text(function(
        (ROOT / "cpp/core/visual/FontImpl.cpp").read_text(),
        "ttstr TVPFindRegisteredSongtiFont("))
    executable = scratch / "font-candidates-test"
    for web in (False, True):
        subprocess.run([
            os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
            *(["-D__EMSCRIPTEN__"] if web else []),
            "-I", str(scratch), "-I", str(ROOT), "-isystem", str(ROOT / "cpp/core/utils"),
            str(ROOT / "tests/web/font_system_candidates.test.cpp"), "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)
        if web:
            for mode in ("force-default", "unavailable-fallback"):
                subprocess.run([str(executable), mode], check=True)
