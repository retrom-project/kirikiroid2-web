#include "WebCanvasInternal.h"
#include "LayerBitmapIntf.h"
#include "BitmapIntf.h"
#include "GraphicsLoaderIntf.h"
#include "RectItf.h"
#include "StorageIntf.h"
#include "WindowImpl.h"
#include "ScriptMgnIntf.h"
#include "tjsArray.h"
#include "cocos2d.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <regex>

namespace webcanvas {

tjs_int32 TextureID = -1, OffscreenID = -1, MatrixID = -1,
          ShaderID = -1, CanvasID = -1;

[[noreturn]] void fail(const char *message) {
    TJS_eTJSError(wide(std::string("Z drawing: ") + message));
    std::terminate(); // TJS_eTJSError throws; its declaration lacks noreturn.
}
std::string utf8(const ttstr &text) { return text.AsStdString(); }
ttstr wide(const std::string &text) { return ttstr(text.c_str()); }
iTJSDispatch2 *object(const tTJSVariant &v) {
    return v.Type() == tvtObject ? v.AsObjectNoAddRef() : nullptr;
}
tTJSVariant makeObject(const tjs_char *name, tjs_int count,
                       tTJSVariant **params) {
    auto *global = TVPGetScriptDispatch();
    tTJSVariant cls;
    tjs_error error = global->PropGet(0, name, nullptr, &cls, global);
    global->Release();
    if(TJS_FAILED(error))
        fail("drawing class is not registered");
    auto *factory = object(cls);
    iTJSDispatch2 *out = nullptr;
    if(!factory || TJS_FAILED(factory->CreateNew(0, nullptr, nullptr, &out,
                                                 count, params, factory)))
        fail("cannot construct drawing object");
    tTJSVariant result(out, out);
    out->Release();
    return result;
}
tTJSVariant makeArray(const float *data, int count) {
    auto *a = TJSCreateArrayObject();
    for(int i = 0; i < count; ++i) {
        tTJSVariant value((tjs_real)data[i]);
        a->PropSetByNum(TJS_MEMBERENSURE, i, &value, a);
    }
    tTJSVariant result(a, a);
    a->Release();
    return result;
}
std::vector<float> numbers(const tTJSVariant &value, int count) {
    std::vector<float> result(count);
    auto *a = object(value);
    if(auto *m = native<Matrix>(a, MatrixID)) {
        if(count == 16) {
            auto matrix = m->mat4();
            return { matrix.begin(), matrix.end() };
        }
        if(count == 6)
            return { m->m.begin(), m->m.end() };
    }
    if(!a) {
        if(count != 1)
            fail("shader vector or matrix requires an Array");
        result[0] = (float)(tjs_real)value;
        return result;
    }
    for(int i = 0; i < count; ++i) {
        tTJSVariant item;
        if(TJS_FAILED(a->PropGetByNum(0, i, &item, a)) || item.Type() == tvtVoid)
            fail("shader vector or matrix is too short");
        result[i] = (float)(tjs_real)item;
    }
    return result;
}

GLState::GLState() {
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_SCISSOR_BOX, scissor);
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
    for(int i = 0; i < 3; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, textures + i);
    }
    glActiveTexture(activeTexture);
    glGetIntegerv(GL_PACK_ALIGNMENT, &pack);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack);
    glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRGB);
    glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRGB);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstAlpha);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &blendEqRGB);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blendEqAlpha);
    glGetIntegerv(GL_FRONT_FACE, &frontFace);
    glGetIntegerv(GL_CULL_FACE_MODE, &cullFace);
    blend = glIsEnabled(GL_BLEND);
    cull = glIsEnabled(GL_CULL_FACE);
    depth = glIsEnabled(GL_DEPTH_TEST);
    stencil = glIsEnabled(GL_STENCIL_TEST);
    scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
    glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor);
}
GLState::~GLState() {
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
    glUseProgram(program);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, arrayBuffer);
    for(int i = 0; i < 3; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, textures[i]);
    }
    glActiveTexture(activeTexture);
    glPixelStorei(GL_PACK_ALIGNMENT, pack);
    glPixelStorei(GL_UNPACK_ALIGNMENT, unpack);
    glBlendFuncSeparate(blendSrcRGB, blendDstRGB, blendSrcAlpha, blendDstAlpha);
    glBlendEquationSeparate(blendEqRGB, blendEqAlpha);
    glFrontFace(frontFace);
    glCullFace(cullFace);
    const auto enable = [](GLenum what, GLboolean enabled) {
        if(enabled) glEnable(what); else glDisable(what);
    };
    enable(GL_BLEND, blend);
    enable(GL_CULL_FACE, cull);
    enable(GL_DEPTH_TEST, depth);
    enable(GL_STENCIL_TEST, stencil);
    enable(GL_SCISSOR_TEST, scissorEnabled);
    glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
    glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
    cocos2d::GL::invalidateStateCache();
}

Surface::~Surface() { release(); }
void Surface::release() {
    if(fbo) glDeleteFramebuffers(1, &fbo);
    if(texture) glDeleteTextures(1, &texture);
    fbo = texture = 0;
}
void Surface::sampling() {
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapX);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapY);
}
void Surface::allocate(int w, int h, bool alpha) {
    GLint maximum = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
    if(w <= 0 || h <= 0 || w > maximum || h > maximum)
        fail("invalid texture dimensions");
    release();
    width = drawWidth = w;
    height = drawHeight = h;
    gray = alpha;
    glGenTextures(1, &texture);
    glActiveTexture(GL_TEXTURE0);
    sampling();
    // RGBA8 also represents the native GL_ALPHA case in WebGL2. Its RGB
    // channels are zero and alpha contains the original grayscale samples.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
}
void Surface::bindTarget() {
    if(!texture)
        fail("texture has been invalidated");
    if(!fbo)
        glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, texture, 0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        fail("framebuffer is incomplete");
    glViewport(0, 0, width, height);
}
void Surface::upload(const iTVPBaseBitmap &bitmap, bool alpha, bool powerOfTwo) {
    int w = bitmap.GetWidth(), h = bitmap.GetHeight();
    int memoryW = w, memoryH = h;
    if(powerOfTwo) {
        const auto next = [](int value) {
            if((value & (value - 1)) == 0) return value;
            int n = 32;
            while(n < value) n *= 2;
            return n;
        };
        memoryW = next(w);
        memoryH = next(h);
    }
    allocate(memoryW, memoryH, alpha);
    drawWidth = w;
    drawHeight = h;
    std::vector<unsigned char> rgba((size_t)memoryW * memoryH * 4, 0);
    for(int y = 0; y < h; ++y) {
        auto *dst = rgba.data() + (size_t)(memoryH - 1 - y) * memoryW * 4;
        if(bitmap.GetBPP() == 8) {
            auto *src = (const unsigned char *)bitmap.GetScanLine(y);
            for(int x = 0; x < w; ++x) {
                if(alpha) dst[x * 4 + 3] = src[x];
                else {
                    dst[x * 4] = dst[x * 4 + 1] = dst[x * 4 + 2] = src[x];
                    dst[x * 4 + 3] = 255;
                }
            }
        } else {
            auto *src = (const tjs_uint32 *)bitmap.GetScanLine(y);
            for(int x = 0; x < w; ++x) {
                tjs_uint32 c = src[x];
                if(alpha) dst[x * 4 + 3] = ((c >> 16 & 255) * 19 +
                                           (c >> 8 & 255) * 38 + (c & 255) * 7) >> 6;
                else {
                    dst[x * 4] = c >> 16;
                    dst[x * 4 + 1] = c >> 8;
                    dst[x * 4 + 2] = c;
                    dst[x * 4 + 3] = c >> 24;
                }
            }
        }
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, memoryW, memoryH,
                    GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
}
void Surface::copyBitmap(int x, int y, const iTVPBaseBitmap &bitmap,
                         tTVPRect r) {
    r.clip(tTVPRect(0, 0, bitmap.GetWidth(), bitmap.GetHeight()));
    if(x < 0) { r.left -= x; x = 0; }
    if(y < 0) { r.top -= y; y = 0; }
    r.right = std::min(r.right, r.left + width - x);
    r.bottom = std::min(r.bottom, r.top + height - y);
    int w = r.get_width(), h = r.get_height();
    if(w <= 0 || h <= 0)
        return;
    if(bitmap.GetBPP() != 32)
        fail("copyRect requires a 32-bit Bitmap");
    std::vector<unsigned char> rgba((size_t)w * h * 4);
    for(int row = 0; row < h; ++row) {
        auto *src = (const tjs_uint32 *)bitmap.GetScanLine(r.top + row) + r.left;
        auto *dst = rgba.data() + (size_t)(h - 1 - row) * w * 4;
        for(int col = 0; col < w; ++col) {
            auto c = src[col];
            dst[col * 4] = c >> 16;
            dst[col * 4 + 1] = c >> 8;
            dst[col * 4 + 2] = c;
            dst[col * 4 + 3] = c >> 24;
        }
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, height - y - h, w, h,
                    GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
}
std::vector<unsigned char> Surface::readRGBA() {
    bindTarget();
    std::vector<unsigned char> rgba((size_t)width * height * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    return rgba;
}
void Surface::readBitmap(iTVPBaseBitmap &bitmap, int x, int y, tTVPRect r,
                         bool resize, bool flip) {
    if(bitmap.GetBPP() != 32)
        fail("copyTo requires a 32-bit Bitmap");
    if(resize)
        bitmap.SetSize(width, height, false);
    r.clip(tTVPRect(0, 0, width, height));
    if(x < 0) { r.left -= x; x = 0; }
    if(y < 0) { r.top -= y; y = 0; }
    r.right = std::min(r.right, r.left + (int)bitmap.GetWidth() - x);
    r.bottom = std::min(r.bottom, r.top + (int)bitmap.GetHeight() - y);
    if(r.is_empty())
        return;
    auto rgba = readRGBA();
    for(int row = 0; row < r.get_height(); ++row) {
        int sourceY = flip ? height - 1 - r.top - row : r.top + row;
        auto *src = rgba.data() + ((size_t)sourceY * width + r.left) * 4;
        auto *dst = (tjs_uint32 *)bitmap.GetScanLineForWrite(y + row) + x;
        for(int col = 0; col < r.get_width(); ++col)
            dst[col] = ((tjs_uint32)src[col * 4 + 3] << 24) |
                ((tjs_uint32)src[col * 4] << 16) |
                ((tjs_uint32)src[col * 4 + 1] << 8) | src[col * 4 + 2];
    }
}
void Surface::exchange(Surface &other) {
    if(width != other.width || height != other.height || gray || other.gray)
        fail("exchangeTexture requires equal RGBA memory dimensions");
    std::swap(texture, other.texture);
    if(fbo) bindTarget();
    if(other.fbo) other.bindTarget();
}
Surface *surface(const tTJSVariant &value) {
    if(auto *t = native<Texture>(value, TextureID)) return t;
    return native<Offscreen>(value, OffscreenID);
}

void Matrix::post(const std::array<float, 6> &b) {
    auto a = m;
    m = { a[0] * b[0] + a[2] * b[1], a[1] * b[0] + a[3] * b[1],
          a[0] * b[2] + a[2] * b[3], a[1] * b[2] + a[3] * b[3],
          a[0] * b[4] + a[2] * b[5] + a[4],
          a[1] * b[4] + a[3] * b[5] + a[5] };
}
std::array<float, 16> Matrix::mat4() const {
    return { m[0], m[1], 0, 0, m[2], m[3], 0, 0,
             0, 0, 1, 0, m[4], m[5], 0, 1 };
}

namespace {
std::string shaderSource(const tTJSVariant &value, bool file) {
    if(!file) return utf8(ttstr(value));
    std::unique_ptr<tTJSBinaryStream> stream(TVPCreateStream(ttstr(value), TJS_BS_READ));
    auto size = stream->GetSize();
    if(size > 16 * 1024 * 1024) fail("shader source is too large");
    std::string source((size_t)size, '\0');
    stream->ReadBuffer(source.data(), (tjs_uint)size);
    if(source.compare(0, 3, "\xef\xbb\xbf") == 0) source.erase(0, 3);
    return source;
}
std::string webShader(std::string source, bool fragment) {
    // GLSL ES 1.00 is the native Z contract; WebGL2 uses ES 3.00. Rewrite
    // identifier tokens, not arbitrary substrings or filenames.
    if(source.find("#version 300 es") != std::string::npos) return source;
    source = std::regex_replace(source, std::regex(R"(^\s*#version[^\n]*\n)"), "");
    const std::regex token(R"(\b(attribute|varying|texture2D|textureCube|gl_FragColor)\b)");
    std::string body;
    size_t last = 0;
    for(auto it = std::sregex_iterator(source.begin(), source.end(), token);
        it != std::sregex_iterator(); ++it) {
        body.append(source, last, it->position() - last);
        auto word = it->str();
        body += word == "attribute" ? "in" : word == "varying" ?
            (fragment ? "in" : "out") : word == "gl_FragColor" ?
            "webCanvasColor" : "texture";
        last = it->position() + it->length();
    }
    body.append(source, last, std::string::npos);
    return std::string("#version 300 es\n") +
        (fragment ? "precision mediump float;\nout vec4 webCanvasColor;\n" : "") + body;
}
GLuint compileShader(GLenum type, const std::string &text) {
    auto source = webShader(text, type == GL_FRAGMENT_SHADER);
    const char *p = source.c_str();
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &p, nullptr);
    glCompileShader(shader);
    GLint ok = 0, size = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if(!ok) {
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &size);
        std::string message(std::max(1, size), '\0');
        glGetShaderInfoLog(shader, size, nullptr, message.data());
        glDeleteShader(shader);
        fail((std::string("shader compilation failed: ") + message).c_str());
    }
    return shader;
}
}

Shader::~Shader() { Invalidate(); }
void Shader::Invalidate() {
    if(program) glDeleteProgram(program);
    program = 0;
    variables.clear();
}
void Shader::compile(const std::string &vertex, const std::string &fragment) {
    Invalidate();
    GLuint vert = compileShader(GL_VERTEX_SHADER, vertex);
    GLuint frag = 0;
    try { frag = compileShader(GL_FRAGMENT_SHADER, fragment); }
    catch(...) { glDeleteShader(vert); throw; }
    program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);
    glDeleteShader(vert);
    glDeleteShader(frag);
    GLint ok = 0, size = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if(!ok) {
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &size);
        std::string message(std::max(1, size), '\0');
        glGetProgramInfoLog(program, size, nullptr, message.data());
        Invalidate();
        fail((std::string("shader link failed: ") + message).c_str());
    }
    for(int attrib = 0; attrib < 2; ++attrib) {
        GLint count = 0, length = 0;
        glGetProgramiv(program, attrib ? GL_ACTIVE_ATTRIBUTES : GL_ACTIVE_UNIFORMS, &count);
        glGetProgramiv(program, attrib ? GL_ACTIVE_ATTRIBUTE_MAX_LENGTH : GL_ACTIVE_UNIFORM_MAX_LENGTH, &length);
        std::vector<char> name(std::max(length, 1));
        for(int i = 0; i < count; ++i) {
            GLsizei n; GLint elements; GLenum type;
            if(attrib) glGetActiveAttrib(program, i, length, &n, &elements, &type, name.data());
            else glGetActiveUniform(program, i, length, &n, &elements, &type, name.data());
            std::string key(name.data(), n);
            GLint loc = attrib ? glGetAttribLocation(program, key.c_str()) :
                                 glGetUniformLocation(program, key.c_str());
            if(key.size() > 3 && key.compare(key.size() - 3, 3, "[0]") == 0)
                key.resize(key.size() - 3);
            variables.emplace(key, Variable { loc, type, elements, !!attrib, {} });
        }
    }
}
GLint Shader::location(const char *name, bool attribute) const {
    auto found = variables.find(name);
    return found != variables.end() && found->second.attribute == attribute ?
        found->second.location : -1;
}
tjs_error Shader::Construct(tjs_int n, tTJSVariant **p, iTJSDispatch2 *) {
    if(n < 2) return TJS_E_BADPARAMCOUNT;
    GLState saved;
    compile(shaderSource(*p[0], n < 3 || (tjs_int)*p[2]),
            shaderSource(*p[1], n < 4 || (tjs_int)*p[3]));
    return TJS_S_OK;
}
tjs_error Shader::get(const std::string &name, tTJSVariant *result) {
    auto it = variables.find(name);
    if(it == variables.end()) return TJS_E_MEMBERNOTFOUND;
    if(result) *result = it->second.value;
    return TJS_S_OK;
}
tjs_error Shader::set(const std::string &name, const tTJSVariant &value) {
    auto it = variables.find(name);
    if(it == variables.end()) return TJS_E_MEMBERNOTFOUND;
    it->second.value = value;
    return TJS_S_OK;
}
void Shader::use() {
    if(!program) fail("ShaderProgram has been invalidated");
    glUseProgram(program);
    int sampler = 0;
    for(auto &[name, v] : variables) {
        if(v.value.Type() == tvtVoid) continue;
        if(v.attribute) {
            // Generic array-backed vertex attributes are supplied by drawMesh;
            // Canvas quad attributes are overridden with its own VBO below.
            continue;
        }
        int components = 1;
        switch(v.type) {
            case GL_FLOAT_VEC2: case GL_INT_VEC2: case GL_BOOL_VEC2: components = 2; break;
            case GL_FLOAT_VEC3: case GL_INT_VEC3: case GL_BOOL_VEC3: components = 3; break;
            case GL_FLOAT_VEC4: case GL_INT_VEC4: case GL_BOOL_VEC4: components = 4; break;
            case GL_FLOAT_MAT2: components = 4; break;
            case GL_FLOAT_MAT3: components = 9; break;
            case GL_FLOAT_MAT4: components = 16; break;
            case GL_SAMPLER_2D: {
                auto *s = surface(v.value);
                if(!s) fail("sampler requires a Texture or Offscreen");
                if(sampler >= 3) fail("more than three script texture samplers are not supported");
                glActiveTexture(GL_TEXTURE0 + sampler);
                glBindTexture(GL_TEXTURE_2D, s->texture);
                glUniform1i(v.location, sampler++);
                continue;
            }
        }
        auto f = numbers(v.value, components * v.count);
        std::vector<GLint> ints;
        if(v.type == GL_INT || v.type == GL_BOOL ||
           (v.type >= GL_INT_VEC2 && v.type <= GL_BOOL_VEC4)) {
            ints.assign(f.begin(), f.end());
            switch(components) {
                case 1: glUniform1iv(v.location, v.count, ints.data()); break;
                case 2: glUniform2iv(v.location, v.count, ints.data()); break;
                case 3: glUniform3iv(v.location, v.count, ints.data()); break;
                case 4: glUniform4iv(v.location, v.count, ints.data()); break;
            }
        } else switch(v.type) {
            case GL_FLOAT: glUniform1fv(v.location, v.count, f.data()); break;
            case GL_FLOAT_VEC2: glUniform2fv(v.location, v.count, f.data()); break;
            case GL_FLOAT_VEC3: glUniform3fv(v.location, v.count, f.data()); break;
            case GL_FLOAT_VEC4: glUniform4fv(v.location, v.count, f.data()); break;
            case GL_FLOAT_MAT2: glUniformMatrix2fv(v.location, v.count, GL_FALSE, f.data()); break;
            case GL_FLOAT_MAT3: glUniformMatrix3fv(v.location, v.count, GL_FALSE, f.data()); break;
            case GL_FLOAT_MAT4: glUniformMatrix4fv(v.location, v.count, GL_FALSE, f.data()); break;
            default: fail("unsupported reflected shader uniform type");
        }
    }
}

namespace {
const char *vertexTexture = R"(
attribute vec2 a_pos;
attribute vec2 a_texCoord;
uniform mat4 a_modelMat4;
uniform vec2 a_size;
varying vec2 v_uv;
void main() {
    vec4 p = a_modelMat4 * vec4(a_pos, 0.0, 1.0);
    gl_Position = vec4(p.x * 2.0 / a_size.x - 1.0,
                       1.0 - p.y * 2.0 / a_size.y, 0.0, 1.0);
    v_uv = a_texCoord;
})";
const char *fragmentTexture = R"(
precision mediump float;
varying vec2 v_uv;
uniform sampler2D s_tex0;
uniform float a_opacity;
void main() {
    gl_FragColor = texture2D(s_tex0, v_uv);
    gl_FragColor.a *= a_opacity;
})";
const char *vertexFill = R"(
attribute vec2 a_pos;
attribute vec4 a_color;
uniform mat4 a_modelMat4;
uniform vec2 a_size;
varying vec4 v_color;
void main() {
    vec4 p = a_modelMat4 * vec4(a_pos, 0.0, 1.0);
    gl_Position = vec4(p.x * 2.0 / a_size.x - 1.0,
                       1.0 - p.y * 2.0 / a_size.y, 0.0, 1.0);
    v_color = a_color;
})";
const char *fragmentFill = R"(
precision mediump float;
varying vec4 v_color;
void main() { gl_FragColor = v_color; }
)";
}

Canvas::~Canvas() { Invalidate(); }
void Canvas::Invalidate() {
    end();
    if(vao) glDeleteVertexArrays(1, &vao);
    if(vbo) glDeleteBuffers(1, &vbo);
    vao = vbo = 0;
    display.release();
    if(window) unwatchWindow(window, this);
    window = nullptr;
    matrix.Clear(); target.Clear(); shader.Clear(); fillShader.Clear(); clip.Clear();
}
tjs_error Canvas::Construct(tjs_int n, tTJSVariant **p, iTJSDispatch2 *) {
    if(n < 1) return TJS_E_BADPARAMCOUNT;
    window = object(*p[0]);
    if(!native<tTJSNI_Window>(window, tTJSNC_Window::ClassID))
        return TJS_E_INVALIDPARAM;
    // Direct new Canvas(window) objects can outlive the Window too. Observe
    // invalidation without a strong Window/Canvas reference cycle.
    watchWindow(window, this);
    GLState saved;
    matrix = makeObject(TJS_W("Matrix32"));
    tTJSVariant vs(wide(vertexTexture)), fs(wide(fragmentTexture)), no((tjs_int)0);
    tTJSVariant *args[] = { &vs, &fs, &no, &no };
    shader = makeObject(TJS_W("ShaderProgram"), 4, args);
    native<Shader>(shader, ShaderID)->set("a_opacity", tTJSVariant((tjs_real)1));
    vs = wide(vertexFill); fs = wide(fragmentFill);
    fillShader = makeObject(TJS_W("ShaderProgram"), 4, args);
    auto *rect = TVPCreateRectObject(-1, -1, -1, -1);
    clip = tTJSVariant(rect, rect); rect->Release();
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    return TJS_S_OK;
}
int Canvas::width() const {
    if(auto *s = surface(target)) return s->width;
    auto *w = native<tTJSNI_Window>(window, tTJSNC_Window::ClassID);
    return w ? w->GetInnerWidth() : 0;
}
int Canvas::height() const {
    if(auto *s = surface(target)) return s->height;
    auto *w = native<tTJSNI_Window>(window, tTJSNC_Window::ClassID);
    return w ? w->GetInnerHeight() : 0;
}
Surface &Canvas::current() {
    if(auto *s = surface(target)) return *s;
    int w = width(), h = height();
    if(display.width != w || display.height != h || !display.texture)
        display.allocate(w, h);
    return display;
}
void Canvas::state() {
    current().bindTarget();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    if(blendMode == 0) glDisable(GL_BLEND);
    else {
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        switch(blendMode) {
            case 1: glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_ZERO); break;
            case 3: glBlendFunc(GL_ONE, GL_ONE); break;
            case 4: glBlendFunc(GL_SRC_ALPHA, GL_ONE); break;
            default: glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
        }
    }
    if(clipEnabled) {
        auto *r = native<tTJSNI_Rect>(clip, tTJSNC_Rect::ClassID);
        if(!r) fail("clipRect requires a Rect");
        auto &rc = r->Get();
        glEnable(GL_SCISSOR_TEST);
        glScissor(rc.left, height() - rc.bottom,
                   std::max(0, rc.get_width()), std::max(0, rc.get_height()));
    } else glDisable(GL_SCISSOR_TEST);
    if(cull) { glEnable(GL_CULL_FACE); glCullFace(GL_BACK); }
    else glDisable(GL_CULL_FACE);
}
void Canvas::begin() {
    if(frameState) return;
    frameState = std::make_unique<GLState>();
    stack.clear();
    // Native Z clears the window surface before rebinding a retained offscreen
    // target. It does not clear the selected Offscreen at frame start.
    auto *w = native<tTJSNI_Window>(window, tTJSNC_Window::ClassID);
    int windowW = w ? w->GetInnerWidth() : 0;
    int windowH = w ? w->GetInnerHeight() : 0;
    if(display.width != windowW || display.height != windowH || !display.texture)
        display.allocate(windowW, windowH);
    display.bindTarget();
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor((clearColor >> 16 & 255) / 255.f,
                  (clearColor >> 8 & 255) / 255.f,
                  (clearColor & 255) / 255.f, (clearColor >> 24) / 255.f);
    glClear(GL_COLOR_BUFFER_BIT);
    state();
}
void Canvas::end() {
    stack.clear();
    if(frameState) glFlush();
    frameState.reset();
}
void Canvas::uniforms(Shader &s) {
    auto *mat = native<Matrix>(matrix, MatrixID);
    if(!mat) fail("matrix requires a Matrix32");
    auto m = mat->mat4();
    glUniformMatrix4fv(s.location("a_modelMat4"), 1, GL_FALSE, m.data());
    glUniform2f(s.location("a_size"), width(), height());
}
void Canvas::attributes(Shader &s, const float *positions, const float *uv,
                         const float *colors) {
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    float vertices[32] = {};
    for(int i = 0; i < 4; ++i) {
        std::copy_n(positions + i * 2, 2, vertices + i * 8);
        if(uv) std::copy_n(uv + i * 2, 2, vertices + i * 8 + 2);
        if(colors) std::copy_n(colors + i * 4, 4, vertices + i * 8 + 4);
    }
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
    const char *names[] = { "a_pos", "a_texCoord", "a_color" };
    const int sizes[] = { 2, 2, 4 }, offsets[] = { 0, 2, 4 };
    for(int i = 0; i < 3; ++i) {
        GLint loc = s.location(names[i], true);
        if(loc >= 0) {
            glEnableVertexAttribArray(loc);
            glVertexAttribPointer(loc, sizes[i], GL_FLOAT, GL_FALSE,
                                   8 * sizeof(float), (void *)(uintptr_t)(offsets[i] * sizeof(float)));
        }
    }
}
void Canvas::draw(const std::vector<Surface *> &textures, Shader &s,
                   const tTVPRect *crop) {
    state();
    auto &t = *textures[0];
    float w = t.drawWidth, h = t.drawHeight;
    float u0 = 0, v0 = 1, u1 = 1, v1 = 0;
    if(crop) {
        auto r = *crop;
        r.clip(tTVPRect(0, 0, t.width, t.height));
        if(r.is_empty()) return;
        w = r.get_width(); h = r.get_height();
        u0 = (float)r.left / t.width; u1 = (float)r.right / t.width;
        v0 = 1 - (float)r.top / t.height; v1 = 1 - (float)r.bottom / t.height;
    }
    float pos[] = { 0, 0, 0, h, w, 0, w, h };
    float uv[] = { u0, v0, u0, v1, u1, v0, u1, v1 };
    s.use();
    attributes(s, pos, uv, nullptr);
    uniforms(s);
    for(size_t i = 0; i < textures.size(); ++i) {
        if(textures[i]->texture == current().texture)
            fail("cannot sample the current render target");
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, textures[i]->texture);
        auto name = std::string("s_tex") + std::to_string(i);
        glUniform1i(s.location(name.c_str()), i);
    }
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}
void Canvas::fill(float w, float h, const std::array<tjs_uint32, 4> &color,
                   Shader &s) {
    state();
    float pos[] = { 0, 0, 0, h, w, 0, w, h };
    float colors[16];
    for(int i = 0; i < 4; ++i) {
        colors[i * 4] = (color[i] >> 16 & 255) / 255.f;
        colors[i * 4 + 1] = (color[i] >> 8 & 255) / 255.f;
        colors[i * 4 + 2] = (color[i] & 255) / 255.f;
        colors[i * 4 + 3] = (color[i] >> 24) / 255.f;
    }
    s.use(); attributes(s, pos, nullptr, colors); uniforms(s);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

} // namespace webcanvas
