#!/usr/bin/env python3
"""Exercise native KAG save observation without building the Wasm core."""

import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

with tempfile.TemporaryDirectory(prefix="kirikiri-game-save-test-") as directory:
    scratch = Path(directory)
    for name in ("ScriptMgnIntf.h", "base/CCDirector.h", "base/CCScheduler.h", "tjsObject.h",
                 "tjsCommHead.h", "emscripten.h"):
        header = scratch / name
        header.parent.mkdir(parents=True, exist_ok=True)
        header.touch()
    for name in ("host_game_save_observer", "host_game_save_flush"):
        executable = scratch / name
        subprocess.run([
            os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra",
            "-I", str(scratch), "-I", str(ROOT),
            str(ROOT / f"tests/web/{name}.test.cpp"), "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)
print("Web native KAG save observation tests passed")
