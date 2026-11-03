#include "CubismWebBridge.h"

#include <emscripten.h>
#include <cstdlib>
#include <limits>
#include <stdexcept>

EM_JS_DEPS(cubism_web_dependencies, "$stringToNewUTF8,$UTF8ToString,$GL");

namespace cubism::web {
    namespace {
        void CheckError() {
            char *message = reinterpret_cast<char *>(MAIN_THREAD_EM_ASM_PTR({
                const text = Module['krkrCubismError'];
                Module['krkrCubismError'] = "";
                return text ? stringToNewUTF8(text) : 0;
            }));
            if(!message) return;
            const std::string text(message);
            std::free(message);
            throw std::runtime_error(text);
        }

        std::string ReadName(std::uint32_t handle, std::size_t index,
                             bool part) {
            char *value = reinterpret_cast<char *>(MAIN_THREAD_EM_ASM_PTR({
                try {
                    const model = krkrCubismWeb.model($0);
                    const source = $2 ? model.parts : model.parameters;
                    if($1 >= source.count) throw new Error('Cubism index is out of range');
                    return stringToNewUTF8(source.ids[$1]);
                } catch(error) {
                    Module['krkrCubismError'] = String(error.message || error);
                    return 0;
                }
            }, handle, index, part));
            CheckError();
            if(!value) throw std::bad_alloc();
            const std::string result(value);
            std::free(value);
            return result;
        }
    }

    bool IsCoreAvailable() {
        return MAIN_THREAD_EM_ASM_INT({
            return typeof krkrCubismWeb !== 'undefined' && krkrCubismWeb.available() ? 1 : 0;
        }) != 0;
    }

    void Uninitialize() {
        MAIN_THREAD_EM_ASM({
            try { krkrCubismWeb.uninitialize(); }
            catch(error) { Module['krkrCubismError'] = String(error.message || error); }
        });
        CheckError();
    }

    Animation::Animation(const char *json, std::size_t size) : Handle(0) {
        Handle = MAIN_THREAD_EM_ASM_INT({
            try {
                return krkrCubismWeb.createAnimation(JSON.parse(UTF8ToString($0, $1).replace(/^\uFEFF/, "")));
            } catch(error) {
                Module['krkrCubismError'] = String(error.message || error);
                return 0;
            }
        }, json, size);
        CheckError();
    }

    Animation::~Animation() {
        try { Close(); } catch(...) { }
    }

    void Animation::Close() {
        if(!Handle) return;
        MAIN_THREAD_EM_ASM({ krkrCubismWeb.releaseAnimation($0); }, Handle);
        Handle = 0;
    }

    Model::Model(const void *mocBytes, std::size_t size) : Handle(0) {
        if(size > std::numeric_limits<std::uint32_t>::max())
            throw std::runtime_error("Cubism MOC3 input is too large");
        Handle = MAIN_THREAD_EM_ASM_INT({
            try {
                if(typeof krkrCubismWeb === 'undefined')
                    throw new Error('Cubism Web bridge is not installed');
                return krkrCubismWeb.create(HEAPU8.slice($0, $0 + $1).buffer);
            } catch(error) {
                Module['krkrCubismError'] = String(error.message || error);
                return 0;
            }
        }, mocBytes, size);
        CheckError();
    }

    Model::~Model() {
        try { Close(); } catch(...) { }
    }

    void Model::Close() {
        if(!Handle) return;
        MAIN_THREAD_EM_ASM({
            try { krkrCubismWeb.release($0); }
            catch(error) { Module['krkrCubismError'] = String(error.message || error); }
        }, Handle);
        Handle = 0;
        CheckError();
    }

    void Model::LoadDocument(DocumentKind kind, const char *json, std::size_t size) {
        MAIN_THREAD_EM_ASM({
            try {
                const document = JSON.parse(UTF8ToString($2, $3).replace(/^\uFEFF/, ""));
                if($1 === 0) krkrCubismWeb.configure($0, document);
                else {
                    const controller = krkrCubismWeb.controller($0);
                    if($1 === 2) controller.loadExternalParameters(document);
                    else controller.loadPhysics(document, $1 === 3);
                }
            } catch(error) { Module['krkrCubismError'] = String(error.message || error); }
        }, Handle, static_cast<int>(kind), json, size);
        CheckError();
    }

    std::size_t Model::ParameterCount() const {
        const int count = MAIN_THREAD_EM_ASM_INT({
            try { return krkrCubismWeb.model($0).parameters.count; }
            catch(error) { Module['krkrCubismError'] = String(error.message || error); return 0; }
        }, Handle);
        CheckError();
        return static_cast<std::size_t>(count);
    }

    std::string Model::ParameterName(std::size_t index) const {
        return ReadName(Handle, index, false);
    }

    double Model::GetParameter(std::size_t index, ParameterField field) const {
        const double value = MAIN_THREAD_EM_ASM_DOUBLE({
            try {
                const source = krkrCubismWeb.model($0).parameters;
                if($1 >= source.count) throw new Error('Cubism index is out of range');
                const fields = ([source.values, source.minimumValues, source.maximumValues, source.defaultValues]);
                return fields[$2][$1];
            } catch(error) {
                Module['krkrCubismError'] = String(error.message || error);
                return 0;
            }
        }, Handle, index, static_cast<int>(field));
        CheckError();
        return value;
    }

    void Model::SetParameter(std::size_t index, double value) {
        MAIN_THREAD_EM_ASM({
            try {
                const source = krkrCubismWeb.model($0).parameters;
                if($1 >= source.count) throw new Error('Cubism index is out of range');
                source.values[$1] = $2;
            } catch(error) { Module['krkrCubismError'] = String(error.message || error); }
        }, Handle, index, value);
        CheckError();
    }

    std::size_t Model::PartCount() const {
        const int count = MAIN_THREAD_EM_ASM_INT({
            try { return krkrCubismWeb.model($0).parts.count; }
            catch(error) { Module['krkrCubismError'] = String(error.message || error); return 0; }
        }, Handle);
        CheckError();
        return static_cast<std::size_t>(count);
    }

    std::string Model::PartName(std::size_t index) const {
        return ReadName(Handle, index, true);
    }

    double Model::GetPartOpacity(std::size_t index) const {
        const double value = MAIN_THREAD_EM_ASM_DOUBLE({
            try {
                const source = krkrCubismWeb.model($0).parts;
                if($1 >= source.count) throw new Error('Cubism index is out of range');
                return source.opacities[$1];
            } catch(error) { Module['krkrCubismError'] = String(error.message || error); return 0; }
        }, Handle, index);
        CheckError();
        return value;
    }

    void Model::SetPartOpacity(std::size_t index, double value) {
        MAIN_THREAD_EM_ASM({
            try {
                const source = krkrCubismWeb.model($0).parts;
                if($1 >= source.count) throw new Error('Cubism index is out of range');
                source.opacities[$1] = $2;
            } catch(error) { Module['krkrCubismError'] = String(error.message || error); }
        }, Handle, index, value);
        CheckError();
    }

    bool Model::GetNamedParameter(const std::string &name, double &value) const {
        const int found = MAIN_THREAD_EM_ASM_INT({
            try {
                const value = krkrCubismWeb.controller($0).get(UTF8ToString($1));
                if(value === undefined) return 0;
                HEAPF64[$2 >>> 3] = value;
                return 1;
            } catch(error) { Module['krkrCubismError'] = String(error.message || error); return 0; }
        }, Handle, name.c_str(), &value);
        CheckError();
        return found != 0;
    }

    bool Model::SetNamedParameter(const std::string &name, double value) {
        const int found = MAIN_THREAD_EM_ASM_INT({
            try { return krkrCubismWeb.controller($0).set(UTF8ToString($1), $2) ? 1 : 0; }
            catch(error) { Module['krkrCubismError'] = String(error.message || error); return 0; }
        }, Handle, name.c_str(), value);
        CheckError();
        return found != 0;
    }

    bool Model::SetParameterRate(const std::string &name, double value) {
        const int found = MAIN_THREAD_EM_ASM_INT({
            try { return krkrCubismWeb.controller($0).setRate(UTF8ToString($1), $2) ? 1 : 0; }
            catch(error) { Module['krkrCubismError'] = String(error.message || error); return 0; }
        }, Handle, name.c_str(), value);
        CheckError();
        return found != 0;
    }

    void Model::ResetParameters(bool systemOnly) {
        MAIN_THREAD_EM_ASM({
            try {
                const controller = krkrCubismWeb.controller($0);
                if($1) controller.resetSystem();
                else controller.resetDefaults();
            } catch(error) { Module['krkrCubismError'] = String(error.message || error); }
        }, Handle, systemOnly);
        CheckError();
    }

    void Model::SetExternalPhysicsEnabled(int index, bool enabled) {
        MAIN_THREAD_EM_ASM({
            try { krkrCubismWeb.controller($0).setExternalEnabled($1, $2); }
            catch(error) { Module['krkrCubismError'] = String(error.message || error); }
        }, Handle, index, enabled);
        CheckError();
    }

    void Model::ApplyAnimation(const Animation &animation, double delta, double weight) {
        MAIN_THREAD_EM_ASM({
            try { krkrCubismWeb.applyAnimation($0, $1, $2, $3); }
            catch(error) { Module['krkrCubismError'] = String(error.message || error); }
        }, Handle, animation.Handle, delta, weight);
        CheckError();
    }

    void Model::Update() {
        MAIN_THREAD_EM_ASM({
            try { krkrCubismWeb.update($0); }
            catch(error) { Module['krkrCubismError'] = String(error.message || error); }
        }, Handle);
        CheckError();
    }

    void Model::Update(double delta, const double *properties, int voicePeak) {
        MAIN_THREAD_EM_ASM({
            try {
                const names = (['eyeBlinkRate', 'lipSyncRate', 'gravityX', 'gravityY', 'windX', 'windY',
                                'positionX', 'positionY', 'scaleX', 'scaleY', 'enablePhysics']);
                const options = {};
                for(let i = 0; i < names.length; ++i) options[names[i]] = HEAPF64[($2 >>> 3) + i];
                krkrCubismWeb.update($0, $1, options, $3);
            } catch(error) { Module['krkrCubismError'] = String(error.message || error); }
        }, Handle, delta, properties, voicePeak);
        CheckError();
    }

    void Model::Draw(const std::uint32_t *textures, std::size_t textureCount,
                     int width, int height, double positionX, double positionY,
                     double scaleX, double scaleY) {
        if(width <= 0 || height <= 0)
            throw std::runtime_error("Cubism render target has an invalid size");
        MAIN_THREAD_EM_ASM({
            try {
                if(typeof GLctx === 'undefined' || !GLctx)
                    throw new Error('Cubism requires a current WebGL2 context');
                const textures = [];
                for(let i = 0; i < $2; ++i)
                    textures.push(GL.textures[HEAPU32[($1 >>> 2) + i]] || null);
                krkrCubismWeb.draw($0, GLctx, textures, $3, $4, $5, $6, $7, $8);
            } catch(error) { Module['krkrCubismError'] = String(error.message || error); }
        }, Handle, textures, textureCount, width, height,
           positionX, positionY, scaleX, scaleY);
        CheckError();
    }

    bool Model::HitTest(const std::string &name, int x, int y, int width, int height,
                        double positionX, double positionY, double scaleX, double scaleY) {
        const int hit = MAIN_THREAD_EM_ASM_INT({
            try { return krkrCubismWeb.hitTest($0, UTF8ToString($1), $2, $3, $4, $5, $6, $7, $8, $9) ? 1 : 0; }
            catch(error) { Module['krkrCubismError'] = String(error.message || error); return 0; }
        }, Handle, name.c_str(), x, y, width, height, positionX, positionY, scaleX, scaleY);
        CheckError();
        return hit != 0;
    }
} // namespace cubism::web
