#include <iostream>
#include <stdexcept>
#include <algorithm>
#include "font_system_fixture.h"

static const ttstr defaultName(TJS_W("Droid Sans Fallback"));
static std::vector<ttstr> registered = {defaultName, ttstr(TJS_W("Source Han Sans JP Heavy")),
    ttstr(TJS_W("源ノ角ゴシック JP Heavy")), ttstr(TJS_W("方正准圆_GBK"))};
const ttstr &TVPGetDefaultFontName() { return defaultName; }
void TVPGetAllFontList(std::vector<ttstr> &list) {
    list = registered;
}
#ifdef __EMSCRIPTEN__
static int fallbackLoads = 0;
static bool fallbackAvailable = true;
bool TVPHasRegisteredFont(const ttstr &name) {
    return std::find(registered.begin(), registered.end(), name) != registered.end();
}
struct RegisteredFontFixture {
    struct Iterator {
        std::size_t at;
        bool IsNull() const { return at == registered.size(); }
        const ttstr &GetKey() const { return registered[at]; }
        Iterator &operator++() { ++at; return *this; }
        bool operator!=(const Iterator &other) const { return at != other.at; }
    };
    Iterator GetFirst() const { return {0}; }
    Iterator GetLast() const { return {registered.size() - 1}; }
};
static RegisteredFontFixture TVPFontNames;
// Run the real Web resolver against the registry boundary. The runner extracts
// this method from FontImpl.cpp; its alias/priority algorithm is not stubbed.
#include "cpp/core/visual/FontNameCandidates.h"
#include "production-font-real-family.h"
ttstr TVPLoadWebCjkFallbackFont() {
    ++fallbackLoads;
    if(!fallbackAvailable) return {};
    const ttstr family(TJS_W("Noto Sans SC"));
    if(!TVPHasRegisteredFont(family)) registered.push_back(family);
    return family;
}
#endif

#include "cpp/core/visual/FontSystem.cpp"

static void check(const ttstr &actual, const ttstr &expected, const char *message) {
    if(actual != expected) throw std::runtime_error(message);
}
int main(int argc, char **argv) {
    try {
#ifdef __EMSCRIPTEN__
        if(argc > 1 && std::string(argv[1]) == "force-default") {
            IndividualConfigManager::forceDefault = true;
            FontSystem fonts;
            check(fonts.GetBeingFont(TJS_W("宋体")), defaultName, "explicit default was overridden");
            if(fallbackLoads) throw std::runtime_error("forced default loaded an unused CJK font");
            return 0;
        }
        if(argc > 1 && std::string(argv[1]) == "unavailable-fallback") {
            fallbackAvailable = false;
            FontSystem fonts;
            check(fonts.GetBeingFont(TJS_W("宋体")), defaultName, "missing optional fallback lost default");
            return 0;
        }
#else
        (void)argc;
        (void)argv;
#endif
        FontSystem fonts;
        check(fonts.GetBeingFont(TJS_W(",源ノ角ゴシック JP Heavy,ＭＳ ゴシック")),
            TJS_W("源ノ角ゴシック JP Heavy"), "forced Unicode family lost prefix/suffix around interior spaces");
        check(fonts.GetBeingFont(TJS_W("  Source Han Sans JP Heavy \t")),
            TJS_W("Source Han Sans JP Heavy"), "ASCII family with interior spaces was changed");
        check(fonts.GetBeingFont(TJS_W(" \t方正准圆_GBK\r\n")),
            TJS_W("方正准圆_GBK"), "Unicode family boundary whitespace was not trimmed");
        check(fonts.GetBeingFont(TJS_W("方正准圆_GBK")), TJS_W("方正准圆_GBK"), "space-free Unicode family changed");
        check(fonts.GetBeingFont(TJS_W("@源ノ角ゴシック JP Heavy")),
            TJS_W("@源ノ角ゴシック JP Heavy"), "vertical family selection changed");
        check(fonts.GetBeingFont(TJS_W("missing,方正准圆_GBK")), TJS_W("方正准圆_GBK"), "candidate fallback order changed");
        check(fonts.GetBeingFont(TJS_W("missing")), defaultName, "default fallback changed");
        check(fonts.GetBeingFont(TJS_W(" \t\r\n")), defaultName, "empty candidate did not use default");
        check(fonts.GetBeingFont(TJS_W(",unregistered family")),
            TJS_W("unregistered family"), "explicit unverified-candidate contract changed");
#ifdef __EMSCRIPTEN__
        check(fonts.GetBeingFont(TJS_W("宋体,方正准圆_GBK")), TJS_W("方正准圆_GBK"),
            "substitute stole an explicitly available candidate");
        if(fallbackLoads) throw std::runtime_error("font fallback was loaded before it was needed");
        check(fonts.GetBeingFont(TJS_W("宋体")), TJS_W("Noto Sans SC"), "missing Songti was not substituted");
        check(fonts.GetBeingFont(TJS_W("sImSuN")), TJS_W("Noto Sans SC"), "ASCII Songti alias was not recognized");
        check(fonts.GetBeingFont(TJS_W("@宋体")), TJS_W("@Noto Sans SC"), "vertical family marker was lost");
        check(fonts.GetBeingFont(TJS_W(",宋体")), TJS_W("Noto Sans SC"), "forced known missing family was not substituted");
        check(fonts.GetBeingFont(TJS_W("NSimSun")), defaultName, "unrecognized family acquired an alias");
        registered.emplace_back(TJS_W("siMsUn"));
        check(fonts.GetBeingFont(TJS_W("simsun")), TJS_W("siMsUn"),
            "registered mixed-case real family lost priority to substitute");
        registered.emplace_back(TJS_W("SIMSUN"));
        check(fonts.GetBeingFont(TJS_W("sImSuN")), TJS_W("SIMSUN"),
            "noncanonical real families did not use deterministic name order");
        check(fonts.GetBeingFont(TJS_W("siMsUn")), TJS_W("siMsUn"),
            "exact real name lost priority to another known-family spelling");
        registered.emplace_back(TJS_W("SimSun"));
        check(fonts.GetBeingFont(TJS_W("sImSuN")), TJS_W("SimSun"),
            "canonical real family lost priority to noncanonical spelling");
        check(fonts.GetBeingFont(TJS_W("宋体")), TJS_W("SimSun"),
            "registered English family was ignored for its known Chinese alias");
        check(fonts.GetBeingFont(TJS_W("sImSuN,方正准圆_GBK")), TJS_W("SimSun"),
            "registered known-family candidate lost its original list position");
        registered.emplace_back(TJS_W("宋体"));
        check(fonts.GetBeingFont(TJS_W("宋体")), TJS_W("宋体"), "late registered real family lost priority");
        check(fonts.GetBeingFont(TJS_W(",宋体")), TJS_W("宋体"), "forced real family lost priority");
        check(fonts.GetBeingFont(TJS_W("SimSun")), TJS_W("SimSun"),
            "exact English name lost priority to canonical Chinese name");
        check(fonts.GetBeingFont(TJS_W("sImSuN")), TJS_W("宋体"),
            "known-family canonical preference was not Chinese then English");
        check(fonts.GetBeingFont(TJS_W("@sImSuN")), TJS_W("@宋体"),
            "registered known-family lookup lost the vertical marker");
        check(fonts.GetBeingFont(TJS_W(",sImSuN")), TJS_W("宋体"),
            "forced known-family request bypassed registered real family");
        registered.emplace_back(TJS_W("Unregistered family"));
        check(fonts.GetBeingFont(TJS_W("unregistered family")), defaultName,
            "known-family normalization leaked into an unknown family");
#endif
    } catch(const std::exception &error) {
        std::cerr << "FontSystem actual candidate selection failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "Actual FontSystem UTF-16 candidate selection passed\n";
}
