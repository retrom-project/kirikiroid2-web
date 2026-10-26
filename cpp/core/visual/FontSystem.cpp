

#include "tjsCommHead.h"

#include "FontSystem.h"
#include "FontNameCandidates.h"
#include "MsgIntf.h"
#include <vector>
#include "ConfigManager/IndividualConfigManager.h"

extern void TVPGetAllFontList(std::vector<ttstr> &list);
extern const ttstr &TVPGetDefaultFontName();
#ifdef __EMSCRIPTEN__
extern bool TVPHasRegisteredFont(const ttstr &name);
extern ttstr TVPFindRegisteredSongtiFont(const ttstr &requested);
extern ttstr TVPLoadWebCjkFallbackFont();
#endif

void FontSystem::InitFontNames() {
    // enumlate all fonts
    if(FontNamesInit)
        return;

    std::vector<ttstr> list;
    TVPGetAllFontList(list);
    size_t count = list.size();
    for(size_t i = 0; i < count; i++) {
        AddFont(list[i]);
    }

    FontNamesInit = true;
}
//---------------------------------------------------------------------------
void FontSystem::AddFont(const ttstr &name) { TVPFontNames.Add(name, 1); }
//---------------------------------------------------------------------------
bool FontSystem::FontExists(const ttstr &name) {
#ifdef __EMSCRIPTEN__
    // System.addFont may register a real family after this selector's first
    // lookup. The live registry must win over any Web compatibility fallback.
    return TVPHasRegisteredFont(name);
#else
    // check existence of font
    InitFontNames();

    int *t = TVPFontNames.Find(name);
    return t != nullptr;
#endif
}

FontSystem::FontSystem() : FontNamesInit(false), DefaultLOGFONTCreated(false) {
    ConstructDefaultFont();
}

void FontSystem::ConstructDefaultFont() {
    if(!DefaultLOGFONTCreated) {
        DefaultLOGFONTCreated = true;
        DefaultFont.Height = -12;
        DefaultFont.Flags = 0;
        DefaultFont.Angle = 0;
        DefaultFont.Face = ttstr(TVPGetDefaultFontName());
    }
}

ttstr FontSystem::GetBeingFont(ttstr fonts) {
    // retrieve being font in the system.
    // font candidates are given by "fonts", separated by comma.

    bool vfont;

    if(fonts.c_str()[0] == TJS_W('@')) { // for vertical writing
        fonts = fonts.c_str() + 1;
        vfont = true;
    } else {
        vfont = false;
    }

    static bool force_default_font =
        IndividualConfigManager::GetInstance()->GetValue<bool>(
            "force_default_font", false);
#ifdef __EMSCRIPTEN__
    bool missingSongti = false;
#endif
    if(!force_default_font) {
        bool prev_empty_name = false;
        while(fonts != TJS_W("")) {
            ttstr fontname;
            int pos = fonts.IndexOf(TJS_W(","));
            if(pos != -1) {
                const auto candidate = fonts.SubString(0, pos);
                const auto trimmed = FontNameCandidates::trim({candidate.c_str(), static_cast<std::size_t>(candidate.length())});
                fontname = ttstr(trimmed.data(), trimmed.size());
                fonts = fonts.SubString(pos + 1, -1);
            } else {
                const auto trimmed = FontNameCandidates::trim({fonts.c_str(), static_cast<std::size_t>(fonts.length())});
                fontname = ttstr(trimmed.data(), trimmed.size());
                fonts = TJS_W("");
            }

            // no existing check if previously specified font
            // candidate is empty eg. ",Fontname"

#ifdef __EMSCRIPTEN__
            if(FontNameCandidates::requestsSongti({fontname.c_str(), static_cast<std::size_t>(fontname.length())})) {
                const auto registered = TVPFindRegisteredSongtiFont(fontname);
                if(!registered.IsEmpty()) {
                    fontname = registered;
                } else {
                    missingSongti = true;
                    // A leading empty candidate deliberately forces the next
                    // family. Resolve a known unavailable Windows family here;
                    // unknown forced names keep their existing behavior.
                    if(prev_empty_name) {
                        const auto substitute = TVPLoadWebCjkFallbackFont();
                        if(!substitute.IsEmpty()) fontname = substitute;
                    }
                }
            }
#endif

            if(fontname != TJS_W("") &&
               (prev_empty_name || FontExists(fontname))) {
                if(vfont && fontname.c_str()[0] != TJS_W('@')) {
                    return TJS_W("@") + fontname;
                } else {
                    return fontname;
                }
            }

            prev_empty_name = (fontname == TJS_W(""));
        }
    }

#ifdef __EMSCRIPTEN__
    // Exhaust real candidates before loading an optional substitute. The
    // browser has no Windows font installation; keep the original default
    // for unknown names and explicit force-default settings.
    if(!force_default_font && missingSongti) {
        const auto substitute = TVPLoadWebCjkFallbackFont();
        if(!substitute.IsEmpty())
            return vfont ? ttstr(TJS_W("@")) + substitute : substitute;
    }
#endif

    if(vfont) {
        return ttstr(TJS_W("@")) + TVPGetDefaultFontName();
    } else {
        return TVPGetDefaultFontName();
    }
}
