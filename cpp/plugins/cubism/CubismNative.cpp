#include "tjsCommHead.h"
#include "CubismWebBridge.h"
#include "StorageIntf.h"
#include "MsgIntf.h"
#include "tjsArray.h"
#include "tjsNative.h"
#include "WaveImpl.h"
#include <json/document.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
using Model = cubism::web::Model;
static tjs_int32 ModelClassID = -1;
static tjs_int32 AnimationClassID = -1;

template<typename Function> tjs_error Guard(Function &&function) {
    try { return function(); }
    catch(const std::exception &error) {
        TVPThrowExceptionMessage(ttstr(error.what()).c_str());
    }
    return TJS_E_FAIL;
}

std::vector<char> ReadStorage(const ttstr &name) {
    std::unique_ptr<tTJSBinaryStream> stream(TVPCreateStream(name, TJS_BS_READ));
    if(!stream) throw std::runtime_error("Cannot open Cubism resource");
    const auto size = stream->GetSize();
    if(size > std::numeric_limits<tjs_uint>::max())
        throw std::runtime_error("Cubism resource is too large");
    std::vector<char> bytes(static_cast<std::size_t>(size));
    if(size && stream->Read(bytes.data(), static_cast<tjs_uint>(size)) != size)
        throw std::runtime_error("Cannot read Cubism resource");
    return bytes;
}

rapidjson::Document ReadSettings(const std::vector<char> &bytes) {
    if(bytes.empty()) throw std::runtime_error("Empty Cubism model3 JSON");
    rapidjson::Document document;
    std::size_t start = 0;
    if(bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xef &&
       static_cast<unsigned char>(bytes[1]) == 0xbb && static_cast<unsigned char>(bytes[2]) == 0xbf)
        start = 3;
    document.Parse(bytes.data() + start, bytes.size() - start);
    if(document.HasParseError() || !document.IsObject())
        throw std::runtime_error("Invalid Cubism model3 JSON");
    return document;
}

tTJSVariant StringArray(const std::vector<std::string> &strings) {
    auto *array = TJSCreateArrayObject();
    tTJSVariant result(array, array);
    array->Release();
    for(std::size_t i = 0; i < strings.size(); ++i) {
        const tTJSVariant value(ttstr(strings[i].c_str()));
        array->PropSetByNum(TJS_MEMBERENSURE, static_cast<tjs_int>(i), &value, array);
    }
    return result;
}

class NativeAnimation : public tTJSNativeInstance {
public:
    std::unique_ptr<cubism::web::Animation> animation;
    void Load(const ttstr &name) {
        animation.reset();
        const auto bytes = ReadStorage(name);
        animation = std::make_unique<cubism::web::Animation>(bytes.data(), bytes.size());
    }
    void Invalidate() override { animation.reset(); }
};

class NativeModel : public tTJSNativeInstance {
public:
    std::unique_ptr<Model> model;
    std::array<double, Model::PropertyCount> properties{{-1, -1, 0, 0, 0, 0, 0, 0, 1, 1, 1}};
    tTJSVariant textureFiles{static_cast<iTJSDispatch2 *>(nullptr)};
    tTJSVariant parameterNames{static_cast<iTJSDispatch2 *>(nullptr)};
    tTJSVariant voice{static_cast<iTJSDispatch2 *>(nullptr)};
    std::unordered_map<std::string, std::size_t> parameters;
    int width = 1, height = 1;

    void Close() {
        model.reset();
        textureFiles = tTJSVariant(static_cast<iTJSDispatch2 *>(nullptr));
        parameterNames = tTJSVariant(static_cast<iTJSDispatch2 *>(nullptr));
        parameters.clear();
        properties[Model::PositionX] = properties[Model::PositionY] = 0;
        properties[Model::ScaleX] = properties[Model::ScaleY] = 1;
        properties[Model::EnablePhysics] = 1;
        width = height = 1;
    }

    void Invalidate() override {
        Close();
        voice = tTJSVariant(static_cast<iTJSDispatch2 *>(nullptr));
    }

    void Open(const ttstr &name) {
        Close();
        const auto bytes = ReadStorage(name);
        const auto settings = ReadSettings(bytes);
        if(!settings.HasMember("Version") || !settings["Version"].IsInt() || settings["Version"].GetInt() != 3 ||
           !settings.HasMember("FileReferences") || !settings["FileReferences"].IsObject())
            throw std::runtime_error("Unsupported Cubism model settings");
        const auto &references = settings["FileReferences"];
        if(!references.HasMember("Moc") || !references["Moc"].IsString())
            throw std::runtime_error("Cubism model settings have no MOC3 reference");
        const ttstr directory = TVPExtractStoragePath(name);
        const auto moc = ReadStorage(directory + ttstr(references["Moc"].GetString()));
        auto next = std::make_unique<Model>(moc.data(), moc.size());
        next->LoadDocument(Model::DocumentKind::Settings, bytes.data(), bytes.size());
        std::vector<std::string> textures;
        if(references.HasMember("Textures")) {
            if(!references["Textures"].IsArray()) throw std::runtime_error("Invalid Cubism texture list");
            for(const auto &texture : references["Textures"].GetArray()) {
                if(!texture.IsString()) throw std::runtime_error("Invalid Cubism texture reference");
                textures.emplace_back(texture.GetString(), texture.GetStringLength());
            }
        }
        const auto loadDocument = [&](const char *key, Model::DocumentKind kind) {
            if(!references.HasMember(key)) return;
            const auto &reference = references[key];
            if(!reference.IsString()) throw std::runtime_error("Invalid Cubism auxiliary resource reference");
            if(reference.GetStringLength() == 0) return;
            const auto resource = ReadStorage(directory + ttstr(reference.GetString()));
            next->LoadDocument(kind, resource.data(), resource.size());
        };
        loadDocument("Physics", Model::DocumentKind::Physics);
        // ExternalParamter is the original plugin's published spelling.
        loadDocument("ExternalParamter", Model::DocumentKind::ExternalParameters);
        loadDocument("ExternalPhysics", Model::DocumentKind::ExternalPhysics);
        std::vector<std::string> names;
        for(std::size_t i = 0; i < next->ParameterCount(); ++i) {
            names.push_back(next->ParameterName(i));
            parameters[names.back()] = i;
        }
        textureFiles = StringArray(textures);
        parameterNames = StringArray(names);
        model = std::move(next);
    }

    void LoadExternalPhysics(const ttstr &parametersName, const ttstr &physicsName) {
        if(!model) throw std::runtime_error("Cubism model is closed");
        const auto parameterBytes = ReadStorage(parametersName);
        model->LoadDocument(Model::DocumentKind::ExternalParameters, parameterBytes.data(), parameterBytes.size());
        const auto bytes = ReadStorage(physicsName);
        model->LoadDocument(Model::DocumentKind::ExternalPhysics, bytes.data(), bytes.size());
    }

    bool Range(const std::string &name, Model::ParameterField field, double &value) const {
        const auto found = parameters.find(name);
        if(!model || found == parameters.end()) return false;
        value = model->GetParameter(found->second, field);
        return true;
    }

    int VoicePeak(double delta) {
        if(voice.Type() != tvtObject || !voice.AsObjectNoAddRef()) return -1;
        auto *object = voice.AsObjectThisNoAddRef();
        tTJSNI_WaveSoundBuffer *buffer = nullptr;
        if(!object || TJS_FAILED(object->NativeInstanceSupport(TJS_NIS_GETINSTANCE, tTJSNC_WaveSoundBuffer::ClassID,
                        reinterpret_cast<iTJSNativeInstance **>(&buffer))) || !buffer)
            return -1;
        const int frequency = buffer->GetFrequency();
        const double wanted = frequency * delta;
        if(wanted <= 0 || wanted > std::numeric_limits<tjs_int>::max()) return -1;
        const int count = static_cast<int>(wanted);
        if(!count) return -1;
        if(!buffer->GetUseVisBuffer()) buffer->SetUseVisBuffer(true);
        std::vector<tjs_int16> samples(count, 0);
        buffer->GetVisBuffer(samples.data(), count, 1, 0);
        int peak = 0;
        for(const auto sample : samples) {
            // abs16(-32768) remains negative in the original loop.
            const int magnitude = static_cast<tjs_int16>(std::abs(static_cast<int>(sample)));
            peak = std::max(peak, magnitude);
        }
        return peak;
    }

    void Update(double delta) {
        if(model) model->Update(delta, properties.data(), VoicePeak(delta));
    }

    void Draw(tTJSVariant &textures, int targetWidth, int targetHeight) {
        if(!model) return;
        std::vector<std::uint32_t> handles;
        if(textures.Type() == tvtInteger) handles.push_back(static_cast<tjs_int64>(textures));
        else if(textures.Type() == tvtObject && textures.AsObjectNoAddRef()) {
            auto array = textures.AsObjectClosureNoAddRef();
            tTJSVariant count;
            if(TJS_FAILED(array.PropGet(0, TJS_W("count"), nullptr, &count, nullptr)))
                throw std::runtime_error("Invalid Cubism texture handle list");
            const auto length = static_cast<tjs_int>(count);
            if(length < 0) throw std::runtime_error("Invalid Cubism texture handle count");
            handles.reserve(length);
            for(tjs_int i = 0; i < length; ++i) {
                tTJSVariant value;
                if(TJS_SUCCEEDED(array.PropGetByNum(0, i, &value, nullptr)))
                    handles.push_back(static_cast<tjs_int64>(value));
                else handles.push_back(0);
            }
        } else throw std::runtime_error("Cubism draw requires texture handles");
        model->Draw(handles.data(), handles.size(), targetWidth, targetHeight,
                    properties[Model::PositionX], properties[Model::PositionY],
                    properties[Model::ScaleX], properties[Model::ScaleY]);
        width = targetWidth;
        height = targetHeight;
    }
};

template<typename Instance> Instance *GetInstance(iTJSDispatch2 *object, tjs_int32 classID) {
    Instance *instance = nullptr;
    if(!object || TJS_FAILED(object->NativeInstanceSupport(TJS_NIS_GETINSTANCE, classID,
                                    reinterpret_cast<iTJSNativeInstance **>(&instance)))) return nullptr;
    return instance;
}

template<int Minimum, typename Callback>
tjs_error ModelCall(tjs_int count, iTJSDispatch2 *object, Callback &&callback) {
    auto *instance = GetInstance<NativeModel>(object, ModelClassID);
    if(!instance) return TJS_E_NATIVECLASSCRASH;
    if(count < Minimum) return TJS_E_BADPARAMCOUNT;
    return Guard([&] { return callback(*instance); });
}

template<int Property> tjs_error GetProperty(tTJSVariant *result, iTJSDispatch2 *object) {
    return ModelCall<0>(0, object, [&](NativeModel &instance) {
        if(result) *result = instance.properties[Property];
        return TJS_S_OK;
    });
}

template<int Property> tjs_error SetProperty(const tTJSVariant *value, iTJSDispatch2 *object) {
    return ModelCall<0>(0, object, [&](NativeModel &instance) {
        double number = static_cast<float>(static_cast<tjs_real>(*value));
        if(Property == Model::EyeBlinkRate) number = std::min(1.0, std::max(0.0, number));
        if(Property == Model::EnablePhysics) number = static_cast<tjs_int>(*value) != 0;
        instance.properties[Property] = number;
        return TJS_S_OK;
    });
}

tjs_error ReadOnly(const tTJSVariant *, iTJSDispatch2 *) { return TJS_E_ACCESSDENYED; }

template<Model::ParameterField Field>
tjs_error Range(tTJSVariant *result, tjs_int count, tTJSVariant **arguments, iTJSDispatch2 *object) {
    return ModelCall<1>(count, object, [&](NativeModel &instance) {
        double value;
        if(!instance.Range(ttstr(*arguments[0]).AsStdString(), Field, value)) return TJS_E_INVALIDPARAM;
        if(result) *result = value;
        return TJS_S_OK;
    });
}

void Method(tTJSNativeClass *object, const tjs_char *name, tTJSNativeClassMethodCallback callback, bool isStatic = false) {
    object->RegisterNCM(name, TJSCreateNativeClassMethod(callback), object->GetClassName().c_str(), nitMethod,
                        isStatic ? TJS_STATICMEMBER : 0);
}

void Property(tTJSNativeClass *object, const tjs_char *name, tTJSNativeClassPropertyGetCallback get,
                tTJSNativeClassPropertySetCallback set = ReadOnly) {
    object->RegisterNCM(name, TJSCreateNativeClassProperty(get, set), object->GetClassName().c_str(), nitProperty);
}

template<int Index> void NumericProperty(tTJSNativeClass *object, const tjs_char *name) {
    Property(object, name, GetProperty<Index>, SetProperty<Index>);
}

} // namespace

iTJSDispatch2 *TVPCreateNativeClass_Live2DCubism() {
    auto *object = TJSCreateNativeClassForPlugin(TJS_W("Live2DCubism"), []() -> iTJSNativeInstance * { return new NativeModel(); });
    ModelClassID = TJSRegisterNativeClass(TJS_W("Live2DCubism"));
    object->SetClassID(ModelClassID);
    object->RegisterNCM(TJS_W("Live2DCubism"), TJSCreateNativeClassConstructor(
        [](tTJSVariant *, tjs_int count, tTJSVariant **arguments, iTJSDispatch2 *object) -> tjs_error {
            return ModelCall<0>(count, object, [&](NativeModel &instance) {
                if(count && arguments[0]->Type() == tvtString) instance.Open(ttstr(*arguments[0]));
                tTJSVariant missing(TJS_W("missing"));
                return object->ClassInstanceInfo(TJS_CII_SET_MISSING, 0, &missing);
            });
        }), TJS_W("Live2DCubism"), nitMethod);
    Method(object, TJS_W("finalize"), [](tTJSVariant *, tjs_int, tTJSVariant **, iTJSDispatch2 *) { return TJS_S_OK; });
    Method(object, TJS_W("open"), [](tTJSVariant *, tjs_int count, tTJSVariant **arguments, iTJSDispatch2 *object) {
        return ModelCall<1>(count, object, [&](NativeModel &instance) { instance.Open(ttstr(*arguments[0])); return TJS_S_OK; });
    });
    Method(object, TJS_W("loadExternalPhysics"), [](tTJSVariant *, tjs_int count, tTJSVariant **arguments, iTJSDispatch2 *object) {
        return ModelCall<2>(count, object, [&](NativeModel &instance) {
            instance.LoadExternalPhysics(ttstr(*arguments[0]), ttstr(*arguments[1])); return TJS_S_OK;
        });
    });
    Method(object, TJS_W("update"), [](tTJSVariant *, tjs_int count, tTJSVariant **arguments, iTJSDispatch2 *object) {
        return ModelCall<1>(count, object, [&](NativeModel &instance) { instance.Update(static_cast<tjs_real>(*arguments[0])); return TJS_S_OK; });
    });
    Method(object, TJS_W("applyAnimation"), [](tTJSVariant *, tjs_int count, tTJSVariant **arguments, iTJSDispatch2 *object) {
        return ModelCall<1>(count, object, [&](NativeModel &instance) {
            auto *animation = GetInstance<NativeAnimation>(arguments[0]->AsObjectThisNoAddRef(), AnimationClassID);
            if(instance.model && animation && animation->animation)
                instance.model->ApplyAnimation(*animation->animation, count > 1 ? static_cast<tjs_real>(*arguments[1]) : 0,
                                                count > 2 ? static_cast<tjs_real>(*arguments[2]) : 1);
            return TJS_S_OK;
        });
    });
    Method(object, TJS_W("draw"), [](tTJSVariant *, tjs_int count, tTJSVariant **arguments, iTJSDispatch2 *object) {
        return ModelCall<3>(count, object, [&](NativeModel &instance) {
            instance.Draw(*arguments[0], static_cast<tjs_int>(*arguments[1]), static_cast<tjs_int>(*arguments[2]));
            return TJS_S_OK;
        });
    });
    Method(object, TJS_W("close"), [](tTJSVariant *, tjs_int count, tTJSVariant **, iTJSDispatch2 *object) {
        return ModelCall<0>(count, object, [&](NativeModel &instance) { instance.Close(); return TJS_S_OK; });
    });
    Method(object, TJS_W("getMaxValue"), Range<Model::ParameterField::Maximum>);
    Method(object, TJS_W("getMinValue"), Range<Model::ParameterField::Minimum>);
    Method(object, TJS_W("getDefaultValue"), Range<Model::ParameterField::Default>);
    Method(object, TJS_W("setParameterRate"), [](tTJSVariant *, tjs_int count, tTJSVariant **arguments, iTJSDispatch2 *object) {
        return ModelCall<2>(count, object, [&](NativeModel &instance) {
            if(!instance.model || !instance.model->SetParameterRate(ttstr(*arguments[0]).AsStdString(), static_cast<tjs_real>(*arguments[1])))
                return TJS_E_INVALIDPARAM;
            return TJS_S_OK;
        });
    });
    Method(object, TJS_W("resetParameterToDefault"), [](tTJSVariant *, tjs_int count, tTJSVariant **, iTJSDispatch2 *object) {
        return ModelCall<0>(count, object, [&](NativeModel &instance) { if(instance.model) instance.model->ResetParameters(false); return TJS_S_OK; });
    });
    Method(object, TJS_W("resetSystemControlParameter"), [](tTJSVariant *, tjs_int count, tTJSVariant **, iTJSDispatch2 *object) {
        return ModelCall<0>(count, object, [&](NativeModel &instance) { if(instance.model) instance.model->ResetParameters(true); return TJS_S_OK; });
    });
    Method(object, TJS_W("hitTest"), [](tTJSVariant *result, tjs_int count, tTJSVariant **arguments, iTJSDispatch2 *object) {
        return ModelCall<3>(count, object, [&](NativeModel &instance) {
            if(result) *result = instance.model && instance.model->HitTest(ttstr(*arguments[0]).AsStdString(),
                static_cast<tjs_int>(*arguments[1]), static_cast<tjs_int>(*arguments[2]), instance.width, instance.height,
                instance.properties[Model::PositionX], instance.properties[Model::PositionY],
                instance.properties[Model::ScaleX], instance.properties[Model::ScaleY]);
            return TJS_S_OK;
        });
    });
    Method(object, TJS_W("setExternalPhysicsEnabled"), [](tTJSVariant *result, tjs_int count, tTJSVariant **arguments, iTJSDispatch2 *object) {
        return ModelCall<2>(count, object, [&](NativeModel &instance) {
            if(result && instance.model) instance.model->SetExternalPhysicsEnabled(static_cast<tjs_int>(*arguments[0]), static_cast<tjs_int>(*arguments[1]) != 0);
            return TJS_S_OK;
        });
    });
    Method(object, TJS_W("missing"), [](tTJSVariant *result, tjs_int count, tTJSVariant **arguments, iTJSDispatch2 *object) {
        return ModelCall<3>(count, object, [&](NativeModel &instance) -> tjs_error {
            if(!instance.model) return TJS_E_MEMBERNOTFOUND;
            auto reference = arguments[2]->AsObjectClosureNoAddRef();
            if(!reference.Object) return TJS_E_INVALIDPARAM;
            const std::string name = ttstr(*arguments[1]).AsStdString();
            bool success;
            if(static_cast<tjs_int>(*arguments[0])) {
                tTJSVariant value;
                const auto status = reference.PropGet(0, nullptr, nullptr, &value, nullptr);
                if(TJS_FAILED(status)) return status;
                success = instance.model->SetNamedParameter(name, static_cast<tjs_real>(value));
            } else {
                double value;
                success = instance.model->GetNamedParameter(name, value);
                if(success) {
                    const tTJSVariant variant(value);
                    const auto status = reference.PropSet(0, nullptr, nullptr, &variant, nullptr);
                    if(TJS_FAILED(status)) return status;
                }
            }
            if(result) *result = success;
            return success ? TJS_S_OK : TJS_E_MEMBERNOTFOUND;
        });
    });
    Method(object, TJS_W("initialize"), [](tTJSVariant *, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
        return Guard([] { if(!cubism::web::IsCoreAvailable()) throw std::runtime_error("Live2D Cubism Core for Web is not installed"); return TJS_S_OK; });
    }, true);
    // The native uninitializer deletes its shared mask/program resources.
    // The Web backend releases corresponding per-model GPU resources and can
    // lazily recreate them on a later draw while keeping Core models alive.
    Method(object, TJS_W("uninitialize"), [](tTJSVariant *, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
        return Guard([] { cubism::web::Uninitialize(); return TJS_S_OK; });
    }, true);
    Property(object, TJS_W("textureFiles"), [](tTJSVariant *result, iTJSDispatch2 *object) {
        return ModelCall<0>(0, object, [&](NativeModel &instance) { if(result) *result = instance.textureFiles; return TJS_S_OK; });
    });
    Property(object, TJS_W("parameterNames"), [](tTJSVariant *result, iTJSDispatch2 *object) {
        return ModelCall<0>(0, object, [&](NativeModel &instance) { if(result) *result = instance.parameterNames; return TJS_S_OK; });
    });
    Property(object, TJS_W("voiceSoundBuffer"), [](tTJSVariant *result, iTJSDispatch2 *object) {
        return ModelCall<0>(0, object, [&](NativeModel &instance) { if(result) *result = instance.voice; return TJS_S_OK; });
    }, [](const tTJSVariant *value, iTJSDispatch2 *object) {
        return ModelCall<0>(0, object, [&](NativeModel &instance) {
            instance.voice = value->Type() == tvtObject ? *value : tTJSVariant(static_cast<iTJSDispatch2 *>(nullptr));
            return TJS_S_OK;
        });
    });
    NumericProperty<Model::EyeBlinkRate>(object, TJS_W("eyeBlinkRate"));
    NumericProperty<Model::LipSyncRate>(object, TJS_W("lipSyncRate"));
    NumericProperty<Model::GravityX>(object, TJS_W("gravityX"));
    NumericProperty<Model::GravityY>(object, TJS_W("gravityY"));
    NumericProperty<Model::WindX>(object, TJS_W("windX"));
    NumericProperty<Model::WindY>(object, TJS_W("windY"));
    NumericProperty<Model::PositionX>(object, TJS_W("positionX"));
    NumericProperty<Model::PositionY>(object, TJS_W("positionY"));
    NumericProperty<Model::ScaleX>(object, TJS_W("scaleX"));
    NumericProperty<Model::ScaleY>(object, TJS_W("scaleY"));
    NumericProperty<Model::EnablePhysics>(object, TJS_W("enablePhysics"));
    return object;
}

iTJSDispatch2 *TVPCreateNativeClass_Live2DAnimation() {
    auto *object = TJSCreateNativeClassForPlugin(TJS_W("Live2DAnimation"), []() -> iTJSNativeInstance * { return new NativeAnimation(); });
    AnimationClassID = TJSRegisterNativeClass(TJS_W("Live2DAnimation"));
    object->SetClassID(AnimationClassID);
    object->RegisterNCM(TJS_W("Live2DAnimation"), TJSCreateNativeClassConstructor(
        [](tTJSVariant *, tjs_int count, tTJSVariant **arguments, iTJSDispatch2 *object) {
            return Guard([&] {
                auto *instance = GetInstance<NativeAnimation>(object, AnimationClassID);
                if(!instance) return TJS_E_NATIVECLASSCRASH;
                if(count && arguments[0]->Type() == tvtString) instance->Load(ttstr(*arguments[0]));
                return TJS_S_OK;
            });
        }), TJS_W("Live2DAnimation"), nitMethod);
    Method(object, TJS_W("finalize"), [](tTJSVariant *, tjs_int, tTJSVariant **, iTJSDispatch2 *) { return TJS_S_OK; });
    Method(object, TJS_W("load"), [](tTJSVariant *, tjs_int count, tTJSVariant **arguments, iTJSDispatch2 *object) {
        return Guard([&] {
            auto *instance = GetInstance<NativeAnimation>(object, AnimationClassID);
            if(!instance) return TJS_E_NATIVECLASSCRASH;
            if(count < 1) return TJS_E_BADPARAMCOUNT;
            instance->Load(ttstr(*arguments[0]));
            return TJS_S_OK;
        });
    });
    Method(object, TJS_W("close"), [](tTJSVariant *, tjs_int, tTJSVariant **, iTJSDispatch2 *object) {
        auto *instance = GetInstance<NativeAnimation>(object, AnimationClassID);
        if(!instance) return TJS_E_NATIVECLASSCRASH;
        instance->Invalidate();
        return TJS_S_OK;
    });
    return object;
}
