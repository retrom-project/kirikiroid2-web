#pragma once

#include "tjsCommHead.h"
#include "tjsNative.h"
#include "ComplexRect.h"
#include <GLES3/gl3.h>
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class iTVPBaseBitmap;
class tTJSNI_Window;

namespace webcanvas {

extern tjs_int32 TextureID, OffscreenID, MatrixID, ShaderID, CanvasID;

[[noreturn]] void fail(const char *message);
std::string utf8(const ttstr &text);
ttstr wide(const std::string &text);
iTJSDispatch2 *object(const tTJSVariant &v);
template<class T> T *native(iTJSDispatch2 *obj, tjs_int32 id) {
    iTJSNativeInstance *ni = nullptr;
    if(!obj || TJS_FAILED(obj->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
                                                     id, &ni)))
        return nullptr;
    return static_cast<T *>(ni);
}
template<class T> T *native(const tTJSVariant &v, tjs_int32 id) {
    return native<T>(object(v), id);
}
tTJSVariant makeObject(const tjs_char *name, tjs_int count = 0,
                       tTJSVariant **params = nullptr);
tTJSVariant makeArray(const float *data, int count);
std::vector<float> numbers(const tTJSVariant &value, int count);

class Native : public tTJSNativeInstance {
public:
    virtual tjs_error call(const std::string &, tTJSVariant *, tjs_int,
                            tTJSVariant **) { return TJS_E_MEMBERNOTFOUND; }
    virtual tjs_error get(const std::string &, tTJSVariant *) {
        return TJS_E_MEMBERNOTFOUND;
    }
    virtual tjs_error set(const std::string &, const tTJSVariant &) {
        return TJS_E_MEMBERNOTFOUND;
    }
};

// Resource operations and whole Canvas frames preserve the caller's actual GL
// state. Raw GL users (including model renderers) share the current target FBO.
class GLState {
    GLint drawFbo, readFbo, viewport[4], scissor[4], program, vao, arrayBuffer;
    GLint activeTexture, textures[3], pack, unpack;
    GLint blendSrcRGB, blendDstRGB, blendSrcAlpha, blendDstAlpha;
    GLint blendEqRGB, blendEqAlpha, frontFace, cullFace;
    GLboolean blend, cull, depth, stencil, scissorEnabled, colorMask[4];
    GLfloat clearColor[4];
public:
    GLState();
    ~GLState();
    GLState(const GLState &) = delete;
    GLState &operator=(const GLState &) = delete;
};

class Surface {
public:
    GLuint texture = 0, fbo = 0;
    int width = 0, height = 0, drawWidth = 0, drawHeight = 0;
    bool gray = false;
    GLint filter = GL_LINEAR, wrapX = GL_CLAMP_TO_EDGE,
          wrapY = GL_CLAMP_TO_EDGE;
    Surface() = default;
    Surface(const Surface &) = delete;
    Surface &operator=(const Surface &) = delete;
    virtual ~Surface();
    void release();
    void allocate(int w, int h, bool alpha = false);
    void upload(const iTVPBaseBitmap &bitmap, bool alpha = false,
                bool powerOfTwo = false);
    void copyBitmap(int x, int y, const iTVPBaseBitmap &bitmap,
                    tTVPRect rect);
    void readBitmap(iTVPBaseBitmap &bitmap, int x, int y, tTVPRect rect,
                    bool resize = false, bool flip = true);
    void bindTarget();
    void sampling();
    void exchange(Surface &other);
    std::vector<unsigned char> readRGBA();
};

class Texture : public Native, public Surface {
public:
    tjs_error Construct(tjs_int, tTJSVariant **, iTJSDispatch2 *) override;
    void Invalidate() override { release(); }
    tjs_error call(const std::string &, tTJSVariant *, tjs_int,
                    tTJSVariant **) override;
    tjs_error get(const std::string &, tTJSVariant *) override;
    tjs_error set(const std::string &, const tTJSVariant &) override;
};
class Offscreen : public Native, public Surface {
public:
    tjs_error Construct(tjs_int, tTJSVariant **, iTJSDispatch2 *) override;
    void Invalidate() override { release(); }
    tjs_error call(const std::string &, tTJSVariant *, tjs_int,
                    tTJSVariant **) override;
    tjs_error get(const std::string &, tTJSVariant *) override;
};
Surface *surface(const tTJSVariant &v);

class Matrix : public Native {
    tTJSVariant array;
public:
    std::array<float, 6> m { 1, 0, 0, 1, 0, 0 };
    void reset() { m = { 1, 0, 0, 1, 0, 0 }; }
    void post(const std::array<float, 6> &other);
    std::array<float, 16> mat4() const;
    tjs_error Construct(tjs_int, tTJSVariant **, iTJSDispatch2 *) override;
    tjs_error call(const std::string &, tTJSVariant *, tjs_int,
                    tTJSVariant **) override;
    tjs_error get(const std::string &, tTJSVariant *) override;
    tjs_error set(const std::string &, const tTJSVariant &) override;
};

class Shader : public Native {
public:
    struct Variable {
        GLint location;
        GLenum type;
        GLint count;
        bool attribute;
        tTJSVariant value;
    };
    GLuint program = 0;
    std::map<std::string, Variable> variables;
    ~Shader() override;
    void Invalidate() override;
    void compile(const std::string &vert, const std::string &frag);
    void use();
    GLint location(const char *name, bool attribute = false) const;
    tjs_error Construct(tjs_int, tTJSVariant **, iTJSDispatch2 *) override;
    tjs_error get(const std::string &, tTJSVariant *) override;
    tjs_error set(const std::string &, const tTJSVariant &) override;
};

class Canvas : public Native {
    struct Saved {
        std::array<float, 6> matrix;
        tTVPRect clip;
        bool clipEnabled, cull;
    };
    std::vector<Saved> stack;
    std::unique_ptr<GLState> frameState;
    GLuint vao = 0, vbo = 0;
    void state();
    void attributes(Shader &, const float *, const float *, const float *);
    void uniforms(Shader &);
public:
    iTJSDispatch2 *window = nullptr; // borrowed; Window attachment owns Canvas
    Surface display;
    tTJSVariant matrix, target, shader, fillShader, clip;
    tjs_uint32 clearColor = 0xff000000;
    int blendMode = 2;
    bool clipEnabled = false, cull = false;
    ~Canvas() override;
    void Invalidate() override;
    int width() const;
    int height() const;
    Surface &current();
    void begin();
    void end();
    void draw(const std::vector<Surface *> &, Shader &, const tTVPRect *);
    void fill(float width, float height, const std::array<tjs_uint32, 4> &,
              Shader &);
    tjs_error Construct(tjs_int, tTJSVariant **, iTJSDispatch2 *) override;
    tjs_error call(const std::string &, tTJSVariant *, tjs_int,
                    tTJSVariant **) override;
    tjs_error get(const std::string &, tTJSVariant *) override;
    tjs_error set(const std::string &, const tTJSVariant &) override;
};

tTJSNativeClass *createClass(const tjs_char *name, tjs_int32 &id,
                              std::function<Native *()> factory,
                              const std::vector<const char *> &methods,
                              const std::vector<const char *> &properties,
                              bool shaderObject = false);
void registerWindow(tTJSNativeClass *windowClass);
void watchWindow(iTJSDispatch2 *window, Canvas *canvas);
void unwatchWindow(iTJSDispatch2 *window, Canvas *canvas);

} // namespace webcanvas
