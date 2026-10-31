#define NCB_MODULE_NAME TJS_W("layerexparticle.dll")
#include "ncbind.hpp"
#include "layerExBase.hpp"
#include "layerExParticleManager.hpp"
#include "TickCount.h"
#include "DebugIntf.h"

#include <cmath>
#include <cstring>
#include <new>

namespace layerexparticle {

struct ImageRect_guess {
    tjs_int x, y, width, height;
    double halfWidth, halfHeight;
};

struct DirtyRect_guess {
    double x, y, width, height;
};

struct layerExBase {
    using DispatchT = iTJSDispatch2 *;
    explicit layerExBase(DispatchT object) :
        owner_guess(object), imageLeft_guess(object, TJS_W("imageLeft")),
        imageTop_guess(object, TJS_W("imageTop")),
        imageWidth_guess(object, TJS_W("imageWidth")),
        imageHeight_guess(object, TJS_W("imageHeight")),
        imageBuffer_guess(object, TJS_W("mainImageBufferForWrite")),
        imagePitch_guess(object, TJS_W("mainImageBufferPitch")),
        update_guess(object, TJS_W("update")), left_guess(0), top_guess(0),
        width_guess(0), height_guess(0) {}
    virtual ~layerExBase() = default;
    virtual void redraw() {
        tTJSVariant values[4] = {imageLeft_guess, imageTop_guess, imageWidth_guess, imageHeight_guess};
        tTJSVariant *args[4] = {values, values + 1, values + 2, values + 3};
        update_guess(4, args);
    }
    virtual void reset() {
        left_guess = static_cast<ObjectCache::IntegerT>(imageLeft_guess);
        top_guess = static_cast<ObjectCache::IntegerT>(imageTop_guess);
        width_guess = static_cast<ObjectCache::IntegerT>(imageWidth_guess);
        height_guess = static_cast<ObjectCache::IntegerT>(imageHeight_guess);
    }
    DispatchT owner_guess;
    ObjectCache imageLeft_guess, imageTop_guess, imageWidth_guess,
        imageHeight_guess, imageBuffer_guess, imagePitch_guess, update_guess;
    tjs_int left_guess, top_guess, width_guess, height_guess;
};

struct layerExParticle : layerExBase {
    explicit layerExParticle(DispatchT object) : layerExBase(object),
        manager_guess(nullptr), dirtyRects_guess(nullptr), dirtyCount_guess(0),
        image_guess(nullptr), imageRects_guess(nullptr), imageCount_guess(0),
        operateMode_guess(2) {}
    ~layerExParticle() override { uninitParticle(); }

    void finishInit_guess() {
        manager_guess->ownerLayer = owner_guess;
        if(dirtyRects_guess) {
            ::operator delete(dirtyRects_guess);
            dirtyRects_guess = nullptr;
            dirtyCount_guess = 0;
        }
        dirtyRects_guess = static_cast<DirtyRect_guess *>(
            ::operator new(sizeof(DirtyRect_guess) * manager_guess->capacity_guess));
        dirtyCount_guess = 0;
        tTJSVariant value;
        auto *global = TVPGetScriptDispatch();
        global->PropGet(0, TJS_W("Layer"), nullptr, &value, global);
        auto *original = value.AsObjectNoAddRef();
        tTJSVariant values[5] = {0, 0, 0, 0, 0};
        tTJSVariant *args[5] = {values, values + 1, values + 2, values + 3, values + 4};
        original->PropGet(0, TJS_W("imageWidth"), nullptr, &value, owner_guess);
        values[2] = value;
        original->PropGet(0, TJS_W("imageHeight"), nullptr, &value, owner_guess);
        values[3] = value;
        original->FuncCall(0, TJS_W("fillRect"), nullptr, &value, 5, args, owner_guess);
        TVPAddLog(TJS_W("layerExParticle: initialized"));
    }
    void initVectorParticle(tjs_int start, tjs_int maximum, double rate,
                            double x, double y, double width, double height) {
        if(!manager_guess) {
            manager_guess = new VectorParticleManager(start, maximum, rate, x, y, width, height);
            particleType_guess = 1;
            finishInit_guess();
        }
    }
    void initRotateParticle(tjs_int start, tjs_int maximum, double rate,
                            double x, double y) {
        if(!manager_guess) {
            manager_guess = new RotateParticleManager(start, maximum, rate, x, y);
            particleType_guess = 2;
            finishInit_guess();
        }
    }
    void initAccelRotateParticle(tjs_int start, tjs_int maximum, double rate,
                                 double x, double y) {
        if(!manager_guess) {
            manager_guess = new AccelRotateParticleManager(start, maximum, rate, x, y);
            particleType_guess = 4;
            finishInit_guess();
        }
    }
    void initBlinkParticle(tjs_int start, tjs_int maximum, double rate,
                           double x, double y, double width, double height) {
        if(!manager_guess) {
            manager_guess = new BlinkParticleManager(start, maximum, rate, x, y, width, height);
            particleType_guess = 3;
            finishInit_guess();
        }
    }
    void uninitParticle() {
        if(manager_guess) {
            // The native teardown releases only the manager allocation.
            manager_guess->ParticleManager::~ParticleManager();
            ::operator delete(manager_guess);
            manager_guess = nullptr;
        }
        if(dirtyRects_guess) {
            ::operator delete(dirtyRects_guess);
            dirtyRects_guess = nullptr;
            dirtyCount_guess = 0;
        }
        if(imageRects_guess) {
            ::operator delete(imageRects_guess);
            imageRects_guess = nullptr;
            imageCount_guess = 0;
        }
    }
    void assignParticle(tTJSVariant source);
    tjs_int getInitializedParticle() const {
        return manager_guess && manager_guess->imageCount != 0;
    }
    iTJSDispatch2 *getParticleImage() const { return image_guess; }
    void setParticleImage(tTJSVariant input) {
        auto *image = input.AsObjectNoAddRef();
        tTJSVariant value;
        if(TJS_FAILED(image->PropGet(0, TJS_W("tag"), nullptr, &value, image)) &&
           TJS_FAILED(image->PropGet(0, TJS_W("orgTag"), nullptr, &value, image))) {
            TVPAddLog(TJS_W("layerExParticle: image has no tag"));
            return;
        }
        auto *tag = value.AsObjectNoAddRef();
        if(TJS_FAILED(tag->PropGet(0, TJS_W("divideArea"), nullptr, &value, tag))) {
            TVPAddLog(TJS_W("layerExParticle: image has no divideArea"));
            return;
        }
        if(imageRects_guess)
            ::operator delete(imageRects_guess);
        if(value.Type() == tvtObject) {
            auto *areas = value.AsObjectNoAddRef();
            areas->PropGet(0, TJS_W("count"), nullptr, &value, areas);
            const tjs_int count = value.AsInteger();
            imageRects_guess = static_cast<ImageRect_guess *>(
                ::operator new(sizeof(ImageRect_guess) * count));
            imageCount_guess = 0;
            for(tjs_int i = 0; i < count; ++i) {
                areas->PropGetByNum(0, i, &value, areas);
                if(value.Type() != tvtObject)
                    continue;
                auto *rect = value.AsObjectNoAddRef();
                auto &out = imageRects_guess[imageCount_guess];
                rect->PropGetByNum(0, 0, &value, rect);
                out.x = value.AsInteger();
                rect->PropGetByNum(0, 1, &value, rect);
                out.y = value.AsInteger();
                rect->PropGetByNum(0, 2, &value, rect);
                out.width = value.AsInteger();
                rect->PropGetByNum(0, 3, &value, rect);
                out.height = value.AsInteger();
                finishImageRect_guess(out);
                ++imageCount_guess;
            }
        } else if(value.Type() == tvtString) {
            const ttstr text(value.AsStringNoAddRef());
            imageCount_guess = 1;
            for(tjs_int i = 0; i < text.GetLen(); ++i)
                if(text.c_str()[i] == TJS_W('/'))
                    ++imageCount_guess;
            imageRects_guess = static_cast<ImageRect_guess *>(
                ::operator new(sizeof(ImageRect_guess) * imageCount_guess));
            tjs_int cursor = 0;
            for(tjs_int i = 0; i < imageCount_guess; ++i) {
                auto &rect = imageRects_guess[i];
                rect.x = rect.y = rect.width = rect.height = -1;
                tjs_int number = 0;
                for(; cursor < text.GetLen(); ++cursor) {
                    const tjs_char character = text.c_str()[cursor];
                    if(character >= TJS_W('0') && character <= TJS_W('9')) {
                        number = number * 10 + character - TJS_W('0');
                    } else {
                        if(rect.x < 0) rect.x = number;
                        else if(rect.y < 0) rect.y = number;
                        else if(rect.width < 0) rect.width = number;
                        else if(rect.height < 0) rect.height = number;
                        if(character == TJS_W('/')) {
                            ++cursor;
                            break;
                        }
                        number = 0;
                    }
                }
                if(rect.height < 0)
                    rect.height = number;
                finishImageRect_guess(rect);
            }
        }
        manager_guess->imageCount = imageCount_guess;
        if(!imageCount_guess)
            TVPAddLog(TJS_W("layerExParticle: no image divisions to draw"));
        image_guess = image;
    }
    void setParticleInitializer(tTJSVariant input) {
        if(manager_guess->particleInitializer)
            manager_guess->particleInitializer->Release();
        manager_guess->particleInitializer = input.AsObject();
    }
    void updateParticle(tjs_int delta);
    tjs_int getParticleMaxCount() const { return manager_guess->maxCount; }
    void setParticleMaxCount(tjs_int count) {
        const tjs_int formerCapacity = manager_guess->capacity_guess;
        manager_guess->setMaxCount(count);
        const tjs_int capacity = manager_guess->capacity_guess;
        if(capacity > formerCapacity) {
            auto *previous = dirtyRects_guess;
            dirtyRects_guess = static_cast<DirtyRect_guess *>(
                ::operator new(sizeof(DirtyRect_guess) * capacity));
            if(dirtyCount_guess > 0)
                std::memcpy(dirtyRects_guess, previous,
                            sizeof(DirtyRect_guess) * dirtyCount_guess);
            std::memset(dirtyRects_guess + dirtyCount_guess, 0,
                        sizeof(DirtyRect_guess) * (capacity - dirtyCount_guess));
            ::operator delete(previous);
        }
    }
    VectorParticleManager *vector_guess() const {
        return static_cast<VectorParticleManager *>(manager_guess);
    }
    RotateParticleManager *rotate_guess() const {
        return static_cast<RotateParticleManager *>(manager_guess);
    }
    AccelRotateParticleManager *accelRotate_guess() const {
        return static_cast<AccelRotateParticleManager *>(manager_guess);
    }
    BlinkParticleManager *blink_guess() const {
        return static_cast<BlinkParticleManager *>(manager_guess);
    }
    tjs_int getOperateMode() const { return operateMode_guess; }
    void setOperateMode(tjs_int value) { operateMode_guess = value; }
    tjs_int getParticleStartCount() const { return manager_guess->startCount; }
    tjs_int getParticleCount() const { return manager_guess->liveCount_guess; }
    tjs_int getParticleType() const { return particleType_guess; }
    double getParticleGenerateRate() const { return manager_guess->generateRate; }
    void setParticleGenerateRate(double value) { manager_guess->generateRate = value; }
    tjs_int getRotateReverse() const { return manager_guess->rotateReverse; }
    void setRotateReverse(tjs_int value) { manager_guess->rotateReverse = value; }
    double getParticleAppearLeft() const { return vector_guess()->x_guess; }
    void setParticleAppearLeft(double value) { vector_guess()->x_guess = value; }
    double getParticleAppearTop() const { return vector_guess()->y_guess; }
    void setParticleAppearTop(double value) { vector_guess()->y_guess = value; }
    double getParticleAppearWidth() const { return vector_guess()->dx_guess; }
    void setParticleAppearWidth(double value) { vector_guess()->dx_guess = value; }
    double getParticleAppearHeight() const { return vector_guess()->dy_guess; }
    void setParticleAppearHeight(double value) { vector_guess()->dy_guess = value; }
    double getParticleRotateCenterX() const { return rotate_guess()->x_guess; }
    void setParticleRotateCenterX(double value) { rotate_guess()->x_guess = value; }
    double getParticleRotateCenterY() const { return rotate_guess()->y_guess; }
    void setParticleRotateCenterY(double value) { rotate_guess()->y_guess = value; }
    tjs_int getParticleRotateReverse() const { return rotate_guess()->particleRotateReverse; }
    void setParticleRotateReverse(tjs_int value) { rotate_guess()->particleRotateReverse = value; }
    void setParticleAngle(double maximum, double minimum) {
        manager_guess->angleRange_guess.set(maximum, minimum);
    }
    double getMaxParticleAngle() const { return manager_guess->angleRange_guess.max; }
    void setMaxParticleAngle(double value) {
        manager_guess->angleRange_guess.max = value;
    }
    double getMinParticleAngle() const { return manager_guess->angleRange_guess.min; }
    void setMinParticleAngle(double value) {
        manager_guess->angleRange_guess.min = value;
    }
    void setParticleAngleOmega(double maximum, double minimum) {
        manager_guess->angleOmegaRange_guess.set(maximum, minimum);
    }
    double getMaxParticleAngleOmega() const { return manager_guess->angleOmegaRange_guess.max; }
    void setMaxParticleAngleOmega(double value) {
        manager_guess->angleOmegaRange_guess.max = value;
    }
    double getMinParticleAngleOmega() const { return manager_guess->angleOmegaRange_guess.min; }
    void setMinParticleAngleOmega(double value) {
        manager_guess->angleOmegaRange_guess.min = value;
    }
    void setParticleMagnify(double maximum, double minimum) {
        manager_guess->magnify.value.set(maximum, minimum);
    }
    double getMaxParticleMagnify() const { return manager_guess->magnify.value.max; }
    void setMaxParticleMagnify(double value) {
        manager_guess->magnify.value.max = value;
        manager_guess->magnify.value.span = manager_guess->magnify.value.max - manager_guess->magnify.value.min;
    }
    double getMinParticleMagnify() const { return manager_guess->magnify.value.min; }
    void setMinParticleMagnify(double value) {
        manager_guess->magnify.value.min = value;
        manager_guess->magnify.value.span = manager_guess->magnify.value.max - manager_guess->magnify.value.min;
    }
    void setParticleMagnifyVaridation(double maximum, double minimum) {
        manager_guess->magnify.variation.set(maximum, minimum);
    }
    double getMaxParticleMagnifyVaridation() const { return manager_guess->magnify.variation.max; }
    void setMaxParticleMagnifyVaridation(double value) {
        manager_guess->magnify.variation.max = value;
        manager_guess->magnify.variation.span = manager_guess->magnify.variation.max - manager_guess->magnify.variation.min;
    }
    double getMinParticleMagnifyVaridation() const { return manager_guess->magnify.variation.min; }
    void setMinParticleMagnifyVaridation(double value) {
        manager_guess->magnify.variation.min = value;
        manager_guess->magnify.variation.span = manager_guess->magnify.variation.max - manager_guess->magnify.variation.min;
    }
    void setParticleOpacity(double maximum, double minimum) {
        manager_guess->opacity.value.set(maximum, minimum);
    }
    double getMaxParticleOpacity() const { return manager_guess->opacity.value.max; }
    void setMaxParticleOpacity(double value) {
        manager_guess->opacity.value.max = value;
        manager_guess->opacity.value.span = manager_guess->opacity.value.max - manager_guess->opacity.value.min;
    }
    double getMinParticleOpacity() const { return manager_guess->opacity.value.min; }
    void setMinParticleOpacity(double value) {
        manager_guess->opacity.value.min = value;
        manager_guess->opacity.value.span = manager_guess->opacity.value.max - manager_guess->opacity.value.min;
    }
    void setParticleOpacityVaridation(double maximum, double minimum) {
        manager_guess->opacity.variation.set(maximum, minimum);
    }
    double getMaxParticleOpacityVaridation() const { return manager_guess->opacity.variation.max; }
    void setMaxParticleOpacityVaridation(double value) {
        manager_guess->opacity.variation.max = value;
        manager_guess->opacity.variation.span = manager_guess->opacity.variation.max - manager_guess->opacity.variation.min;
    }
    double getMinParticleOpacityVaridation() const { return manager_guess->opacity.variation.min; }
    void setMinParticleOpacityVaridation(double value) {
        manager_guess->opacity.variation.min = value;
        manager_guess->opacity.variation.span = manager_guess->opacity.variation.max - manager_guess->opacity.variation.min;
    }
    void setParticleSpeed(double maximum, double minimum) {
        vector_guess()->velocity.value.set(maximum, minimum);
    }
    double getMaxParticleSpeed() const { return vector_guess()->velocity.value.max; }
    void setMaxParticleSpeed(double value) {
        vector_guess()->velocity.value.max = value;
        vector_guess()->velocity.value.span = vector_guess()->velocity.value.max - vector_guess()->velocity.value.min;
    }
    double getMinParticleSpeed() const { return vector_guess()->velocity.value.min; }
    void setMinParticleSpeed(double value) {
        vector_guess()->velocity.value.min = value;
        vector_guess()->velocity.value.span = vector_guess()->velocity.value.max - vector_guess()->velocity.value.min;
    }
    void setParticleAccel(double maximum, double minimum) {
        vector_guess()->velocity.variation.set(maximum, minimum);
    }
    double getMaxParticleAccel() const { return vector_guess()->velocity.variation.max; }
    void setMaxParticleAccel(double value) {
        vector_guess()->velocity.variation.max = value;
        vector_guess()->velocity.variation.span = vector_guess()->velocity.variation.max - vector_guess()->velocity.variation.min;
    }
    double getMinParticleAccel() const { return vector_guess()->velocity.variation.min; }
    void setMinParticleAccel(double value) {
        vector_guess()->velocity.variation.min = value;
        vector_guess()->velocity.variation.span = vector_guess()->velocity.variation.max - vector_guess()->velocity.variation.min;
    }
    void setParticleVectorAngle(double maximum, double minimum) {
        vector_guess()->direction.value.set(maximum, minimum);
    }
    double getMaxParticleVectorAngle() const { return vector_guess()->direction.value.max; }
    void setMaxParticleVectorAngle(double value) {
        vector_guess()->direction.value.max = value;
        vector_guess()->direction.value.span = vector_guess()->direction.value.max - vector_guess()->direction.value.min;
    }
    double getMinParticleVectorAngle() const { return vector_guess()->direction.value.min; }
    void setMinParticleVectorAngle(double value) {
        vector_guess()->direction.value.min = value;
        vector_guess()->direction.value.span = vector_guess()->direction.value.max - vector_guess()->direction.value.min;
    }
    void setParticleVectorRotate(double maximum, double minimum) {
        vector_guess()->direction.variation.set(maximum, minimum);
    }
    double getMaxParticleVectorRotate() const { return vector_guess()->direction.variation.max; }
    void setMaxParticleVectorRotate(double value) {
        vector_guess()->direction.variation.max = value;
        vector_guess()->direction.variation.span = vector_guess()->direction.variation.max - vector_guess()->direction.variation.min;
    }
    double getMinParticleVectorRotate() const { return vector_guess()->direction.variation.min; }
    void setMinParticleVectorRotate(double value) {
        vector_guess()->direction.variation.min = value;
        vector_guess()->direction.variation.span = vector_guess()->direction.variation.max - vector_guess()->direction.variation.min;
    }
    void setParticleRotateRadius(double maximum, double minimum) {
        rotate_guess()->radius.value.set(maximum, minimum);
    }
    double getMaxParticleRotateRadius() const { return rotate_guess()->radius.value.max; }
    void setMaxParticleRotateRadius(double value) {
        rotate_guess()->radius.value.max = value;
        rotate_guess()->radius.value.span = rotate_guess()->radius.value.max - rotate_guess()->radius.value.min;
    }
    double getMinParticleRotateRadius() const { return rotate_guess()->radius.value.min; }
    void setMinParticleRotateRadius(double value) {
        rotate_guess()->radius.value.min = value;
        rotate_guess()->radius.value.span = rotate_guess()->radius.value.max - rotate_guess()->radius.value.min;
    }
    void setParticleRotateRadiusVaridation(double maximum, double minimum) {
        rotate_guess()->radius.variation.set(maximum, minimum);
    }
    double getMaxParticleRotateRadiusVaridation() const { return rotate_guess()->radius.variation.max; }
    void setMaxParticleRotateRadiusVaridation(double value) {
        rotate_guess()->radius.variation.max = value;
        rotate_guess()->radius.variation.span = rotate_guess()->radius.variation.max - rotate_guess()->radius.variation.min;
    }
    double getMinParticleRotateRadiusVaridation() const { return rotate_guess()->radius.variation.min; }
    void setMinParticleRotateRadiusVaridation(double value) {
        rotate_guess()->radius.variation.min = value;
        rotate_guess()->radius.variation.span = rotate_guess()->radius.variation.max - rotate_guess()->radius.variation.min;
    }
    void setParticleRotateOmega(double maximum, double minimum) {
        rotate_guess()->omega.set(maximum, minimum);
    }
    double getMaxParticleRotateOmega() const { return rotate_guess()->omega.max; }
    void setMaxParticleRotateOmega(double value) {
        rotate_guess()->omega.max = value;
        rotate_guess()->omega.span = rotate_guess()->omega.max - rotate_guess()->omega.min;
    }
    double getMinParticleRotateOmega() const { return rotate_guess()->omega.min; }
    void setMinParticleRotateOmega(double value) {
        rotate_guess()->omega.min = value;
        rotate_guess()->omega.span = rotate_guess()->omega.max - rotate_guess()->omega.min;
    }
    void setParticleRotateOmegaMax(double maximum, double minimum) {
        rotate_guess()->omega.set(maximum, minimum);
    }
    double getMaxParticleRotateOmegaMax() const { return rotate_guess()->omega.max; }
    void setMaxParticleRotateOmegaMax(double value) {
        rotate_guess()->omega.max = value;
        rotate_guess()->omega.span = rotate_guess()->omega.max - rotate_guess()->omega.min;
    }
    double getMinParticleRotateOmegaMax() const { return rotate_guess()->omega.min; }
    void setMinParticleRotateOmegaMax(double value) {
        rotate_guess()->omega.min = value;
        rotate_guess()->omega.span = rotate_guess()->omega.max - rotate_guess()->omega.min;
    }
    void setParticleRotateOmegaTime(double maximum, double minimum) {
        accelRotate_guess()->accelerationTime.set(maximum, minimum);
    }
    double getMaxParticleRotateOmegaTime() const { return accelRotate_guess()->accelerationTime.max; }
    void setMaxParticleRotateOmegaTime(double value) {
        accelRotate_guess()->accelerationTime.max = value;
        accelRotate_guess()->accelerationTime.span = accelRotate_guess()->accelerationTime.max - accelRotate_guess()->accelerationTime.min;
    }
    double getMinParticleRotateOmegaTime() const { return accelRotate_guess()->accelerationTime.min; }
    void setMinParticleRotateOmegaTime(double value) {
        accelRotate_guess()->accelerationTime.min = value;
        accelRotate_guess()->accelerationTime.span = accelRotate_guess()->accelerationTime.max - accelRotate_guess()->accelerationTime.min;
    }
    void setParticleBlinkMax(double maximum, double minimum) {
        manager_guess->opacity.value.set(maximum, minimum);
    }
    double getMaxParticleBlinkMax() const { return manager_guess->opacity.value.max; }
    void setMaxParticleBlinkMax(double value) {
        manager_guess->opacity.value.max = value;
        manager_guess->opacity.value.span = manager_guess->opacity.value.max - manager_guess->opacity.value.min;
    }
    double getMinParticleBlinkMax() const { return manager_guess->opacity.value.min; }
    void setMinParticleBlinkMax(double value) {
        manager_guess->opacity.value.min = value;
        manager_guess->opacity.value.span = manager_guess->opacity.value.max - manager_guess->opacity.value.min;
    }
    void setParticleBlinkTime(double maximum, double minimum) {
        blink_guess()->time.set(maximum, minimum);
    }
    double getMaxParticleBlinkTime() const { return blink_guess()->time.max; }
    void setMaxParticleBlinkTime(double value) {
        blink_guess()->time.max = value;
        blink_guess()->time.span = blink_guess()->time.max - blink_guess()->time.min;
    }
    double getMinParticleBlinkTime() const { return blink_guess()->time.min; }
    void setMinParticleBlinkTime(double value) {
        blink_guess()->time.min = value;
        blink_guess()->time.span = blink_guess()->time.max - blink_guess()->time.min;
    }
    void setParticleBlinkCount(double maximum, double minimum) {
        blink_guess()->count.set(maximum, minimum);
    }
    double getMaxParticleBlinkCount() const { return blink_guess()->count.max; }
    void setMaxParticleBlinkCount(double value) {
        blink_guess()->count.max = value;
        blink_guess()->count.span = blink_guess()->count.max - blink_guess()->count.min;
    }
    double getMinParticleBlinkCount() const { return blink_guess()->count.min; }
    void setMinParticleBlinkCount(double value) {
        blink_guess()->count.min = value;
        blink_guess()->count.span = blink_guess()->count.max - blink_guess()->count.min;
    }
    void setParticleAppearArea(double x, double y, double width, double height) {
        vector_guess()->x_guess = x;
        vector_guess()->y_guess = y;
        vector_guess()->dx_guess = width;
        vector_guess()->dy_guess = height;
    }
    void setParticleRotateCenter(double x, double y) {
        rotate_guess()->x_guess = x;
        rotate_guess()->y_guess = y;
    }

    ParticleManager *manager_guess;
    tjs_int particleType_guess;
    DirtyRect_guess *dirtyRects_guess;
    tjs_int dirtyCount_guess;
    iTJSDispatch2 *image_guess;
    ImageRect_guess *imageRects_guess;
    tjs_int imageCount_guess;
    tjs_int operateMode_guess;

private:
    static void finishImageRect_guess(ImageRect_guess &rect) {
        rect.halfWidth = rect.width * 0.5;
        rect.halfHeight = rect.height * 0.5;
        TVPAddLog(ttstr(TJS_W("layerExParticle: image rect ")) + ttstr(rect.x) +
                  TJS_W(",") + ttstr(rect.y) + TJS_W(",") + ttstr(rect.width) +
                  TJS_W(",") + ttstr(rect.height));
    }
};

void layerExParticle::assignParticle(tTJSVariant input) {
    auto *source = ncbInstanceAdaptor<layerExParticle>::GetNativeInstance(
        input.AsObjectNoAddRef(), true);
    if(source->manager_guess && source->manager_guess->imageCount) {
        switch(source->particleType_guess) {
            case 1: manager_guess = new VectorParticleManager(); particleType_guess = 1; break;
            case 2: manager_guess = new RotateParticleManager(); particleType_guess = 2; break;
            case 3: manager_guess = new BlinkParticleManager(); particleType_guess = 3; break;
            case 4: manager_guess = new AccelRotateParticleManager(); particleType_guess = 4; break;
        }
        manager_guess->assign_guess(*source->manager_guess);
        finishInit_guess();
        operateMode_guess = source->operateMode_guess;
    } else {
        uninitParticle();
    }
}

namespace {
    struct Matrix_guess {
        double a, b, c, d, tx, ty;
    };
    tjs_int minCorner_guess(const double *values) {
        if(values[0] < values[1]) {
            if(values[3] > values[0]) return 0;
            return values[3] < values[2] ? 3 : 2;
        }
        if(values[2] > values[1]) return 1;
        return values[2] < values[3] ? 2 : 3;
    }
}

void layerExParticle::updateParticle(tjs_int delta) {
    manager_guess->update(delta);
    tTJSVariant value;
    auto *global = TVPGetScriptDispatch();
    global->PropGet(0, TJS_W("Layer"), nullptr, &value, global);
    auto *original = value.AsObjectNoAddRef();
    tTJSVariant values[14];
    tTJSVariant *args[14];
    for(tjs_int i = 0; i < 14; ++i) {
        values[i] = 0;
        args[i] = values + i;
    }
    for(tjs_int i = 0; i < dirtyCount_guess; ++i) {
        const auto &rect = dirtyRects_guess[i];
        values[0] = rect.x;
        values[1] = rect.y;
        values[2] = rect.width;
        values[3] = rect.height;
        values[4] = 0;
        original->FuncCall(0, TJS_W("fillRect"), nullptr, &value, 5, args, owner_guess);
    }
    dirtyCount_guess = 0;
    double anchorX, anchorY;
    if(TJS_SUCCEEDED(owner_guess->PropGet(0, TJS_W("afx"), nullptr, &value, owner_guess)))
        anchorX = value.AsReal();
    else {
        original->PropGet(0, TJS_W("imageWidth"), nullptr, &value, owner_guess);
        anchorX = value.AsReal() * 0.5;
    }
    if(TJS_SUCCEEDED(owner_guess->PropGet(0, TJS_W("afy"), nullptr, &value, owner_guess)))
        anchorY = value.AsReal();
    else {
        original->PropGet(0, TJS_W("imageHeight"), nullptr, &value, owner_guess);
        anchorY = value.AsReal() * 0.5;
    }
    double left = 0, top = 0, zoomX = 1, zoomY = 1, rotation = 0;
    if(TJS_SUCCEEDED(owner_guess->PropGet(0, TJS_W("left"), nullptr, &value, owner_guess)))
        left = value.AsReal();
    if(TJS_SUCCEEDED(owner_guess->PropGet(0, TJS_W("top"), nullptr, &value, owner_guess)))
        top = value.AsReal();
    if(TJS_SUCCEEDED(owner_guess->PropGet(0, TJS_W("zoomx"), nullptr, &value, owner_guess)))
        zoomX = value.AsReal() / 100.0;
    if(TJS_SUCCEEDED(owner_guess->PropGet(0, TJS_W("zoomy"), nullptr, &value, owner_guess)))
        zoomY = value.AsReal() / 100.0;
    if(TJS_SUCCEEDED(owner_guess->PropGet(0, TJS_W("rotate"), nullptr, &value, owner_guess)))
        rotation = value.AsReal() * 3.141592653589793 / 180.0;
    const double cosine = std::cos(rotation), sine = std::sin(rotation);
    const Matrix_guess scaled{zoomX, zoomY * 0.0, 0.0 * zoomX, zoomY,
                              zoomX * (0.0 - anchorX), (0.0 - anchorY) * zoomY};
    const Matrix_guess owner{
        scaled.a * cosine + scaled.b * sine,
        scaled.b * cosine - scaled.a * sine,
        scaled.d * sine + scaled.c * cosine,
        scaled.d * cosine - scaled.c * sine,
        scaled.ty * sine + scaled.tx * cosine + left + anchorX,
        cosine * scaled.ty - sine * scaled.tx + top + anchorY};
    values[0] = image_guess;
    values[5] = false;
    values[12] = operateMode_guess;
    for(tjs_int i = manager_guess->capacity_guess - 1; i >= 0; --i) {
        const Particle *particle = manager_guess->particles_guess[i];
        if(!particle || particle->lifetime < 0)
            continue;
        const auto &rect = imageRects_guess[particle->imageIndex];
        const double cosAngle = std::cos(particle->angle), sinAngle = std::sin(particle->angle);
        const double scale = particle->magnify;
        const double zeroY = scale * 0.0, zeroX = 0.0 * scale;
        const Matrix_guess local{
            scale * cosAngle + zeroY * sinAngle,
            zeroY * cosAngle - scale * sinAngle,
            scale * sinAngle + zeroX * cosAngle,
            scale * cosAngle - zeroX * sinAngle,
            particle->x + zeroY * sinAngle + zeroX * cosAngle,
            particle->y + (cosAngle * zeroY - sinAngle * zeroX)};
        const double negativeX = -rect.halfWidth, negativeY = -rect.halfHeight;
        double xs[4] = {
            negativeX * local.a + local.c * negativeY + local.tx,
            rect.halfWidth * local.a + local.c * negativeY + local.tx,
            rect.halfWidth * local.a + local.c * rect.halfHeight + local.tx,
            local.tx + local.c * rect.halfHeight + local.a * negativeX};
        double ys[4] = {
            negativeY * local.d + local.b * negativeX + local.ty,
            negativeY * local.d + local.b * rect.halfWidth + local.ty,
            rect.halfHeight * local.d + local.b * rect.halfWidth + local.ty,
            negativeX * local.b + rect.halfHeight * local.d + local.ty};
        const double firstX = xs[0], secondX = xs[1], thirdX = xs[2], fourthX = xs[3];
        xs[0] = owner.c * ys[0] + xs[0] * owner.a + owner.tx;
        ys[0] = firstX * owner.b + ys[0] * owner.d + owner.ty;
        xs[1] = owner.c * ys[1] + xs[1] * owner.a + owner.tx;
        ys[1] = ys[1] * owner.d + owner.b * secondX + owner.ty;
        xs[2] = owner.c * ys[2] + xs[2] * owner.a + owner.tx;
        ys[2] = ys[2] * owner.d + owner.b * thirdX + owner.ty;
        xs[3] = owner.tx + owner.c * ys[3] + xs[3] * owner.a;
        ys[3] = ys[3] * owner.d + fourthX * owner.b + owner.ty;
        values[1] = rect.x;
        values[2] = rect.y;
        values[3] = rect.width;
        values[4] = rect.height;
        values[6] = xs[0] - 0.5;
        values[7] = ys[0] - 0.5;
        values[8] = xs[1] - 0.5;
        values[9] = ys[1] - 0.5;
        values[10] = xs[3] - 0.5;
        values[11] = ys[3] - 0.5;
        values[13] = particle->opacity;
        original->FuncCall(0, TJS_W("operateAffine"), nullptr, &value, 14, args, owner_guess);
        const tjs_int minX = minCorner_guess(xs), minY = minCorner_guess(ys);
        auto &dirty = dirtyRects_guess[dirtyCount_guess++];
        dirty.x = xs[minX];
        dirty.y = ys[minY];
        dirty.width = xs[minX ^ 2] - xs[minX] + 2.0;
        dirty.height = ys[minY ^ 2] - ys[minY] + 2.0;
    }
}

static void initializeRandom_guess() {
    seedRandom_guess(static_cast<tjs_uint32>(TVPGetTickCount()));
}

} // namespace layerexparticle

using layerexparticle::layerExParticle;
NCB_GET_INSTANCE_HOOK(layerExParticle) {
    NCB_INSTANCE_GETTER(objthis) {
        ClassT *object = GetNativeInstance(objthis);
        if(!object) {
            object = new ClassT(objthis);
            SetNativeInstance(objthis, object);
        }
        object->reset();
        return object;
    }
};
using layerexparticle::initializeRandom_guess;
NCB_PRE_REGIST_CALLBACK(initializeRandom_guess);
NCB_ATTACH_CLASS_WITH_HOOK(layerExParticle, Layer) {
    NCB_METHOD(initVectorParticle);
    NCB_METHOD(initRotateParticle);
    NCB_METHOD(initAccelRotateParticle);
    NCB_METHOD(initBlinkParticle);
    NCB_METHOD(uninitParticle);
    NCB_METHOD(updateParticle);
    NCB_METHOD(assignParticle);
    Variant(TJS_W("ptVector"), 1);
    Variant(TJS_W("ptRotate"), 2);
    Variant(TJS_W("ptBlink"), 3);
    Variant(TJS_W("ptAccelRotate"), 4);
    NCB_PROPERTY_RO(initializedParticle, getInitializedParticle);
    NCB_PROPERTY(particleImage, getParticleImage, setParticleImage);
    NCB_PROPERTY(operateMode, getOperateMode, setOperateMode);
    NCB_PROPERTY_RO(particleStartCount, getParticleStartCount);
    NCB_PROPERTY(particleMaxCount, getParticleMaxCount, setParticleMaxCount);
    NCB_PROPERTY(particleGenerateRate, getParticleGenerateRate, setParticleGenerateRate);
    NCB_PROPERTY_RO(particleCount, getParticleCount);
    NCB_PROPERTY_RO(particleType, getParticleType);
    NCB_METHOD(setParticleAngle);
    NCB_METHOD(setParticleAngleOmega);
    NCB_PROPERTY(maxParticleAngle, getMaxParticleAngle, setMaxParticleAngle);
    NCB_PROPERTY(minParticleAngle, getMinParticleAngle, setMinParticleAngle);
    NCB_PROPERTY(maxParticleAngleOmega, getMaxParticleAngleOmega, setMaxParticleAngleOmega);
    NCB_PROPERTY(minParticleAngleOmega, getMinParticleAngleOmega, setMinParticleAngleOmega);
    NCB_PROPERTY(rotateReverse, getRotateReverse, setRotateReverse);
    NCB_METHOD(setParticleMagnify);
    NCB_PROPERTY(maxParticleMagnify, getMaxParticleMagnify, setMaxParticleMagnify);
    NCB_PROPERTY(minParticleMagnify, getMinParticleMagnify, setMinParticleMagnify);
    NCB_METHOD(setParticleMagnifyVaridation);
    NCB_PROPERTY(maxParticleMagnifyVaridation, getMaxParticleMagnifyVaridation, setMaxParticleMagnifyVaridation);
    NCB_PROPERTY(minParticleMagnifyVaridation, getMinParticleMagnifyVaridation, setMinParticleMagnifyVaridation);
    NCB_METHOD(setParticleOpacity);
    NCB_PROPERTY(maxParticleOpacity, getMaxParticleOpacity, setMaxParticleOpacity);
    NCB_PROPERTY(minParticleOpacity, getMinParticleOpacity, setMinParticleOpacity);
    NCB_METHOD(setParticleOpacityVaridation);
    NCB_PROPERTY(maxParticleOpacityVaridation, getMaxParticleOpacityVaridation, setMaxParticleOpacityVaridation);
    NCB_PROPERTY(minParticleOpacityVaridation, getMinParticleOpacityVaridation, setMinParticleOpacityVaridation);
    NCB_METHOD(setParticleAppearArea);
    NCB_PROPERTY(particleAppearLeft, getParticleAppearLeft, setParticleAppearLeft);
    NCB_PROPERTY(particleAppearTop, getParticleAppearTop, setParticleAppearTop);
    NCB_PROPERTY(particleAppearWidth, getParticleAppearWidth, setParticleAppearWidth);
    NCB_PROPERTY(particleAppearHeight, getParticleAppearHeight, setParticleAppearHeight);
    NCB_METHOD(setParticleSpeed);
    NCB_PROPERTY(maxParticleSpeed, getMaxParticleSpeed, setMaxParticleSpeed);
    NCB_PROPERTY(minParticleSpeed, getMinParticleSpeed, setMinParticleSpeed);
    NCB_METHOD(setParticleAccel);
    NCB_PROPERTY(maxParticleAccel, getMaxParticleAccel, setMaxParticleAccel);
    NCB_PROPERTY(minParticleAccel, getMinParticleAccel, setMinParticleAccel);
    NCB_METHOD(setParticleVectorAngle);
    NCB_PROPERTY(maxParticleVectorAngle, getMaxParticleVectorAngle, setMaxParticleVectorAngle);
    NCB_PROPERTY(minParticleVectorAngle, getMinParticleVectorAngle, setMinParticleVectorAngle);
    NCB_METHOD(setParticleVectorRotate);
    NCB_PROPERTY(maxParticleVectorRotate, getMaxParticleVectorRotate, setMaxParticleVectorRotate);
    NCB_PROPERTY(minParticleVectorRotate, getMinParticleVectorRotate, setMinParticleVectorRotate);
    NCB_METHOD(setParticleRotateCenter);
    NCB_PROPERTY(particleRotateCenterX, getParticleRotateCenterX, setParticleRotateCenterX);
    NCB_PROPERTY(particleRotateCenterY, getParticleRotateCenterY, setParticleRotateCenterY);
    NCB_METHOD(setParticleRotateRadius);
    NCB_PROPERTY(maxParticleRotateRadius, getMaxParticleRotateRadius, setMaxParticleRotateRadius);
    NCB_PROPERTY(minParticleRotateRadius, getMinParticleRotateRadius, setMinParticleRotateRadius);
    NCB_METHOD(setParticleRotateRadiusVaridation);
    NCB_PROPERTY(maxParticleRotateRadiusVaridation, getMaxParticleRotateRadiusVaridation, setMaxParticleRotateRadiusVaridation);
    NCB_PROPERTY(minParticleRotateRadiusVaridation, getMinParticleRotateRadiusVaridation, setMinParticleRotateRadiusVaridation);
    NCB_METHOD(setParticleRotateOmega);
    NCB_PROPERTY(maxParticleRotateOmega, getMaxParticleRotateOmega, setMaxParticleRotateOmega);
    NCB_PROPERTY(minParticleRotateOmega, getMinParticleRotateOmega, setMinParticleRotateOmega);
    NCB_PROPERTY(particleRotateReverse, getParticleRotateReverse, setParticleRotateReverse);
    NCB_METHOD(setParticleRotateOmegaMax);
    NCB_PROPERTY(maxParticleRotateOmegaMax, getMaxParticleRotateOmegaMax, setMaxParticleRotateOmegaMax);
    NCB_PROPERTY(minParticleRotateOmegaMax, getMinParticleRotateOmegaMax, setMinParticleRotateOmegaMax);
    NCB_METHOD(setParticleRotateOmegaTime);
    NCB_PROPERTY(maxParticleRotateOmegaTime, getMaxParticleRotateOmegaTime, setMaxParticleRotateOmegaTime);
    NCB_PROPERTY(minParticleRotateOmegaTime, getMinParticleRotateOmegaTime, setMinParticleRotateOmegaTime);
    NCB_METHOD(setParticleBlinkMax);
    NCB_PROPERTY(maxParticleBlinkMax, getMaxParticleBlinkMax, setMaxParticleBlinkMax);
    NCB_PROPERTY(minParticleBlinkMax, getMinParticleBlinkMax, setMinParticleBlinkMax);
    NCB_METHOD(setParticleBlinkTime);
    NCB_PROPERTY(maxParticleBlinkTime, getMaxParticleBlinkTime, setMaxParticleBlinkTime);
    NCB_PROPERTY(minParticleBlinkTime, getMinParticleBlinkTime, setMinParticleBlinkTime);
    NCB_METHOD(setParticleBlinkCount);
    NCB_PROPERTY(maxParticleBlinkCount, getMaxParticleBlinkCount, setMaxParticleBlinkCount);
    NCB_PROPERTY(minParticleBlinkCount, getMinParticleBlinkCount, setMinParticleBlinkCount);
    NCB_PROPERTY_WO(particleInitializer, setParticleInitializer);
}
