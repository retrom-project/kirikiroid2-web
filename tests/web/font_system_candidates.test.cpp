#include <iostream>
#include <stdexcept>
#include "font_system_fixture.h"

static const ttstr defaultName(TJS_W("Droid Sans Fallback"));
const ttstr &TVPGetDefaultFontName() { return defaultName; }
void TVPGetAllFontList(std::vector<ttstr> &list) {
    list = {defaultName, ttstr(TJS_W("Source Han Sans JP Heavy")),
        ttstr(TJS_W("源ノ角ゴシック JP Heavy")), ttstr(TJS_W("方正准圆_GBK"))};
}

#include "cpp/core/visual/FontSystem.cpp"

static void check(const ttstr &actual, const ttstr &expected, const char *message) {
    if(actual != expected) throw std::runtime_error(message);
}
int main() {
    try {
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
    } catch(const std::exception &error) {
        std::cerr << "FontSystem actual candidate selection failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "Actual FontSystem UTF-16 candidate selection passed\n";
}
