#define NCB_MODULE_NAME TJS_W("krlive2dcubism.dll")
#include "ncbind.hpp"

iTJSDispatch2 *TVPCreateNativeClass_Live2DCubism();
iTJSDispatch2 *TVPCreateNativeClass_Live2DAnimation();

namespace {
void Register() {
    auto *global = TVPGetScriptDispatch();
    if(!global) return;
    auto *model = TVPCreateNativeClass_Live2DCubism();
    const tTJSVariant modelValue(model, model);
    model->Release();
    global->PropSet(TJS_MEMBERENSURE, TJS_W("Live2DCubism"), nullptr, &modelValue, global);
    auto *animation = TVPCreateNativeClass_Live2DAnimation();
    const tTJSVariant animationValue(animation, animation);
    animation->Release();
    global->PropSet(TJS_MEMBERENSURE, TJS_W("Live2DAnimation"), nullptr, &animationValue, global);
    global->Release();
}

void Unregister() {
    auto *global = TVPGetScriptDispatch();
    if(!global) return;
    global->DeleteMember(0, TJS_W("Live2DCubism"), nullptr, global);
    global->DeleteMember(0, TJS_W("Live2DAnimation"), nullptr, global);
    global->Release();
}
} // namespace

NCB_POST_REGIST_CALLBACK(Register);
NCB_PRE_UNREGIST_CALLBACK(Unregister);
