#pragma once

#include <string>

// Browsers load the compiled-in implementation of a native plugin. A DLL/TPM
// storage path therefore identifies its module by basename, independently of
// the game's installation directory and the host path separator.
template <typename Character>
std::basic_string<Character> TVPWebPluginModuleName(
    const std::basic_string<Character> &path) {
    if(path.size() < 4) return path;
    auto extension = path.substr(path.size() - 4);
    for(auto &character : extension) {
        if(character >= Character('A') && character <= Character('Z'))
            character += Character('a' - 'A');
    }
    const std::basic_string<Character> dll{
        Character('.'), Character('d'), Character('l'), Character('l') };
    const std::basic_string<Character> tpm{
        Character('.'), Character('t'), Character('p'), Character('m') };
    if(extension != dll && extension != tpm) return path;
    const auto separator = path.find_last_of(std::basic_string<Character>{
        Character('/'), Character('\\') });
    auto module = path.substr(separator == path.npos ? 0 : separator + 1);
    if(extension == tpm) module.replace(module.size() - 4, 4, dll);
    return module;
}
