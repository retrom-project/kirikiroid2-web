#include <array>
#include <cassert>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <sys/stat.h>
#include "font_system_fixture.h"

using tjs_uint8 = unsigned char;
using tjs_uint64 = std::uint64_t;
constexpr int TJS_BS_READ = 0;
struct tTVPLocalFileInfo {
    const char *NativeName;
    int Mode;
    std::uint64_t Size;
};
struct FixtureFile {
    ttstr directory;
    const char *name;
    std::vector<unsigned char> bytes;
    int mode = S_IFREG;
    std::uint64_t advertisedSize = 0;
};
static std::vector<FixtureFile> files;
static std::vector<ttstr> registered;
static std::vector<ttstr> opened;
static std::vector<ttstr> listed;
static const ttstr root(TJS_W("file://./game/"));
ttstr TVPGetAppPath() { return root; }
void TVPGetLocalName(ttstr &directory) {
    if(directory == root) directory = TJS_W("/game/");
    else if(directory == root + TJS_W("fonts/")) directory = TJS_W("/game/fonts/");
    else throw std::runtime_error("unexpected discovery directory");
}
void TVPGetLocalFileListAt(const ttstr &directory,
        const std::function<void(const ttstr &, tTVPLocalFileInfo *)> &callback) {
    listed.push_back(directory);
    for(const auto &file : files) if(file.directory == directory) {
        tTVPLocalFileInfo info{file.name, file.mode, file.advertisedSize ? file.advertisedSize : file.bytes.size()};
        callback(file.name, &info);
    }
}
class tTJSBinaryStream {
    std::vector<unsigned char> bytes_;
public:
    explicit tTJSBinaryStream(std::vector<unsigned char> bytes) : bytes_(std::move(bytes)) {}
    unsigned Read(void *out, unsigned count) {
        const auto length = std::min<std::size_t>(count, bytes_.size());
        std::memcpy(out, bytes_.data(), length); return static_cast<unsigned>(length);
    }
};
tTJSBinaryStream *TVPCreateStream(const ttstr &path, int) {
    opened.push_back(path);
    for(const auto &file : files) {
        auto storage = root;
        if(file.directory == TJS_W("/game/fonts/")) storage = root + TJS_W("fonts/");
        if(storage + file.name == path) return new tTJSBinaryStream(file.bytes);
    }
    throw std::runtime_error("font stream path lost its directory prefix");
}
int TVPEnumFontsProc(const ttstr &path) { registered.push_back(path); return 1; }
void TVPAddLog(const ttstr &) { throw std::runtime_error("unexpected optional-font registration failure"); }

#include "font-discovery-production-includes.h"
static void discover() {
#include "font-discovery-production-body.h"
}
int main() {
    try {
        files = {
            {TJS_W("/game/"), "Loose.TTF", {0,1,0,0,0}},
            {TJS_W("/game/"), "bad.ttf", {0,0,0,0,0}},
            {TJS_W("/game/"), "data.xp3", {'X','P','3',0,0}},
            {TJS_W("/game/"), "font.ttf", {}, S_IFDIR},
            {TJS_W("/game/fonts/"), "Family.otf", {'O','T','T','O',0}},
        };
        discover();
        const std::vector<ttstr> expected{root + TJS_W("Loose.TTF"), root + TJS_W("fonts/Family.otf")};
        if(registered != expected) throw std::runtime_error("root/subdirectory fonts were not registered with full storage paths and validated headers");
        assert(opened.size() == 3);
        for(const auto &path : opened) assert(path != root + TJS_W("data.xp3"));
        assert(listed == (std::vector<ttstr>{ttstr(TJS_W("/game/")), ttstr(TJS_W("/game/fonts/"))}));
        registered.clear(); opened.clear(); listed.clear();
        files = {{TJS_W("/game/"), "huge.ttf", {0,1,0,0}, S_IFREG, 1024ull * 1024 * 1024}};
        discover();
        assert(opened.empty() && registered.empty());
        std::vector<std::string> names;
        names.reserve(80); files.clear();
        for(int index = 0; index < 80; ++index) {
            names.push_back("family" + std::to_string(index) + ".ttf");
            files.push_back({TJS_W("/game/"), names.back().c_str(), {0,1,0,0}});
        }
        discover();
        assert(opened.size() == 64 && registered.size() == 64);
    } catch(const std::exception &error) {
        std::cerr << "Actual font discovery failed: " << error.what() << '\n'; return 1;
    }
    std::cout << "Actual bounded root/font-directory discovery and SFNT header filtering passed\n";
}
