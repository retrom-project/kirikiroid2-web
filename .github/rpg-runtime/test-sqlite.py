#!/usr/bin/env python3
"""Run the real SQLite engine over the production Kiri storage VFS."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
plugin = ROOT / "cpp/plugins/sqlite/Sqlite.cpp"
source = plugin.read_text() if plugin.exists() else ""
source = re.sub(r"//[^\n]*|/\*[\s\S]*?\*/", "", source)
for name in ["Sqlite", "SqliteStatement"]:
    assert re.search(r"NCB_REGISTER_CLASS\(" + name + r"\)", source), \
        f"sqlite3.dll must register the actual {name} class used by KAG"
scratch_root = ROOT / "out/sqlite-validation"
scratch_root.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=scratch_root) as directory:
    for name in ["storage_vfs", "managed_storage"]:
        executable = Path(directory) / name
        subprocess.run([
            os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread",
            "-I", str(ROOT), str(ROOT / f"tests/web/sqlite_{name}.test.cpp"),
            "-lsqlite3", "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)
print("SQLite VFS query, transaction, attachment and fresh-store restore tests passed")
