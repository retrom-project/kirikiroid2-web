#include "cpp/core/environ/web/WebPluginModuleName.h"

#include <cassert>
#include <string>

int main() {
    const auto module = [](const char16_t *name) {
        return TVPWebPluginModuleName(std::u16string(name));
    };
    assert(module(u"plugin/WindowEx.tpm") == u"WindowEx.dll");
    assert(module(u"file://./plugin/custom-extension.TPM") == u"custom-extension.dll");
    assert(module(u"C:\\架空目录\\modules\\Extension.DLL") == u"Extension.DLL");
    assert(module(u"addon.dll") == u"addon.dll");
    assert(module(u"addon.tpm") == u"addon.dll");
    assert(module(u"plugin/archive.xp3") == u"plugin/archive.xp3");
    assert(module(u"plugin/extension.tpm.backup") == u"plugin/extension.tpm.backup");
    assert(module(u"").empty());
}
