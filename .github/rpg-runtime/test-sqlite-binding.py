#!/usr/bin/env python3
"""Exercise the production factory expression with actual TJS variant overloads."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "cpp/plugins/sqlite/Sqlite.cpp").read_text()
expression = re.search(r"\*result = new Sqlite\(ttstr\(\*arguments\[0\]\), ([^\n]+)\);", source)
assert expression, "test must exercise the actual Sqlite factory readonly expression"
scratch_root = ROOT / "out/sqlite-validation"
scratch_root.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=scratch_root) as directory:
    scratch = Path(directory)
    (scratch / "sqlite-constructor-readonly.h").write_text(
        "static bool constructorReadonly(tjs_int count, tTJSVariant **arguments) { return "
        + expression[1] + "; }\n")
    executable = scratch / "sqlite-binding-test"
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
        "-I", str(scratch), "-isystem", str(ROOT / "cpp/core/tjs2"),
        "-isystem", str(ROOT / "out/web/release/vcpkg_installed/wasm32-emscripten/include"),
        str(ROOT / "tests/web/sqlite_tjs_readonly.test.cpp"), "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)
