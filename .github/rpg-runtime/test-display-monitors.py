#!/usr/bin/env python3
"""Test the production Web display boundary without the full Wasm engine."""

import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

with tempfile.TemporaryDirectory(prefix="kirikiri-display-test-") as directory:
    scratch = Path(directory)
    # The harness supplies minimal containers, not a simulated TJS interpreter.
    (scratch / "ncbind.hpp").touch()
    executable = scratch / "display-test"
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-I", str(scratch), "-I", str(ROOT),
        str(ROOT / "tests/web/web_display_monitors.test.cpp"),
        "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)
print("Web display monitor boundary tests passed (minimal NCB containers)")
