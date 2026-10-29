#include "tjsCommHead.h"
#include "HostResourceOverlay.h"
#include "StorageIntf.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <emscripten.h>

// Explicit Web boundary: native DLL patch loaders cannot run in this VM.
// The Host supplies only paths from its validated, immutable project index.
EM_JS(int, krkr2_web_overlay_file_count, (), {
    return Array.isArray(Module['_resourceOverlayFiles']) ? Module['_resourceOverlayFiles'].length : 0;
});
EM_JS(char *, krkr2_web_overlay_file_field, (int index, int relative), {
    var file = Module['_resourceOverlayFiles'][index];
    var value = relative ? file.relativePath : file.path;
    var size = lengthBytesUTF8(value) + 1;
    var result = _malloc(size);
    stringToUTF8(value, result, size);
    return result;
});

extern "C" EMSCRIPTEN_KEEPALIVE int krkr2_host_resource_overlay_abi() { return 1; }

namespace {
std::string resourceKey(ttstr name) {
    tTVPArchive::NormalizeInArchiveStorageName(name);
    return name.AsStdString();
}
std::string basename(const std::string &name) {
    return name.substr(name.find_last_of('/') + 1);
}
struct ReleaseArchive {
    void operator()(tTVPArchive *archive) const { if(archive) archive->Release(); }
};
struct ResourceOverlay {
    bool built = false;
    bool building = false;
    std::map<std::string, std::string> exact;
    std::map<std::string, std::string> unique_names;

    void build() {
        const int count = krkr2_web_overlay_file_count();
        if(count < 0 || count > 10000) throw std::runtime_error("Resource overlay file limit");
        std::vector<std::pair<std::string, std::string>> files;
        for(int i = 0; i < count; ++i) {
            std::unique_ptr<char, decltype(&std::free)> path(krkr2_web_overlay_file_field(i, 0), &std::free);
            std::unique_ptr<char, decltype(&std::free)> relative(krkr2_web_overlay_file_field(i, 1), &std::free);
            files.emplace_back(relative.get(), path.get());
        }
        std::sort(files.begin(), files.end());
        std::map<std::string, std::string> entries;
        for(const auto &[relative, path] : files) {
            const auto key = resourceKey(ttstr(relative));
            if(key.size() < 4 || key.substr(key.size() - 4) != ".xp3") continue;
            const ttstr normalized = TVPNormalizeStorageName(ttstr(path));
            std::unique_ptr<tTVPArchive, ReleaseArchive> archive(TVPOpenArchive(normalized, true));
            if(!archive) throw std::runtime_error("Resource overlay archive unavailable");
            for(tjs_uint i = 0; i < archive->GetCount(); ++i) {
                if(entries.size() >= 100000) throw std::runtime_error("Resource overlay entry limit");
                const auto member = archive->GetName(i);
                entries[resourceKey(member)] = (normalized + TJS_W(">") + member).AsStdString();
            }
        }
        // Loose files override archive members; whole base archives stay intact.
        for(const auto &[relative, path] : files) {
            const auto key = resourceKey(ttstr(relative));
            if(key.size() >= 4 && key.substr(key.size() - 4) == ".xp3") continue;
            entries[key] = TVPNormalizeStorageName(ttstr(path)).AsStdString();
        }
        std::map<std::string, std::string> names;
        for(const auto &[key, path] : entries) {
            const auto name = basename(key);
            const auto [it, inserted] = names.emplace(name, path);
            if(!inserted) it->second.clear(); // Ambiguous basenames cannot select a resource.
        }
        exact.swap(entries);
        unique_names.swap(names);
        built = true;
    }
};
}

ttstr TVPWebFindResourceOverlay(const ttstr &name) {
    static ResourceOverlay overlay;
    if(overlay.building) return {};
    if(!overlay.built) {
        overlay.building = true;
        try { overlay.build(); }
        catch(...) { overlay.building = false; throw; }
        overlay.building = false;
    }
    if(overlay.exact.empty()) return {};
    auto relative = TVPNormalizeStorageName(name).AsStdString();
    const auto delimiter = relative.find('>');
    if(delimiter != std::string::npos) relative.erase(0, delimiter + 1);
    else {
        const auto root = TVPGetAppPath().AsStdString();
        if(relative.compare(0, root.size(), root) != 0) return {};
        relative.erase(0, root.size());
    }
    const auto key = resourceKey(ttstr(relative));
    auto exact = overlay.exact.find(key);
    if(exact != overlay.exact.end()) return ttstr(exact->second);
    auto unique = overlay.unique_names.find(basename(key));
    return unique != overlay.unique_names.end() ? ttstr(unique->second) : ttstr();
}
