#!/usr/bin/env python3
"""Exercise the actual layered-window lookup and closing-window return path."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "cpp/plugins/LayeredWindowWeb.cpp").read_text()
lookup = "tjs_error LayeredWindow" + source.split(
    "tjs_error LayeredWindow", 1)[1].split("    auto *area", 1)[0]
harness = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
using tjs_int = int;
using tjs_int64 = long long;
using tjs_error = int;
constexpr int TJS_S_OK = 0, TJS_E_BADPARAMCOUNT = -1004, TJS_E_INVALIDPARAM = -1005;
struct iTJSDispatch2 {};
struct tTJSVariant {
    long long value;
    operator long long() const { return value; }
};
struct tTJSNI_Window {
    bool live = false;
    void *GetForm() { return live ? this : nullptr; }
};
tTJSNI_Window *registered = nullptr;
int TVPGetWindowCount() { return registered ? 1 : 0; }
tTJSNI_Window *TVPGetWindowListAt(int) { return registered; }
'''
harness += lookup + "    (void)invalid;\n    return 777;\n}\n"
harness += r'''
int main() {
    tTJSVariant values[] = {{1234}, {1}, {4}, {1}, {1}};
    tTJSVariant *args[] = {values, values+1, values+2, values+3, values+4};
    assert(LayeredWindow(nullptr, 4, args, nullptr) == TJS_E_BADPARAMCOUNT);
    assert(LayeredWindow(nullptr, 5, args, nullptr) == TJS_S_OK);
    tTJSNI_Window window;
    registered = &window;
    values[0].value = reinterpret_cast<std::uintptr_t>(&window);
    assert(LayeredWindow(nullptr, 5, args, nullptr) == TJS_S_OK);
    window.live = true;
    assert(LayeredWindow(nullptr, 5, args, nullptr) == 777);
    values[0].value = 1234;
    assert(LayeredWindow(nullptr, 5, args, nullptr) == TJS_S_OK);
}
'''
with tempfile.TemporaryDirectory(prefix="kirikiri-layered-window-") as directory:
    scratch = Path(directory)
    path, executable = scratch / "lookup.cpp", scratch / "lookup-test"
    path.write_text(harness)
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra",
        "-Wno-unused-parameter", "-Werror", str(path), "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)
print("Layered-window closing repaint preserves native success behavior")
