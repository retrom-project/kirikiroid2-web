#!/usr/bin/env python3
"""Exercise Web window placement without constructing the full renderer."""

import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "cpp/plugins/windowEx.cpp").read_text()
source = re.sub(r"//[^\n]*|/\*[\s\S]*?\*/", "", source)
assert re.search(r'RawCallback\(TJS_W\("maximized"\),\s*&Class::getMaximized,\s*&Class::setMaximized', source), \
    "Window.maximized must be registered as a readable and writable property"
scratch_root = ROOT / "out/window-placement-validation"
scratch_root.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=scratch_root) as directory:
    executable = Path(directory) / "placement-test"
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-I", str(ROOT), str(ROOT / "tests/web/web_window_placement.test.cpp"),
        "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)
print("Web window maximized/restore placement tests passed")
