#include "WebCanvas.h"
#include "WebCanvasInternal.h"
#include "BitmapIntf.h"
#include "GraphicsLoaderIntf.h"
#include "LayerBitmapIntf.h"
#include "RectItf.h"
#include "tjsObject.h"
#include <algorithm>
#include <cmath>

namespace webcanvas {

namespace {
class Method : public tTJSNativeClassMethod {
    tjs_int32 id;
    std::string name;
    bool constructor;
public:
    Method(tjs_int32 id, std::string name, bool ctor = false) :
        tTJSNativeClassMethod(nullptr), id(id), name(std::move(name)), constructor(ctor) {}
    tjs_error FuncCall(tjs_uint32 flag, const tjs_char *member,
                       tjs_uint32 *hint, tTJSVariant *result, tjs_int count,
                       tTJSVariant **params, iTJSDispatch2 *objthis) override {
        if(member) return TJS_E_MEMBERNOTFOUND;
        if(result) result->Clear();
        if(name == "finalize") return TJS_S_OK;
        auto *instance = native<Native>(objthis, id);
        if(!instance) return TJS_E_NATIVECLASSCRASH;
        if(constructor) return instance->Construct(count, params, objthis);
        return instance->call(name, result, count, params);
    }
};
class Property : public tTJSNativeClassProperty {
    tjs_int32 id;
    std::string name;
public:
    Property(tjs_int32 id, std::string name) :
        tTJSNativeClassProperty(nullptr, nullptr), id(id), name(std::move(name)) {}
    tjs_error PropGet(tjs_uint32, const tjs_char *member, tjs_uint32 *,
                      tTJSVariant *result, iTJSDispatch2 *objthis) override {
        if(member) return TJS_E_MEMBERNOTFOUND;
        auto *instance = native<Native>(objthis, id);
        return instance ? instance->get(name, result) : TJS_E_NATIVECLASSCRASH;
    }
    tjs_error PropSet(tjs_uint32, const tjs_char *member, tjs_uint32 *,
                      const tTJSVariant *value, iTJSDispatch2 *objthis) override {
        if(member) return TJS_E_MEMBERNOTFOUND;
        auto *instance = native<Native>(objthis, id);
        return instance ? instance->set(name, *value) : TJS_E_NATIVECLASSCRASH;
    }
};
class ShaderObject : public tTJSCustomObject {
public:
    tjs_error PropSetByVS(tjs_uint32 flag, tTJSVariantString *member,
                          const tTJSVariant *value, iTJSDispatch2 *objthis) override {
        // The bytecode VM uses this optimized entry point for literal names.
        // Route it through the same reflection boundary as ordinary PropSet.
        return PropSet(flag, member ? member->operator const tjs_char *() : nullptr,
                        nullptr, value, objthis);
    }
    tjs_error PropGet(tjs_uint32 flag, const tjs_char *member,
                      tjs_uint32 *hint, tTJSVariant *result,
                      iTJSDispatch2 *objthis) override {
        if(member && !(flag & TJS_IGNOREPROP)) {
            auto *shader = native<Shader>(objthis, ShaderID);
            if(shader) {
                auto error = shader->get(utf8(ttstr(member)), result);
                if(error != TJS_E_MEMBERNOTFOUND) return error;
            }
        }
        return tTJSCustomObject::PropGet(flag, member, hint, result, objthis);
    }
    tjs_error PropSet(tjs_uint32 flag, const tjs_char *member,
                      tjs_uint32 *hint, const tTJSVariant *value,
                      iTJSDispatch2 *objthis) override {
        if(member && !(flag & TJS_IGNOREPROP)) {
            auto *shader = native<Shader>(objthis, ShaderID);
            if(shader) {
                auto error = shader->set(utf8(ttstr(member)), *value);
                if(error != TJS_E_MEMBERNOTFOUND) return error;
            }
        }
        return tTJSCustomObject::PropSet(flag, member, hint, value, objthis);
    }
};
class Class : public tTJSNativeClass {
    std::function<Native *()> factory;
    bool shaderObject;
protected:
    iTJSNativeInstance *CreateNativeInstance() override { return factory(); }
    iTJSDispatch2 *CreateBaseTJSObject() override {
        return shaderObject ? new ShaderObject() :
                              tTJSNativeClass::CreateBaseTJSObject();
    }
public:
    Class(const tjs_char *name, tjs_int32 &id,
          std::function<Native *()> factory, bool shaderObject) :
        tTJSNativeClass(name), factory(std::move(factory)), shaderObject(shaderObject) {
        id = TJSRegisterNativeClass(name);
        SetClassID(id);
        RegisterNCM(name, new Method(id, utf8(ttstr(name)), true), name, nitMethod);
        RegisterNCM(TJS_W("finalize"), new Method(id, "finalize"), name, nitMethod);
    }
};
tTJSNI_Bitmap *bitmap(const tTJSVariant &v) {
    return native<tTJSNI_Bitmap>(v, tTJSNC_Bitmap::ClassID);
}
tTVPRect rectangle(const tTJSVariant &v) {
    auto *r = native<tTJSNI_Rect>(v, tTJSNC_Rect::ClassID);
    if(!r) fail("rectangle argument requires a Rect");
    return r->Get();
}
tTVPRect copyRectangle(tjs_int n, tTJSVariant **p, int start) {
    if(n > start && native<tTJSNI_Rect>(*p[start], tTJSNC_Rect::ClassID))
        return rectangle(*p[start]);
    if(n >= start + 4) {
        int left = (tjs_int)*p[start], top = (tjs_int)*p[start + 1];
        return tTVPRect(left, top, left + (tjs_int)*p[start + 2],
                        top + (tjs_int)*p[start + 3]);
    }
    fail("copyRect requires a source rectangle");
}
Shader &getShader(const tTJSVariant &value) {
    auto *s = native<Shader>(value, ShaderID);
    if(!s) fail("drawing argument requires a ShaderProgram");
    return *s;
}
float real(const tTJSVariant &value) { return (float)(tjs_real)value; }
}

tTJSNativeClass *createClass(const tjs_char *name, tjs_int32 &id,
                              std::function<Native *()> factory,
                              const std::vector<const char *> &methods,
                              const std::vector<const char *> &properties,
                              bool shaderObject) {
    auto *c = new Class(name, id, std::move(factory), shaderObject);
    for(auto *method : methods)
        c->RegisterNCM(wide(method).c_str(), new Method(id, method), name, nitMethod);
    for(auto *property : properties)
        c->RegisterNCM(wide(property).c_str(), new Property(id, property), name, nitProperty);
    return c;
}

tjs_error Texture::Construct(tjs_int n, tTJSVariant **p, iTJSDispatch2 *) {
    if(n < 1) return TJS_E_BADPARAMCOUNT;
    GLState saved;
    int format = n > 1 ? (tjs_int)*p[1] : 0;
    bool ninePatch = n > 2 && (tjs_int)*p[2];
    bool pot = n > 3 && (tjs_int)*p[3];
    if(p[0]->Type() == tvtString) {
        if(ninePatch) fail("nine-patch Texture decoding is not implemented");
        tTVPBaseBitmap image(1, 1, format == 1 ? 8 : 32);
        TVPLoadGraphic(&image, ttstr(*p[0]), 0x1fffffff, 0, 0,
                        format == 1 ? glmGrayscale : glmNormal);
        upload(image, format == 1, pot);
    } else if(auto *b = bitmap(*p[0])) {
        if(ninePatch) fail("nine-patch Texture decoding is not implemented");
        upload(*b->GetBitmap(), format == 1, pot);
    } else if(p[0]->Type() == tvtObject) {
        fail("Texture requires a filename, Bitmap, or numeric dimensions");
    } else {
        if(n < 2) return TJS_E_BADPARAMCOUNT;
        allocate((tjs_int)*p[0], (tjs_int)*p[1], n > 2 && (tjs_int)*p[2] == 1);
    }
    return TJS_S_OK;
}
tjs_error Texture::call(const std::string &name, tTJSVariant *, tjs_int n,
                         tTJSVariant **p) {
    if(name == "setDrawSize") {
        if(n < 2) return TJS_E_BADPARAMCOUNT;
        drawWidth = (tjs_int)*p[0]; drawHeight = (tjs_int)*p[1];
        return TJS_S_OK;
    }
    if(name == "copyRect") {
        GLState saved;
        if(n == 1) {
            auto *b = bitmap(*p[0]);
            if(!b) return TJS_E_INVALIDPARAM;
            copyBitmap(0, 0, *b->GetBitmap(),
                        tTVPRect(0, 0, b->GetWidth(), b->GetHeight()));
        } else if(n >= 4) {
            auto *b = bitmap(*p[2]);
            if(!b) return TJS_E_INVALIDPARAM;
            copyBitmap((tjs_int)*p[0], (tjs_int)*p[1], *b->GetBitmap(),
                        copyRectangle(n, p, 3));
        } else return TJS_E_BADPARAMCOUNT;
        return TJS_S_OK;
    }
    return TJS_E_MEMBERNOTFOUND;
}
tjs_error Texture::get(const std::string &name, tTJSVariant *result) {
    if(!result) return TJS_S_OK;
    if(name == "width") *result = drawWidth;
    else if(name == "height") *result = drawHeight;
    else if(name == "memoryWidth") *result = width;
    else if(name == "memoryHeight") *result = height;
    else if(name == "isGray") *result = (tjs_int)gray;
    else if(name == "isPowerOfTwo")
        *result = (tjs_int)(width > 0 && height > 0 &&
            !(width & (width - 1)) && !(height & (height - 1)));
    else if(name == "nativeHandle") *result = (tjs_int64)texture;
    else if(name == "stretchType") *result = filter;
    else if(name == "wrapModeHorizontal") *result = wrapX;
    else if(name == "wrapModeVertical") *result = wrapY;
    else if(name == "margin9Patch") *result = tTJSVariant((iTJSDispatch2 *)nullptr);
    else return TJS_E_MEMBERNOTFOUND;
    return TJS_S_OK;
}
tjs_error Texture::set(const std::string &name, const tTJSVariant &value) {
    GLint v = (tjs_int)value;
    if(name == "stretchType") {
        if(v != GL_NEAREST && v != GL_LINEAR) return TJS_E_INVALIDPARAM;
        filter = v;
    } else if(name == "wrapModeHorizontal" || name == "wrapModeVertical") {
        if(v != GL_REPEAT && v != GL_CLAMP_TO_EDGE && v != GL_MIRRORED_REPEAT)
            return TJS_E_INVALIDPARAM;
        (name == "wrapModeHorizontal" ? wrapX : wrapY) = v;
    } else return TJS_E_ACCESSDENYED;
    GLState saved;
    glActiveTexture(GL_TEXTURE0); sampling();
    return TJS_S_OK;
}
tjs_error Offscreen::Construct(tjs_int n, tTJSVariant **p, iTJSDispatch2 *) {
    if(n < 2) return TJS_E_BADPARAMCOUNT;
    GLState saved;
    allocate((tjs_int)*p[0], (tjs_int)*p[1]);
    bindTarget();
    return TJS_S_OK;
}
tjs_error Offscreen::call(const std::string &name, tTJSVariant *, tjs_int n,
                           tTJSVariant **p) {
    GLState saved;
    if(name == "copyRect") {
        if(n < 4) return TJS_E_BADPARAMCOUNT;
        auto *b = bitmap(*p[2]);
        if(!b) return TJS_E_INVALIDPARAM;
        copyBitmap((tjs_int)*p[0], (tjs_int)*p[1], *b->GetBitmap(),
                    copyRectangle(n, p, 3));
    } else if(name == "copyTo") {
        if(n < 1) return TJS_E_BADPARAMCOUNT;
        auto *b = bitmap(*p[0]);
        if(!b) return TJS_E_INVALIDPARAM;
        if(n == 1)
            readBitmap(*b->GetBitmap(), 0, 0, tTVPRect(0, 0, width, height), true);
        else if(n >= 4)
            readBitmap(*b->GetBitmap(), (tjs_int)*p[1], (tjs_int)*p[2],
                        copyRectangle(n, p, 3));
        else return TJS_E_BADPARAMCOUNT;
    } else if(name == "exchangeTexture") {
        if(n < 1) return TJS_E_BADPARAMCOUNT;
        auto *t = native<Texture>(*p[0], TextureID);
        if(!t) return TJS_E_INVALIDPARAM;
        exchange(*t);
    } else return TJS_E_MEMBERNOTFOUND;
    return TJS_S_OK;
}
tjs_error Offscreen::get(const std::string &name, tTJSVariant *result) {
    if(!result) return TJS_S_OK;
    if(name == "width") *result = width;
    else if(name == "height") *result = height;
    else if(name == "nativeHandle") *result = (tjs_int64)texture;
    else return TJS_E_MEMBERNOTFOUND;
    return TJS_S_OK;
}

tjs_error Matrix::Construct(tjs_int n, tTJSVariant **p, iTJSDispatch2 *) {
    if(n) return call("set", nullptr, n, p);
    reset();
    return TJS_S_OK;
}
tjs_error Matrix::call(const std::string &name, tTJSVariant *, tjs_int n,
                        tTJSVariant **p) {
    if(name == "reset") { reset(); return TJS_S_OK; }
    if(name == "set") {
        if(n == 6) for(int i = 0; i < 6; ++i) m[i] = real(*p[i]);
        else if(n == 1) {
            auto a = numbers(*p[0], 6);
            std::copy_n(a.begin(), 6, m.begin());
        } else return TJS_E_BADPARAMCOUNT;
        return TJS_S_OK;
    }
    if(name == "setTranslate" || name == "setScale" ||
       name == "translate" || name == "scale" || name == "preScale") {
        if(n < 2) return TJS_E_BADPARAMCOUNT;
        float x = real(*p[0]), y = real(*p[1]);
        std::array<float, 6> t = name.find("ranslate") != std::string::npos ?
            std::array<float, 6>{1, 0, 0, 1, x, y} :
            std::array<float, 6>{x, 0, 0, y, 0, 0};
        if(name == "setTranslate" || name == "setScale") m = t;
        else if(name == "preScale") {
            m = { m[0] * x, m[1] * y, m[2] * x, m[3] * y,
                  m[4] * x, m[5] * y };
        } else post(t);
        return TJS_S_OK;
    }
    if(name == "setRotate" || name == "rotate") {
        if(n < 1) return TJS_E_BADPARAMCOUNT;
        float radians = real(*p[0]) * 3.1415927f / 180.f;
        float c = std::cos(radians), s = std::sin(radians);
        std::array<float, 6> r { c, s, -s, c, 0, 0 };
        if(name == "setRotate") m = r;
        else {
            if(n >= 3) {
                float x = real(*p[1]), y = real(*p[2]);
                r[4] = x - x * c + y * s;
                r[5] = y - x * s - y * c;
            }
            post(r);
        }
        return TJS_S_OK;
    }
    if(name == "transform") {
        if(n < 2) return TJS_E_BADPARAMCOUNT;
        float x = real(*p[0]), y = real(*p[1]);
        *p[0] = (tjs_real)(m[2] * y + m[0] * x + m[4]);
        *p[1] = (tjs_real)(m[3] * y + m[1] * x + m[5]);
        return TJS_S_OK;
    }
    if(name == "multiply" || name == "premultiply") {
        if(n < 1) return TJS_E_BADPARAMCOUNT;
        auto *other = native<Matrix>(*p[0], MatrixID);
        if(!other) return TJS_E_INVALIDPARAM;
        if(name == "premultiply") post(other->m);
        else { auto old = m; m = other->m; post(old); }
        return TJS_S_OK;
    }
    if(name == "setSkewX" || name == "setSkewY" ||
       name == "skewX" || name == "skewY")
        fail("Matrix32 skew operations are not implemented");
    return TJS_E_MEMBERNOTFOUND;
}
tjs_error Matrix::get(const std::string &name, tTJSVariant *result) {
    if(!result) return TJS_S_OK;
    if(name == "array") {
        if(!object(array)) array = makeArray(m.data(), 6);
        else for(int i = 0; i < 6; ++i) {
            tTJSVariant v((tjs_real)m[i]);
            auto *a = object(array);
            a->PropSetByNum(TJS_MEMBERENSURE, i, &v, a);
        }
        *result = array;
        return TJS_S_OK;
    }
    const char *names[] = { "m11", "m12", "m21", "m22", "m31", "m32" };
    for(int i = 0; i < 6; ++i) if(name == names[i]) {
        *result = (tjs_real)m[i]; return TJS_S_OK;
    }
    return TJS_E_MEMBERNOTFOUND;
}
tjs_error Matrix::set(const std::string &name, const tTJSVariant &value) {
    const char *names[] = { "m11", "m12", "m21", "m22", "m31", "m32" };
    for(int i = 0; i < 6; ++i) if(name == names[i]) {
        m[i] = real(value); return TJS_S_OK;
    }
    return TJS_E_ACCESSDENYED;
}

tjs_error Canvas::call(const std::string &name, tTJSVariant *result,
                        tjs_int n, tTJSVariant **p) {
    // Outside Window.onDraw, an explicit Canvas call is still a real drawing
    // operation. Preserve the surrounding renderer state for such calls.
    std::unique_ptr<GLState> saved;
    if(!frameState) saved = std::make_unique<GLState>();
    if(name == "clear") {
        state();
        auto color = n > 0 ? (tjs_uint32)(tjs_int64)*p[0] : clearColor;
        glClearColor((color >> 16 & 255) / 255.f, (color >> 8 & 255) / 255.f,
                      (color & 255) / 255.f, (color >> 24) / 255.f);
        glClear(GL_COLOR_BUFFER_BIT);
    } else if(name == "drawTexture") {
        if(n < 1 || n > 4) return TJS_E_BADPARAMCOUNT;
        int count = n < 3 ? 1 : n - 1;
        std::vector<Surface *> textures;
        for(int i = 0; i < count; ++i) {
            auto *t = surface(*p[i]);
            if(!t) return TJS_E_INVALIDPARAM;
            textures.push_back(t);
        }
        auto &s = getShader(n == 1 ? shader : *p[n - 1]);
        draw(textures, s, nullptr);
    } else if(name == "drawTextureAtlas") {
        if(n < 2) return TJS_E_BADPARAMCOUNT;
        auto *t = surface(*p[1]);
        if(!t) return TJS_E_INVALIDPARAM;
        auto r = rectangle(*p[0]);
        draw({t}, getShader(n > 2 ? *p[2] : shader), &r);
    } else if(name == "fill") {
        if(n < 2) return TJS_E_BADPARAMCOUNT;
        std::array<tjs_uint32, 4> colors { 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff };
        if(n > 2) {
            if(auto *a = object(*p[2])) {
                for(int i = 0; i < 4; ++i) {
                    tTJSVariant v;
                    if(TJS_FAILED(a->PropGetByNum(0, i, &v, a))) return TJS_E_INVALIDPARAM;
                    colors[i] = (tjs_int64)v;
                }
            } else colors.fill((tjs_int64)*p[2]);
        }
        fill(real(*p[0]), real(*p[1]), colors,
               getShader(n > 3 ? *p[3] : fillShader));
    } else if(name == "save") {
        auto *m = native<Matrix>(matrix, MatrixID);
        stack.push_back({m->m, rectangle(clip), clipEnabled, cull});
    } else if(name == "restore") {
        if(!stack.empty()) {
            auto state = stack.back(); stack.pop_back();
            native<Matrix>(matrix, MatrixID)->m = state.matrix;
            native<tTJSNI_Rect>(clip, tTJSNC_Rect::ClassID)->Get() = state.clip;
            clipEnabled = state.clipEnabled; cull = state.cull;
        }
    } else if(name == "flush") glFlush();
    else if(name == "capture") {
        if(n < 1) return TJS_E_BADPARAMCOUNT;
        state();
        bool flip = n < 2 || (tjs_int)*p[1];
        if(auto *b = bitmap(*p[0]))
            current().readBitmap(*b->GetBitmap(), 0, 0,
                                  tTVPRect(0, 0, width(), height()), true, flip);
        else if(auto *t = surface(*p[0])) {
            if(t->texture == current().texture)
                fail("capture destination is the current render target");
            auto rgba = current().readRGBA();
            int w = std::min(width(), t->width), h = std::min(height(), t->height);
            std::vector<unsigned char> pixels((size_t)w * h * 4);
            for(int row = 0; row < h; ++row)
                std::copy_n(rgba.data() + (size_t)(flip ? row : height() - 1 - row) * width() * 4,
                            w * 4, pixels.data() + (size_t)row * w * 4);
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, t->texture);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h,
                            GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        } else return TJS_E_INVALIDPARAM;
    } else if(name == "drawMesh" || name == "draw9Patch" || name == "drawText") {
        fail((std::string("Canvas.") + name + " is not implemented").c_str());
    } else return TJS_E_MEMBERNOTFOUND;
    return TJS_S_OK;
}
tjs_error Canvas::get(const std::string &name, tTJSVariant *result) {
    if(!result) return TJS_S_OK;
    if(name == "clearColor") *result = (tjs_int64)clearColor;
    else if(name == "blendMode") *result = blendMode;
    else if(name == "enableClipRect") *result = (tjs_int)clipEnabled;
    else if(name == "enableCulling") *result = (tjs_int)cull;
    else if(name == "width") *result = width();
    else if(name == "height") *result = height();
    else if(name == "matrix") *result = matrix;
    else if(name == "renderTarget") *result = target;
    else if(name == "defaultShader") *result = shader;
    else if(name == "defaultFillShader") *result = fillShader;
    else if(name == "clipRect") *result = clip;
    else return TJS_E_MEMBERNOTFOUND;
    return TJS_S_OK;
}
tjs_error Canvas::set(const std::string &name, const tTJSVariant &value) {
    if(name == "clearColor") clearColor = (tjs_int64)value;
    else if(name == "blendMode") blendMode = (tjs_int)value;
    else if(name == "enableClipRect") clipEnabled = (tjs_int)value != 0;
    else if(name == "enableCulling") cull = (tjs_int)value != 0;
    else if(name == "matrix") {
        if(!native<Matrix>(value, MatrixID)) return TJS_E_INVALIDPARAM;
        matrix = value;
    } else if(name == "renderTarget") {
        if(object(value) && !native<Offscreen>(value, OffscreenID)) return TJS_E_INVALIDPARAM;
        target = value;
        if(frameState) { glFlush(); current().bindTarget(); }
    } else if(name == "defaultShader" || name == "defaultFillShader") {
        if(!native<Shader>(value, ShaderID)) return TJS_E_INVALIDPARAM;
        (name == "defaultShader" ? shader : fillShader) = value;
    } else if(name == "clipRect") {
        if(!native<tTJSNI_Rect>(value, tTJSNC_Rect::ClassID)) return TJS_E_INVALIDPARAM;
        clip = value;
    } else return TJS_E_ACCESSDENYED;
    return TJS_S_OK;
}

} // namespace webcanvas

void TVPRegisterWebCanvasClasses(iTJSDispatch2 *global,
                                 tTJSNativeClass *windowClass) {
    using namespace webcanvas;
    const auto add = [global](const tjs_char *name, tTJSNativeClass *cls) {
        tTJSVariant value(cls);
        global->PropSet(TJS_MEMBERENSURE, name, nullptr, &value, global);
        cls->Release();
    };
    auto *texture = createClass(TJS_W("Texture"), TextureID, [] { return new Texture; },
        { "copyRect", "setDrawSize" },
        { "width", "height", "memoryWidth", "memoryHeight", "isGray", "isPowerOfTwo",
          "nativeHandle", "stretchType", "wrapModeHorizontal", "wrapModeVertical", "margin9Patch" });
    const std::pair<const char *, int> constants[] = {
        {"stNearest", GL_NEAREST}, {"stLinear", GL_LINEAR}, {"wmRepeat", GL_REPEAT},
        {"wmClampToEdge", GL_CLAMP_TO_EDGE}, {"wmMirror", GL_MIRRORED_REPEAT}
    };
    for(auto [name, number] : constants) {
        tTJSVariant value((tjs_int)number);
        texture->PropSet(TJS_MEMBERENSURE | TJS_STATICMEMBER, wide(name).c_str(),
                          nullptr, &value, texture);
    }
    add(TJS_W("Texture"), texture);
    add(TJS_W("Offscreen"), createClass(TJS_W("Offscreen"), OffscreenID,
        [] { return new Offscreen; }, { "copyRect", "copyTo", "exchangeTexture" },
        { "width", "height", "nativeHandle" }));
    add(TJS_W("Matrix32"), createClass(TJS_W("Matrix32"), MatrixID,
        [] { return new Matrix; },
        { "set", "reset", "setTranslate", "setScale", "setRotate", "setSkewX", "setSkewY",
          "translate", "rotate", "scale", "preScale", "skewX", "skewY", "transform",
          "multiply", "premultiply" },
        { "array", "m11", "m12", "m21", "m22", "m31", "m32" }));
    add(TJS_W("ShaderProgram"), createClass(TJS_W("ShaderProgram"), ShaderID,
        [] { return new Shader; }, {}, {}, true));
    add(TJS_W("Canvas"), createClass(TJS_W("Canvas"), CanvasID,
        [] { return new Canvas; },
        { "clear", "capture", "fill", "drawTexture", "drawTextureAtlas", "drawText",
          "drawMesh", "draw9Patch", "flush", "save", "restore" },
        { "clearColor", "renderTarget", "clipRect", "blendMode", "matrix", "defaultShader",
          "defaultFillShader", "width", "height", "enableClipRect", "enableCulling" }));
    registerWindow(windowClass);
}
