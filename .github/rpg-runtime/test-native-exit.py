#!/usr/bin/env python3
"""Run the production exit entry against the Web/desktop lifetime boundary."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "cpp/core/base/impl/SysInitImpl.cpp").read_text()
signature = "void TVPTerminateSync(int code) {"
body = signature + source.split(signature, 1)[1].split("\n}", 1)[0] + "\n}\n"
fixture = r"""#include <stdexcept>
#define TJS_W(value) L##value
struct EAbort { explicit EAbort(const wchar_t *) {} };
int destroyed = 0, ignored = 0, requested = 0, status = -1;
void TVPSystemUninit() { ++destroyed; }
void TVPExitApplication(int code) { ++ignored; status = code; }
void TVPTerminateAsync(int code) { ++requested; status = code; }
"""
checks = r"""int main() {
    bool unwound = false;
    try { TVPTerminateSync(0); } catch(const EAbort &) { unwound = true; }
#ifdef __EMSCRIPTEN__
    if(destroyed || ignored || requested != 1 || !unwound || status != 0)
        throw std::runtime_error("Web System.exit destroyed the script engine before Host final save and tick drain");
#else
    if(destroyed != 1 || ignored != 1 || requested || unwound || status != 0)
        throw std::runtime_error("native desktop exit semantics changed");
#endif
}
"""
with tempfile.TemporaryDirectory(prefix="kirikiri-native-exit-") as directory:
    scratch = Path(directory)
    test = scratch / "native-exit.cpp"
    test.write_text(fixture + body + checks)
    for platform in ("web", "native"):
        executable = scratch / platform
        subprocess.run([
            os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
            *(["-D__EMSCRIPTEN__"] if platform == "web" else []),
            str(test), "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)
    for name in ("ScriptMgnIntf.h", "base/CCDirector.h", "base/CCScheduler.h", "tjsObject.h",
                 "tjsCommHead.h", "SysInitIntf.h", "emscripten.h"):
        header = scratch / name
        header.parent.mkdir(parents=True, exist_ok=True)
        header.touch()
    coordinator = scratch / "coordinator"
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-I", str(scratch), "-I", str(ROOT),
        str(ROOT / "tests/web/host_native_exit.test.cpp"), "-o", str(coordinator),
    ], check=True)
    for mode in ("normal", "invalidated", "failed-flush", "nonzero"):
        subprocess.run([str(coordinator), mode], check=True)
print("Web exit defers native teardown; desktop exit remains unchanged")
