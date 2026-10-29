#define NCB_MODULE_NAME TJS_W("ExtKAGParser.dll")
#include "ncbind.hpp"
#include "KAGParser.h"
#include "ScriptMgnIntf.h"

static void RegisterExtKAGParser() {
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(!global)
        return;

    iTJSDispatch2 *classobj = TVPCreateNativeClass_ExtKAGParser();
    tTJSVariant value(classobj);
    classobj->Release();
    global->PropSet(TJS_MEMBERENSURE, TJS_W("KAGParser"), nullptr, &value,
                    global);
    global->Release();
}

NCB_POST_REGIST_CALLBACK(RegisterExtKAGParser);
