#!/usr/bin/env python3
"""Replay the production Web publication boundary against a cached directory view."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "cpp/core/environ/web/Platform.cpp").read_text()
start = source.index("bool TVPWriteDataToFile(")
end = source.index("\nvoid TVPShowIME(", start)
body = source[start:end]
fixture = r"""
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
struct ttstr {
    std::string path;
    ttstr(const char *value) : path(value) {}
    std::string AsStdString() const { return path; }
};
namespace spdlog { template<class... T> void error(T...) {} }
std::map<std::string, std::vector<char>> files, directoryCache;
std::vector<std::string> order;
int writeLimit = 100000;
namespace VLFS {
bool Enabled() { return true; }
std::string current;
int Open(const char *path, int mode) {
    assert(mode == 1); current = path; files[current].clear(); order.push_back("open"); return 1;
}
int Write(int, const char *data, int size) {
    int length = std::min(size, writeLimit);
    files[current].insert(files[current].end(), data, data + length);
    writeLimit -= length;
    return length;
}
void Close(int) { order.push_back("close"); }
}
void TVPClearStorageCaches() {
    order.push_back("invalidate");
    directoryCache = files;
}
std::string placed(const std::string &base) {
    for (const auto &file : directoryCache) {
        auto slash = file.first.rfind('/');
        if(file.first.substr(slash + 1) == base) return file.first;
    }
    return {};
}
"""
checks = r"""
int main() {
    files["/profiles/玩家/slot-a.png"] = {'a'};
    directoryCache = files; // The engine has already enumerated the auto path.
    assert(placed("slot-b.png").empty());
    assert(TVPWriteDataToFile(ttstr("/profiles/玩家/slot-b.png"), "new", 3));
    assert(files["/profiles/玩家/slot-b.png"] == std::vector<char>({'n','e','w'}));
    assert(placed("slot-b.png") == "/profiles/玩家/slot-b.png" &&
           "published native save is missing from the cached auto-path directory");
    assert((order == std::vector<std::string>{"open","close","invalidate"}));
    order.clear(); writeLimit = 1;
    assert(!TVPWriteDataToFile(ttstr("/other/partial.ksd"), "data", 4));
    assert(files["/other/partial.ksd"] == std::vector<char>({'d'}));
    // A short write is still a failed save, but its actual namespace mutation
    // cannot leave the engine's directory view pretending the file is absent.
    assert(placed("partial.ksd") == "/other/partial.ksd");
    assert((order == std::vector<std::string>{"open","close","invalidate"}));
}
"""
with tempfile.TemporaryDirectory(prefix="krkr-web-write-") as directory:
    scratch = Path(directory)
    test = scratch / "write.cpp"
    test.write_text(fixture + body + checks)
    executable = scratch / "write"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    str(test), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print("Web publication closes the file, invalidates directory caches and preserves short-write failure")
