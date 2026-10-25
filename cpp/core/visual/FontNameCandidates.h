#pragma once

#include <string_view>

namespace FontNameCandidates {

// Font families are UTF-16 identifiers. Only boundary ASCII whitespace is
// removed; embedded spaces and every non-ASCII code unit belong to the name.
inline std::u16string_view trim(std::u16string_view name) {
    const auto whitespace = [](char16_t code) { return code <= u' ' || code == u'\x7f'; };
    while(!name.empty() && whitespace(name.front())) name.remove_prefix(1);
    while(!name.empty() && whitespace(name.back())) name.remove_suffix(1);
    return name;
}

// Windows exposes these two names for the same common Chinese family. This
// identifies a missing-family request; it does not register a substitute as
// the real family or change the order of available font candidates.
inline bool requestsSongti(std::u16string_view name) {
    if(name == u"宋体") return true;
    constexpr std::u16string_view english = u"simsun";
    if(name.size() != english.size()) return false;
    for(std::size_t i = 0; i < name.size(); ++i) {
        auto code = name[i];
        if(code >= u'A' && code <= u'Z') code += u'a' - u'A';
        if(code != english[i]) return false;
    }
    return true;
}

} // namespace FontNameCandidates
