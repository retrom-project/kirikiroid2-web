// Web-only gamepad.dll contract. The mobile references have no such module;
// the two native classes and counter flow follow the supplied Windows plugin.
#define NCB_MODULE_NAME TJS_W("gamepad.dll")
#include "ncbind.hpp"
#include "ScriptMgnIntf.h"
#include "EventIntf.h"
#include "DebugIntf.h"
#include <emscripten.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <list>
#include <iterator>
#include <set>
#include <cstdint>

// Implemented by WindowEx's Web platform message bridge. This runs only from
// the TJS continuous-event thread, never from a browser event listener.
void TVPDeliverBrowserGamepadDeviceChange();

namespace {
tjs_int32 PortClassID = -1;
tjs_int32 GamepadClassID = -1;

int RefreshBrowserDevices(bool observeTopology = false) {
    return MAIN_THREAD_EM_ASM_INT({
        var source;
        try {
            source = typeof Module.retromGamepadDevices === 'function'
                ? Module.retromGamepadDevices()
                : Array.from(navigator.getGamepads ? navigator.getGamepads() : []);
        } catch (_) { source = []; }
        var devices = source.filter(function(p) {
            return p && p.connected && p.mapping === 'standard' &&
                Number.isSafeInteger(p.index) && p.index >= 0 && p.index <= 255;
        }).slice(0, 16).map(function(p) {
            return {index:p.index, id:String(p.id).slice(0,512)};
        });
        var signature = JSON.stringify(devices);
        var changed = !!$0 && Module.krkr2GamepadDeviceSignature !== undefined &&
            Module.krkr2GamepadDeviceSignature !== signature;
        if($0 || Module.krkr2GamepadDeviceSignature === undefined)
            Module.krkr2GamepadDeviceSignature = signature;
        Module.krkr2GamepadDevices = devices;
        return changed ? 1 : 0;
    }, observeTopology);
}

class BrowserDeviceMonitor : public tTVPContinuousEventCallbackIntf {
    unsigned Managers = 0;
public:
    void AddManager() {
        if(Managers++ == 0)
            TVPAddContinuousEventHook(this);
    }
    void RemoveManager() {
        if(--Managers == 0)
            TVPRemoveContinuousEventHook(this);
    }
    void OnContinuousCallback(tjs_uint64) override {
        if(RefreshBrowserDevices(true))
            TVPDeliverBrowserGamepadDeviceChange();
    }
};
BrowserDeviceMonitor DeviceMonitor;

struct DeviceState {
    double LeftX = 0, LeftY = 0, RightX = 0, RightY = 0;
    double LeftTrigger = 0, RightTrigger = 0;
    tjs_int Keys = 0;
};

double NormalizeAxis(double value, int deadzone, bool invert) {
    if(!std::isfinite(value))
        value = 0;
    value = std::max(-1.0, std::min(1.0, invert ? -value : value));
    const int raw = static_cast<int>(value * (value < 0 ? 32768 : 32767));
    if(raw >= -deadzone && raw <= deadzone)
        return 0;
    return raw < 0 ? static_cast<double>(raw + deadzone) / (32768 - deadzone)
                   : static_cast<double>(raw - deadzone) / (32767 - deadzone);
}

class GamepadInstance;
class BrowserInputDevice {
    std::array<std::uint16_t, 2> Vibration{};
    std::array<std::uint16_t, 2> SubmittedVibration{};
    double NextVibrationRefresh = 0;
public:
    int Index;
    ttstr Name;
    DeviceState State;
    std::list<GamepadInstance *> Borrowers;

    BrowserInputDevice(int index, const ttstr &name) : Index(index), Name(name) {}
    ~BrowserInputDevice();

    void SetVibration(unsigned motor, double value) {
        value = value >= 0 ? std::min(1.0, value) : 0.0;
        Vibration[motor] = static_cast<std::uint16_t>(value * 65535);
    }
    void FlushVibration(bool force = false) {
        const double now = emscripten_get_now();
        const bool active = Vibration[0] != 0 || Vibration[1] != 0;
        if(!force && Vibration == SubmittedVibration && (!active || now < NextVibrationRefresh))
            return;
        const auto name = Name.AsStdString();
        MAIN_THREAD_EM_ASM({
            try {
                var name = UTF8ToString($1);
                if(typeof Module.retromGamepadVibrate === 'function') {
                    Module.retromGamepadVibrate($0, name, $2, $3);
                    return;
                }
                var p = Array.from(navigator.getGamepads ? navigator.getGamepads() : [])
                    .find(function(p) {return p && p.connected && p.index === $0 &&
                        String(p.id).slice(0,512) === name;});
                var actuator = p && p.vibrationActuator;
                if(!actuator || typeof actuator.playEffect !== 'function') return;
                var effect = $2 || $3 || typeof actuator.reset !== 'function'
                    ? actuator.playEffect('dual-rumble', {duration:250, startDelay:0,
                        strongMagnitude:$2, weakMagnitude:$3}) : actuator.reset();
                Promise.resolve(effect).catch(function() {});
            } catch(_) {}
        }, Index, name.c_str(), Vibration[0] / 65535.0, Vibration[1] / 65535.0);
        SubmittedVibration = Vibration;
        NextVibrationRefresh = now + 150;
    }
    void Update() {
        FlushVibration();
        std::array<double, 7> sample{};
        const auto name = Name.AsStdString();
        MAIN_THREAD_EM_ASM({
            var p;
            try {
                var pads = typeof Module.retromGamepadSamples === 'function'
                    ? Module.retromGamepadSamples()
                    : Array.from(navigator.getGamepads ? navigator.getGamepads() : []);
                p = pads
                    .find(function(p) {return p && p.index === $0 && p.connected &&
                        p.mapping === 'standard' && String(p.id).slice(0,512) === UTF8ToString($1);});
            } catch (_) {}
            if(!p) return;
            var out = $2 >> 3;
            for(var i=0;i<4;i++) HEAPF64[out+i] = Number(p.axes[i] || 0);
            for(var i=0;i<2;i++) {
                var trigger = p.buttons[6+i];
                HEAPF64[out+4+i] = trigger ? Number(trigger.value) : 0;
            }
            var indices = ([12,13,14,15,9,8,10,11,4,5,0,1,2,3]);
            var masks = ([1,2,4,8,16,32,64,128,256,512,4096,8192,16384,32768]);
            var keys = 0;
            for(var i=0;i<indices.length;i++) {
                var button = p.buttons[indices[i]];
                if(button && (button.pressed || button.value >= 0.5)) keys |= masks[i];
            }
            HEAPF64[out+6] = keys;
        }, Index, name.c_str(), sample.data());
        State.LeftX = NormalizeAxis(sample[0], 7849, false);
        State.LeftY = NormalizeAxis(sample[1], 7849, true);
        State.RightX = NormalizeAxis(sample[2], 8689, false);
        State.RightY = NormalizeAxis(sample[3], 8689, true);
        auto trigger = [](double value) {
            if(!std::isfinite(value)) return 0.0;
            return static_cast<int>(std::max(0.0, std::min(1.0, value)) * 255) / 255.0;
        };
        State.LeftTrigger = trigger(sample[4]);
        State.RightTrigger = trigger(sample[5]);
        State.Keys = static_cast<tjs_int>(sample[6]);
    }
};

std::set<BrowserInputDevice *> LiveDevices;
unsigned ActiveControllers = 0;
void SetInputOwnership(bool acquire) {
    if(acquire) ++ActiveControllers;
    else --ActiveControllers;
    const bool active = ActiveControllers != 0;
    MAIN_THREAD_EM_ASM({
        if(typeof Module.onGamepadInputChanged === 'function')
            Module.onGamepadInputChanged(!!$0);
    }, active);
}

class DeviceManager {
    std::list<BrowserInputDevice *> Devices;
public:
    DeviceManager() { DeviceMonitor.AddManager(); }
    ~DeviceManager() { Clear(); DeviceMonitor.RemoveManager(); }
    void Clear() {
        for(auto *device : Devices) {
            LiveDevices.erase(device);
            delete device;
        }
        Devices.clear();
    }
    void Initialize() {
        Clear();
        RefreshBrowserDevices();
        const int count = MAIN_THREAD_EM_ASM_INT({return Module.krkr2GamepadDevices.length;});
        for(int i = 0; i < count; ++i) {
            const int index = MAIN_THREAD_EM_ASM_INT({return Module.krkr2GamepadDevices[$0].index;}, i);
            char *name = reinterpret_cast<char *>(MAIN_THREAD_EM_ASM_PTR({
                var value = Module.krkr2GamepadDevices[$0].id;
                var size = lengthBytesUTF8(value) + 1;
                var pointer = _malloc(size);
                stringToUTF8(value, pointer, size);
                return pointer;
            }, i));
            ttstr label(name);
            std::free(name);
            auto *device = new BrowserInputDevice(index, label);
            Devices.push_back(device);
            LiveDevices.insert(device);
        }
    }
    tjs_int Count() const { return static_cast<tjs_int>(Devices.size()); }
    BrowserInputDevice *GetController(tjs_uint index) const {
        if(index >= Devices.size()) return nullptr;
        auto it = Devices.begin();
        std::advance(it, index);
        return *it;
    }
};

class PortInstance : public tTJSNativeInstance {
public:
    DeviceManager Manager;
};

class GamepadInstance : public tTJSNativeInstance {
public:
    BrowserInputDevice *Device = nullptr;
    DeviceState Previous;
    std::array<tjs_int, 24> Counts{};
    ~GamepadInstance() override { Detach(); }
    tjs_error Construct(tjs_int, tTJSVariant **, iTJSDispatch2 *) override {
        return TJS_S_OK;
    }
    void Attach(BrowserInputDevice *device) {
        Detach();
        Device = device;
        if(Device) {
            Device->Borrowers.push_back(this);
            SetInputOwnership(true);
        }
    }
    void Detach() {
        if(Device) {
            Device->Borrowers.remove(this);
            Device = nullptr;
            SetInputOwnership(false);
        }
    }
    void Invalidate() override { Detach(); }
    static void Advance(tjs_int &count, bool pressed) {
        if(pressed) {
            if(count <= 0) count = 1;
            else if(count != std::numeric_limits<tjs_int>::max()) ++count;
        } else {
            if(count > 0) count = 0;
            else if(count != std::numeric_limits<tjs_int>::min()) --count;
        }
    }
    static void Axis(double value, tjs_int &negative, tjs_int &positive) {
        Advance(negative, value < 0);
        Advance(positive, value > 0);
    }
    void Update() {
        if(!Device) return;
        Device->Update();
        const DeviceState sample = Device->State;
        Axis(sample.LeftX, Counts[2], Counts[3]);
        Axis(sample.LeftY, Counts[1], Counts[0]);
        Axis(sample.RightX, Counts[6], Counts[7]);
        Axis(sample.RightY, Counts[5], Counts[4]);
        Advance(Counts[17], sample.LeftTrigger != 0);
        Advance(Counts[19], sample.RightTrigger != 0);
        static const tjs_int masks[] = {1,2,4,8,16,32,64,128,256,512,4096,8192,16384,32768};
        static const unsigned counters[] = {8,9,10,11,12,13,14,15,16,18,20,21,22,23};
        for(unsigned i = 0; i < std::size(masks); ++i)
            Advance(Counts[counters[i]], (sample.Keys & masks[i]) != 0);
        Previous = sample;
    }
};

BrowserInputDevice::~BrowserInputDevice() {
    Vibration.fill(0);
    FlushVibration();
    // Browser handles may disappear while script objects are still retained.
    // Clear borrowed bindings before reclaiming this platform-owned handle.
    while(!Borrowers.empty()) {
        auto *borrower = Borrowers.front();
        borrower->Detach();
        borrower->Previous = DeviceState{};
        borrower->Counts.fill(0);
    }
}

template<class Instance, tjs_int32 *ClassID>
Instance *GetNative(iTJSDispatch2 *object) {
    if(!object) return nullptr;
    iTJSNativeInstance *instance = nullptr;
    if(TJS_FAILED(object->NativeInstanceSupport(TJS_NIS_GETINSTANCE, *ClassID, &instance)))
        return nullptr;
    return static_cast<Instance *>(instance);
}

PortInstance *GetPort(iTJSDispatch2 *object) { return GetNative<PortInstance, &PortClassID>(object); }
GamepadInstance *GetGamepad(iTJSDispatch2 *object) { return GetNative<GamepadInstance, &GamepadClassID>(object); }
iTJSNativeInstance *CreatePort() { return new PortInstance(); }
iTJSNativeInstance *CreateGamepad() { return new GamepadInstance(); }

tjs_error Finalize(tTJSVariant *, tjs_int, tTJSVariant **, iTJSDispatch2 *) { return TJS_S_OK; }
tjs_error PortConstructor(tTJSVariant *, tjs_int n, tTJSVariant **p, iTJSDispatch2 *object) {
    auto *instance = GetPort(object);
    return instance ? instance->Construct(n, p, object) : TJS_E_NATIVECLASSCRASH;
}
tjs_error GamepadConstructor(tTJSVariant *, tjs_int n, tTJSVariant **p, iTJSDispatch2 *object) {
    auto *instance = GetGamepad(object);
    if(!instance) return TJS_E_NATIVECLASSCRASH;
    const auto status = instance->Construct(n, p, object);
    if(TJS_FAILED(status)) return status;
    if(n < 1) return TJS_E_BADPARAMCOUNT;
    auto *device = reinterpret_cast<BrowserInputDevice *>(static_cast<intptr_t>(p[0]->AsInteger()));
    if(LiveDevices.find(device) == LiveDevices.end()) return TJS_E_INVALIDPARAM;
    instance->Attach(device);
    return TJS_S_OK;
}
tjs_error Initialize(tTJSVariant *, tjs_int n, tTJSVariant **p, iTJSDispatch2 *object) {
    auto *instance = GetPort(object);
    if(!instance) return TJS_E_NATIVECLASSCRASH;
    if(n < 1) return TJS_E_BADPARAMCOUNT;
    (void)p[0]->AsInteger(); // Coerce the native HWND argument; browsers have no HWND.
    instance->Manager.Initialize();
    return TJS_S_OK;
}
tjs_error GetController(tTJSVariant *result, tjs_int n, tTJSVariant **p, iTJSDispatch2 *object) {
    auto *instance = GetPort(object);
    if(!instance) return TJS_E_NATIVECLASSCRASH;
    if(n < 1) return TJS_E_BADPARAMCOUNT;
    if(!result) return TJS_E_INVALIDPARAM;
    auto *device = instance->Manager.GetController(static_cast<tjs_uint>(p[0]->AsInteger()));
    if(!device) return TJS_E_INVALIDPARAM;
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    tTJSVariant value;
    auto status = global->PropGet(0, TJS_W("Gamepad"), nullptr, &value, global);
    global->Release();
    if(TJS_FAILED(status)) return TJS_E_INVALIDPARAM;
    const auto closure = value.AsObjectClosureNoAddRef();
    if(!closure.Object) return TJS_E_INVALIDPARAM;
    tTJSVariant pointer(static_cast<tjs_int64>(reinterpret_cast<intptr_t>(device)));
    tTJSVariant *arguments[] = {&pointer};
    iTJSDispatch2 *controller = nullptr;
    status = closure.CreateNew(0, nullptr, nullptr, &controller, 1, arguments, object);
    if(TJS_FAILED(status) || !controller) return TJS_E_INVALIDPARAM;
    *result = tTJSVariant(controller, controller);
    controller->Release();
    return TJS_S_OK;
}
tjs_error Count(tTJSVariant *result, iTJSDispatch2 *object) {
    auto *instance = GetPort(object);
    if(!instance) return TJS_E_NATIVECLASSCRASH;
    if(result) *result = instance->Manager.Count();
    return TJS_S_OK;
}
tjs_error Update(tTJSVariant *, tjs_int, tTJSVariant **, iTJSDispatch2 *object) {
    auto *instance = GetGamepad(object);
    if(!instance) return TJS_E_NATIVECLASSCRASH;
    instance->Update();
    return TJS_S_OK;
}
tjs_error DenyGet(tTJSVariant *, iTJSDispatch2 *) { return TJS_E_ACCESSDENYED; }
tjs_error DenySet(const tTJSVariant *, iTJSDispatch2 *) { return TJS_E_ACCESSDENYED; }
template<unsigned Motor>
tjs_error Vibration(const tTJSVariant *value, iTJSDispatch2 *object) {
    auto *instance = GetGamepad(object);
    if(!instance) return TJS_E_NATIVECLASSCRASH;
    if(instance->Device) instance->Device->SetVibration(Motor, value->AsReal());
    return TJS_S_OK;
}
tjs_error Name(tTJSVariant *result, iTJSDispatch2 *object) {
    auto *instance = GetGamepad(object);
    if(!instance) return TJS_E_NATIVECLASSCRASH;
    if(result && instance->Device) *result = instance->Device->Name;
    return TJS_S_OK;
}
tjs_error Type(tTJSVariant *result, iTJSDispatch2 *object) {
    auto *instance = GetGamepad(object);
    if(!instance) return TJS_E_NATIVECLASSCRASH;
    if(result && instance->Device) *result = static_cast<tjs_int>(1);
    return TJS_S_OK;
}
tjs_error Keys(tTJSVariant *result, iTJSDispatch2 *object) {
    auto *instance = GetGamepad(object);
    if(!instance) return TJS_E_NATIVECLASSCRASH;
    if(result && instance->Device) *result = instance->Device->State.Keys;
    return TJS_S_OK;
}
template<double DeviceState::*Field>
tjs_error AxisValue(tTJSVariant *result, iTJSDispatch2 *object) {
    auto *instance = GetGamepad(object);
    if(!instance) return TJS_E_NATIVECLASSCRASH;
    if(result && instance->Device) *result = instance->Device->State.*Field;
    return TJS_S_OK;
}
template<unsigned Index>
tjs_error Counter(tTJSVariant *result, iTJSDispatch2 *object) {
    auto *instance = GetGamepad(object);
    if(!instance) return TJS_E_NATIVECLASSCRASH;
    if(result) *result = instance->Counts[Index];
    return TJS_S_OK;
}

void Method(tTJSNativeClass *cls, const tjs_char *name, tTJSNativeClassMethodCallback callback) {
    TJSNativeClassRegisterNCM(cls, name, TJSCreateNativeClassMethod(callback), cls->GetClassName().c_str(), nitMethod);
}
void Property(tTJSNativeClass *cls, const tjs_char *name, tTJSNativeClassPropertyGetCallback get,
              tTJSNativeClassPropertySetCallback set = DenySet) {
    TJSNativeClassRegisterNCM(cls, name, TJSCreateNativeClassProperty(get, set), cls->GetClassName().c_str(), nitProperty);
}
void RegisterGamepads() {
    // This is the native plugin's literal public constants contract.
    TVPExecuteExpression(TJS_W("const gpDInput=3,gpFFDInput=2,gpXInput=1,"
        "gpButtonDpadUp=1,gpButtonDpadDown=2,gpButtonDpadLeft=4,gpButtonDpadRight=8,"
        "gpButtonStart=16,gpButtonBack=32,gpButtonLeftThumb=64,gpButtonRightThumb=128,"
        "gpButtonLeftShoulder=256,gpButtonRightShoulder=512,gpButtonA=4096,gpButtonB=8192,"
        "gpButtonX=16384,gpButtonY=32768,gpLeftAxisX=65536,gpLeftAxisY=131072,"
        "gpRightAxisX=262144,gpRightAxisY=524288,gpLeftTrigger=1048576,gpRightTrigger=2097152,"
        "gpDIAxisX=0,gpDIAxisY=1,gpDIAxisZ=2,gpDIAxisRotX=3,gpDIAxisRotY=4,gpDIAxisRotZ=5,"
        "gpDISlider_0=6,gpDISlider_1=7,gpDIPOV_0=8,gpDIPOV_1=9,gpDIPOV_2=10,gpDIPOV_3=11,"
        "gpDIButton1=12,gpDIButton2=13,gpDIButton3=14,gpDIButton4=15,gpDIButton5=16,"
        "gpDIButton6=17,gpDIButton7=18,gpDIButton8=19,gpDIButton9=20,gpDIButton10=21,"
        "gpDIButton11=22,gpDIButton12=23,gpDIButton13=24,gpDIButton14=25,gpDIButton15=26,"
        "gpDIButton16=27,gpDIButton17=28,gpDIButton18=29,gpDIButton19=30,gpDIButton20=31,"
        "gpDIButton21=32,gpDIButton22=33,gpDIButton23=34,gpDIButton24=35,gpDIButton25=36,"
        "gpDIButton26=37,gpDIButton27=38,gpDIButton28=39,gpDIButton29=40,gpDIButton30=41,"
        "gpDIButton31=42,gpDIButton32=43,gpDIDisable=44,gpTouchNo=0,gpTouchDown=1,gpTouchLiftoff=0;"));
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    auto publish = [global](const tjs_char *name, tTJSNativeClass *cls) {
        tTJSVariant value(cls); cls->Release();
        global->PropSet(TJS_MEMBERENSURE, name, nullptr, &value, global);
    };
    auto *port = TJSCreateNativeClassForPlugin(TJS_W("GamepadPort"), CreatePort);
    PortClassID = TJSRegisterNativeClass(TJS_W("GamepadPort"));
    TJSNativeClassSetClassID(port, PortClassID);
    Method(port, TJS_W("finalize"), Finalize);
    TJSNativeClassRegisterNCM(port, TJS_W("GamepadPort"), TJSCreateNativeClassConstructor(PortConstructor), TJS_W("GamepadPort"), nitMethod);
    Method(port, TJS_W("initialize"), Initialize);
    Method(port, TJS_W("getController"), GetController);
    Property(port, TJS_W("count"), Count);
    publish(TJS_W("GamepadPort"), port);

    auto *pad = TJSCreateNativeClassForPlugin(TJS_W("Gamepad"), CreateGamepad);
    GamepadClassID = TJSRegisterNativeClass(TJS_W("Gamepad"));
    TJSNativeClassSetClassID(pad, GamepadClassID);
    Method(pad, TJS_W("finalize"), Finalize);
    TJSNativeClassRegisterNCM(pad, TJS_W("Gamepad"), TJSCreateNativeClassConstructor(GamepadConstructor), TJS_W("Gamepad"), nitMethod);
    Method(pad, TJS_W("update"), Update);
    Property(pad, TJS_W("name"), Name);
    Property(pad, TJS_W("type"), Type);
    Property(pad, TJS_W("leftTrigger"), AxisValue<&DeviceState::LeftTrigger>);
    Property(pad, TJS_W("rightTrigger"), AxisValue<&DeviceState::RightTrigger>);
    Property(pad, TJS_W("leftThumbStickX"), AxisValue<&DeviceState::LeftX>);
    Property(pad, TJS_W("leftThumbStickY"), AxisValue<&DeviceState::LeftY>);
    Property(pad, TJS_W("rightThumbStickX"), AxisValue<&DeviceState::RightX>);
    Property(pad, TJS_W("rightThumbStickY"), AxisValue<&DeviceState::RightY>);
    Property(pad, TJS_W("leftVibration"), DenyGet, Vibration<0>);
    Property(pad, TJS_W("rightVibration"), DenyGet, Vibration<1>);
    Property(pad, TJS_W("keyState"), Keys);
#define REGISTER_COUNTER(Name, Index) Property(pad, TJS_W(#Name), Counter<Index>)
    REGISTER_COUNTER(analogLeftUpCount, 0);
    REGISTER_COUNTER(analogLeftDownCount, 1);
    REGISTER_COUNTER(analogLeftLeftCount, 2);
    REGISTER_COUNTER(analogLeftRightCount, 3);
    REGISTER_COUNTER(analogRightUpCount, 4);
    REGISTER_COUNTER(analogRightDownCount, 5);
    REGISTER_COUNTER(analogRightLeftCount, 6);
    REGISTER_COUNTER(analogRightRightCount, 7);
    REGISTER_COUNTER(degitalUpCount, 8);
    REGISTER_COUNTER(degitalDownCount, 9);
    REGISTER_COUNTER(degitalLeftCount, 10);
    REGISTER_COUNTER(degitalRightCount, 11);
    REGISTER_COUNTER(buttonStartCount, 12);
    REGISTER_COUNTER(buttonBackCount, 13);
    REGISTER_COUNTER(buttonLeftThumbCount, 14);
    REGISTER_COUNTER(buttonRightThumbCount, 15);
    REGISTER_COUNTER(buttonLeftShoulderCount, 16);
    REGISTER_COUNTER(buttonLeftTriggerCount, 17);
    REGISTER_COUNTER(buttonRightShoulderCount, 18);
    REGISTER_COUNTER(buttonRightTriggerCount, 19);
    REGISTER_COUNTER(buttonACount, 20);
    REGISTER_COUNTER(buttonBCount, 21);
    REGISTER_COUNTER(buttonXCount, 22);
    REGISTER_COUNTER(buttonYCount, 23);
#undef REGISTER_COUNTER
    publish(TJS_W("Gamepad"), pad);
    global->Release();
}
} // namespace

NCB_POST_REGIST_CALLBACK(RegisterGamepads);
