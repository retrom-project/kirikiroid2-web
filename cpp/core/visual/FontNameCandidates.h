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

} // namespace FontNameCandidates
