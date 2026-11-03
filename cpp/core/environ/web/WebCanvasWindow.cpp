#include "WebCanvasInternal.h"
#include "EventIntf.h"
#include "LayerImpl.h"
#include "LayerBitmapIntf.h"
#include "RenderManager.h"
#include "WindowImpl.h"
#include "TickCount.h"
#include <algorithm>

namespace webcanvas {
namespace {

tjs_int32 attachmentID = -1;
tTVPUniqueTagForInputEvent frameTag;

class WindowCanvas;
class FrameEvent : public tTVPBaseInputEvent {
    WindowCanvas *attachment;
    tTJSVariant owner;
public:
    FrameEvent(WindowCanvas *attachment, iTJSDispatch2 *owner) :
        tTVPBaseInputEvent(attachment, frameTag), attachment(attachment),
        owner(owner, owner) {}
    void Deliver() const override;
};

class WindowCanvas : public tTJSNativeInstance,
                     public tTVPContinuousEventCallbackIntf {
    iTJSDispatch2 *owner; // borrowed; this is attached to that object's NI slots
    tTJSVariant canvas, layer;
    std::vector<Canvas *> observers;
    std::unique_ptr<tTVPBaseBitmap> image;
    bool hooked = false, drawing = false;
    int fps = 0;
    tjs_uint64 next = 0;
public:
    explicit WindowCanvas(iTJSDispatch2 *owner) : owner(owner) {}
    ~WindowCanvas() override { Invalidate(); }
    void Invalidate() override {
        if(hooked) TVPRemoveContinuousEventHook(this);
        hooked = false;
        TVPCancelInputEvents(this);
        owner = nullptr;
        for(auto *c : observers) c->window = nullptr;
        observers.clear();
        // A running onDraw owns local references until end() restores GL state.
        canvas.Clear(); layer.Clear(); image.reset();
    }
    tTJSVariant getCanvas() {
        if(!object(canvas)) {
            if(!owner) fail("Window has been invalidated");
            tTJSVariant window(owner, owner);
            tTJSVariant *params[] = { &window };
            canvas = makeObject(TJS_W("Canvas"), 1, params);
        }
        return canvas;
    }
    void watch(Canvas *c) {
        if(std::find(observers.begin(), observers.end(), c) == observers.end())
            observers.push_back(c);
    }
    void unwatch(Canvas *c) {
        observers.erase(std::remove(observers.begin(), observers.end(), c), observers.end());
    }
    int cycle() const { return fps; }
    void cycle(int value) {
        // The native setter starts/changes a positive cycle. Zero and negative
        // values are no-ops, including when a cycle already exists.
        if(value <= 0) return;
        getCanvas();
        fps = value;
        next = 0;
        if(!hooked) { TVPAddContinuousEventHook(this); hooked = true; }
    }
    void request() {
        if(owner)
            TVPPostInputEvent(new FrameEvent(this, owner), TVP_EPT_REMOVE_POST);
    }
    void OnContinuousCallback(tjs_uint64 tick) override {
        if(!owner || !fps) return;
        if(!next || tick >= next) {
            request();
            next = tick + std::max<tjs_uint64>(1, 1000 / fps);
        }
    }
    void draw() {
        auto *window = native<tTJSNI_Window>(owner, tTJSNC_Window::ClassID);
        if(drawing || !window || !window->CanDeliverEvents()) return;
        tTJSVariant keepOwner(owner, owner), keepCanvas = getCanvas();
        auto *c = native<Canvas>(keepCanvas, CanvasID);
        drawing = true;
        struct Finish {
            Canvas *canvas;
            bool &drawing;
            ~Finish() { canvas->end(); drawing = false; }
        } finish { c, drawing };
        c->begin();
        ttstr event(TJS_W("onDraw"));
        TVPPostEvent(owner, owner, event, 0, TVP_EPT_IMMEDIATE, 0, nullptr);
        if(!owner || !window->GetForm()) return;
        // Software Layer composition remains the window presentation path.
        // Read GL RGBA bottom-up once, convert to the engine's BGRA top-down,
        // then publish through AssignTexture and its normal update handling.
        GLState readState;
        int w = c->display.width, h = c->display.height;
        if(w <= 0 || h <= 0) return;
        if(!image || image->GetWidth() != (unsigned)w || image->GetHeight() != (unsigned)h)
            image = std::make_unique<tTVPBaseBitmap>(w, h, 32);
        c->display.readBitmap(*image, 0, 0, tTVPRect(0, 0, w, h));
        if(!object(layer)) {
            tTJSVariant window(owner, owner), null((iTJSDispatch2 *)nullptr);
            tTJSVariant *params[] = { &window, &null };
            layer = makeObject(TJS_W("Layer"), 2, params);
        }
        auto *presenter = native<tTJSNI_Layer>(layer, tTJSNC_Layer::ClassID);
        if(!presenter) fail("cannot create Canvas presentation Layer");
        auto *layerObject = object(layer);
        tTJSVariant width(w), height(h), visible((tjs_int)1);
        layerObject->PropSet(0, TJS_W("width"), nullptr, &width, layerObject);
        layerObject->PropSet(0, TJS_W("height"), nullptr, &height, layerObject);
        layerObject->PropSet(0, TJS_W("visible"), nullptr, &visible, layerObject);
        presenter->AssignTexture(image->GetTexture());
    }
};
void FrameEvent::Deliver() const { attachment->draw(); }

WindowCanvas *attachment(iTJSDispatch2 *window) {
    if(!native<tTJSNI_Window>(window, tTJSNC_Window::ClassID)) return nullptr;
    auto *existing = native<WindowCanvas>(window, attachmentID);
    if(existing) return existing;
    auto *created = new WindowCanvas(window);
    iTJSNativeInstance *ni = created;
    if(TJS_FAILED(window->NativeInstanceSupport(TJS_NIS_REGISTER, attachmentID, &ni))) {
        delete created;
        fail("Window has no free native instance slot for Canvas");
    }
    return created;
}
tjs_error getCanvas(tTJSVariant *result, iTJSDispatch2 *window) {
    auto *a = attachment(window);
    if(!a) return TJS_E_NATIVECLASSCRASH;
    if(result) *result = a->getCanvas();
    return TJS_S_OK;
}
tjs_error getCycle(tTJSVariant *result, iTJSDispatch2 *window) {
    auto *a = attachment(window);
    if(!a) return TJS_E_NATIVECLASSCRASH;
    if(result) *result = a->cycle();
    return TJS_S_OK;
}
tjs_error setCycle(const tTJSVariant *value, iTJSDispatch2 *window) {
    auto *a = attachment(window);
    if(!a) return TJS_E_NATIVECLASSCRASH;
    a->cycle((tjs_int)*value);
    return TJS_S_OK;
}
tjs_error readonly(const tTJSVariant *, iTJSDispatch2 *) { return TJS_E_ACCESSDENYED; }
tjs_error fireOnDraw(tTJSVariant *, tjs_int, tTJSVariant **, iTJSDispatch2 *window) {
    auto *a = attachment(window);
    if(!a) return TJS_E_NATIVECLASSCRASH;
    a->request();
    return TJS_S_OK;
}
tjs_error onDraw(tTJSVariant *, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
    return TJS_S_OK; // base event handler; scripts override it to draw
}
} // namespace

void watchWindow(iTJSDispatch2 *window, Canvas *canvas) {
    if(auto *a = attachment(window)) a->watch(canvas);
}
void unwatchWindow(iTJSDispatch2 *window, Canvas *canvas) {
    if(auto *a = native<WindowCanvas>(window, attachmentID)) a->unwatch(canvas);
}

void registerWindow(tTJSNativeClass *windowClass) {
    attachmentID = TJSRegisterNativeClass(TJS_W("WebCanvasWindow"));
    windowClass->RegisterNCM(TJS_W("canvas"),
        TJSCreateNativeClassProperty(getCanvas, readonly), TJS_W("Window"), nitProperty);
    windowClass->RegisterNCM(TJS_W("drawCycle"),
        TJSCreateNativeClassProperty(getCycle, setCycle), TJS_W("Window"), nitProperty);
    windowClass->RegisterNCM(TJS_W("fireOnDraw"),
        TJSCreateNativeClassMethod(fireOnDraw), TJS_W("Window"), nitMethod);
    windowClass->RegisterNCM(TJS_W("onDraw"),
        TJSCreateNativeClassMethod(onDraw), TJS_W("Window"), nitMethod);
}
} // namespace webcanvas
