#!/usr/bin/env python3
"""Exercise the production Web virtual fullscreen state and transform."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="kirikiri-window-presentation-") as directory:
    executable = Path(directory) / "presentation-test"
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-I", str(ROOT), str(ROOT / "tests/web/web_window_presentation.test.cpp"),
        "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)
print("Web virtual fullscreen state and presentation tests passed")
