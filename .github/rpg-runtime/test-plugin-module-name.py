#!/usr/bin/env python3
"""Verify the browser's native plugin storage-to-module boundary."""

import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
scratch = ROOT / "out/plugin-module-validation"
scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=scratch) as directory:
    executable = Path(directory) / "plugin-module-test"
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-I", str(ROOT), str(ROOT / "tests/web/web_plugin_module_name.test.cpp"),
        "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)
print("Web plugin module name tests passed")
