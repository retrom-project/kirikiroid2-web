#include "tjsCommHead.h"
#include "MDKParser.h"
#include "TextStream.h"
#include "tjsNative.h"

namespace {

class NativeParser : public tTJSNativeInstance {
    mdk::Parser parser;

public:
    tTJSVariant load(const ttstr &name) {
        // Use the normal storage text stream, including archive lookup and the
        // engine's encoding selection. No native DLL or host path is executed.
        auto *stream = TVPCreateTextStreamForRead(name, TJS_W(""));
        ttstr source;
        try {
            if(stream)
                stream->Read(source, 0);
        } catch(...) {
            delete stream;
            throw;
        }
        delete stream;
        return parser.parse(source);
    }
};

iTJSNativeInstance *createInstance() { return new NativeParser(); }

#undef TJS_NCM_REG_THIS
#undef TJS_NATIVE_SET_ClassID
#undef TJS_NATIVE_CLASSID_NAME
#define TJS_NCM_REG_THIS classobj
#define TJS_NATIVE_SET_ClassID TJS_NATIVE_CLASSID_NAME = TJS_NCM_CLASSID;
#define TJS_NATIVE_CLASSID_NAME MDKParserClassID
static tjs_int32 MDKParserClassID = -1;

} // namespace

iTJSDispatch2 *TVPCreateNativeClass_MDKParser() {
    auto *classobj = TJSCreateNativeClassForPlugin(TJS_W("MDKParser"),
                                                 createInstance);
    TJS_BEGIN_NATIVE_MEMBERS(MDKParser)
    TJS_DECL_EMPTY_FINALIZE_METHOD
    TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL(instance, NativeParser, MDKParser) {
        return TJS_S_OK;
    }
    TJS_END_NATIVE_CONSTRUCTOR_DECL(MDKParser)
    TJS_BEGIN_NATIVE_METHOD_DECL(loadScenario) {
        TJS_GET_NATIVE_INSTANCE(instance, NativeParser);
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        // The original plugin only reads/parses when a result was requested.
        if(result)
            *result = instance->load(ttstr(*param[0]));
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(loadScenario)
    TJS_END_NATIVE_MEMBERS
    return classobj;
}
