#define NCB_MODULE_NAME TJS_W("MDKParser.dll")
#include "ncbind.hpp"

iTJSDispatch2 *TVPCreateNativeClass_MDKParser();

namespace {

void registerParser() {
    auto *global = TVPGetScriptDispatch();
    if(!global)
        return;
    auto *parser = TVPCreateNativeClass_MDKParser();
    tTJSVariant value(parser, parser);
    parser->Release();
    global->PropSet(TJS_MEMBERENSURE, TJS_W("MDKParser"), nullptr, &value,
                    global);
    global->Release();
}

void unregisterParser() {
    auto *global = TVPGetScriptDispatch();
    if(!global)
        return;
    global->DeleteMember(0, TJS_W("MDKParser"), nullptr, global);
    global->Release();
}

} // namespace

NCB_POST_REGIST_CALLBACK(registerParser);
NCB_PRE_UNREGIST_CALLBACK(unregisterParser);
