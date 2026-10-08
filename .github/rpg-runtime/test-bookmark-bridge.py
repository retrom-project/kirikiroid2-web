#!/usr/bin/env python3
"""Compile and exercise the Web host bookmark bridge without the Wasm engine."""

import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

with tempfile.TemporaryDirectory(prefix="kirikiri-bookmark-test-") as directory:
    scratch = Path(directory)
    for name in ("ScriptMgnIntf.h", "EventIntf.h", "tjsObject.h",
                 "tjsCommHead.h", "emscripten.h"):
        header = scratch / name
        header.parent.mkdir(parents=True, exist_ok=True)
        header.touch()
    executable = scratch / "bookmark-test"
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra",
        "-I", str(scratch), "-I", str(ROOT),
        str(ROOT / "tests/web/host_bookmark_bridge.test.cpp"),
        "-o", str(executable),
    ], check=True)
    result = subprocess.run([str(executable)], check=True, capture_output=True, text=True)
    assert "[bookmark] script exception: bookmark test failure" in result.stderr, result.stderr
    assert "[bookmark] unknown script exception" in result.stderr, result.stderr
print("Web host bookmark readiness tests passed")
