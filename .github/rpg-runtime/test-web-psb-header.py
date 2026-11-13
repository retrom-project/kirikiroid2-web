#!/usr/bin/env python3
"""Validate existing PSBs; optional private inputs are never copied into Git."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
scratch = ROOT / "out/psb-header-validation"
scratch.mkdir(parents=True, exist_ok=True)
public_input = ROOT / "tests/test_files/emote/e-mote3.0バニラパジャマa.psb"
with tempfile.TemporaryDirectory(dir=scratch) as directory:
    executable = Path(directory) / "psb-header-test"
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-I", str(ROOT), str(ROOT / "tests/web/web_psb_header.test.cpp"),
        "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable), str(public_input), *sys.argv[1:]], check=True)
print("Web PSB header validation passed; inputs:", 1 + len(sys.argv[1:]))
