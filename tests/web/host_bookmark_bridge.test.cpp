// Compile and exercise the production Web bridge with shared host dependencies.
#include "host_bookmark_bridge_fixture.h"
#include "cpp/core/environ/web/HostBookmarkBridge.cpp"

int main() {
    assert(krkr2_host_load_bookmark_is_ready() == 0);
    testLoopRegistered = true;
    iTJSDispatch2 global, kag, load, save;
    testGlobal = &global;
    assert(krkr2_host_load_bookmark_is_ready() == 0);
    global.properties[L"kag"] = tTJSVariant(&kag);
    assert(krkr2_host_load_bookmark_is_ready() == 0);
    kag.properties[L"loadBookMark"] = tTJSVariant(static_cast<iTJSDispatch2 *>(nullptr));
    assert(krkr2_host_load_bookmark_is_ready() == 0);
    kag.properties[L"loadBookMark"] = tTJSVariant(&load);
    assert(krkr2_host_load_bookmark_is_ready() == 0);
    load.function = true;
    assert(krkr2_host_load_bookmark_is_ready() == 0);
    TVPStartupSuccess = true;
    kag.properties[L"saveBookMark"] = tTJSVariant(&save);
    save.function = true;
    kag.properties[L"currentLabel"] = tTJSVariant(L"");
    kag.properties[L"inStable"] = tTJSVariant(1);
    // A callable loader and default stable flag can be visible while a JSPI
    // tick is still suspended within script initialization.
    testLoopPending = true;
    assert(krkr2_host_load_bookmark_is_ready() == 0);
    assert(krkr2_host_load_bookmark(1999) == kBookmarkRejected);
    assert(krkr2_host_load_bookmark_state() == kLoadIdle);
    assert(krkr2_host_bookmark_is_ready() == 0);
    testLoopPending = false;
    // A complete startup tick can leave KAG awaiting input without notifying
    // stable or starting its first scenario process. Existing saves can load.
    kag.properties[L"inStable"] = tTJSVariant(0);
    kag.properties[L"isFirstProcess"] = tTJSVariant(1);
    assert(krkr2_host_load_bookmark_is_ready() == 1);
    assert(krkr2_host_bookmark_is_ready() == 0);
    assert(krkr2_host_load_bookmark(1999) == 0);
    assert(krkr2_host_load_bookmark_state() == 1);
    assert(load.calls == 0);
    // The callback executes within its own engine tick: do not reject it merely
    // because that tick is active or the startup scene is not a savepoint.
    testLoopPending = true;
    deliverQueued();
    assert(load.calls == 1 && load.slot == 1999);
    assert(krkr2_host_load_bookmark_state() == 2);
    testLoopPending = false;
    // Cancel must also win when the queue is dispatched afterward.
    load_state.store(kLoadIdle);
    assert(krkr2_host_load_bookmark(1999) == kBookmarkSucceeded);
    krkr2_host_cancel_bookmark_load();
    deliverQueued();
    assert(krkr2_host_load_bookmark_state() == kBookmarkRejected);
    assert(load.calls == 1);
    kag.properties[L"inStable"] = tTJSVariant(1);
    assert(krkr2_host_bookmark_is_ready() == 0);
    kag.properties[L"currentLabel"] = tTJSVariant(L"*chapter");
    assert(krkr2_host_bookmark_is_ready() == 1);
    kag.properties.erase(L"inStable");
    assert(krkr2_host_bookmark_is_ready() == 0);
    kag.properties[L"inStable"] = tTJSVariant(&kag);
    assert(krkr2_host_bookmark_is_ready() == 0);
    kag.properties[L"inStable"] = tTJSVariant(0);
    assert(krkr2_host_bookmark_is_ready() == 0);
    assert(krkr2_host_save_bookmark(1999) == kBookmarkRejected);
    kag.properties[L"inStable"] = tTJSVariant(1);
    testLoopStopping = true;
    assert(krkr2_host_load_bookmark_is_ready() == 0);
    assert(krkr2_host_bookmark_is_ready() == 0);
    assert(krkr2_host_load_bookmark(1999) == kBookmarkRejected);
    assert(krkr2_host_save_bookmark(1999) == kBookmarkRejected);
    testLoopStopping = false;
    save.callException = 1;
    assert(krkr2_host_save_bookmark(1999) == kScriptException);
    save.callException = 2;
    assert(krkr2_host_save_bookmark(1999) == kScriptException);
    global.throws = true;
    assert(krkr2_host_load_bookmark_is_ready() == 0);
    global.throws = false;
    save.callException = 0;

    iTJSDispatch2 entry, replacement;
    entry.function = replacement.function = true;
    global.properties[L"loadFunction"] = tTJSVariant(&entry);
    const int receiverReferences = kag.references;
    auto invoke = [&](const tTJSVariant &callback, int slot) {
        tTJSVariant argument(slot), result;
        tTJSVariant *params = &argument;
        return callback.AsObjectNoAddRef()->FuncCall(
            0, nullptr, nullptr, &result, 1, &params, &kag);
    };
    auto request = [&] {
        assert(!pending_load);
        load_state.store(kLoadIdle);
        assert(krkr2_host_load_bookmark(1999) == kBookmarkSucceeded);
        assert(krkr2_host_load_bookmark(1999) == kLoadAlreadyRequested);
        deliverQueued();
    };
    auto balanced = [&] {
        assert(!pending_load);
        assert(kag.references == receiverReferences);
        assert(liveDispatches == 0);
        assert(kag.properties.at(L"loadBookMark").AsObjectNoAddRef() == &load);
    };

    // Framework entry return values are not load results. A deferred entry
    // can return false/void; only the requested original bookmark completes it.
    entry.callResult = 0;
    request();
    assert(krkr2_host_load_bookmark_state() == kLoadPending);
    assert(kag.references > receiverReferences);
    {
        tTJSVariant observer = kag.properties.at(L"loadBookMark");
        assert(invoke(observer, 5) == TJS_S_OK);
        assert(krkr2_host_load_bookmark_state() == kLoadPending);
        assert(invoke(observer, 1999) == TJS_S_OK);
        assert(krkr2_host_load_bookmark_state() == kLoadSucceeded);
        // An alias retained by a script remains callable but cannot complete
        // the operation again after the one-shot observer is detached.
        load.callResult = 0;
        assert(invoke(observer, 1999) == TJS_S_OK);
        assert(krkr2_host_load_bookmark_state() == kLoadSucceeded);
        load.callResult = 1;
    }
    balanced();

    // Public KAGEX variants also call the original loader synchronously.
    entry.onCall = [&](tTJSVariant *result, int, tTJSVariant **) {
        const auto observer = kag.properties.at(L"loadBookMark");
        invoke(observer, 1999);
        *result = tTJSVariant();
    };
    request();
    assert(krkr2_host_load_bookmark_state() == kLoadSucceeded);
    balanced();
    entry.onCall = {};

    for(int failure : {0, 1, 2, 3}) {
        request();
        {
            auto observer = kag.properties.at(L"loadBookMark");
            load.callResult = failure == 0 ? 0 : 1;
            load.callStatus = failure == 1 ? TJS_E_FAIL : TJS_S_OK;
            load.callException = failure >= 2 ? failure - 1 : 0;
            try { invoke(observer, 1999); assert(failure < 2); }
            catch(...) { assert(failure >= 2); }
            assert(krkr2_host_load_bookmark_state() ==
                (failure == 0 ? kBookmarkRejected : failure == 1 ? kMethodUnavailable : kScriptException));
        }
        load.callResult = 1; load.callStatus = TJS_S_OK; load.callException = 0;
        balanced();
    }

    request();
    {
        auto observer = kag.properties.at(L"loadBookMark");
        kag.properties[L"loadBookMark"] = tTJSVariant(&replacement);
        invoke(observer, 1999);
        assert(krkr2_host_load_bookmark_state() == kLoadSucceeded);
        assert(kag.properties.at(L"loadBookMark").AsObjectNoAddRef() == &replacement);
    }
    kag.properties[L"loadBookMark"] = tTJSVariant(&load);
    balanced();

    request();
    {
        auto observer = kag.properties.at(L"loadBookMark");
        krkr2_host_cancel_bookmark_load();
        assert(krkr2_host_load_bookmark_state() == kBookmarkRejected);
        invoke(observer, 1999);
        assert(krkr2_host_load_bookmark_state() == kBookmarkRejected);
    }
    balanced();

    // Neither an entry error nor an observer installation error may leak the
    // pending owner, leave a false success, or bypass the framework loader.
    const int callsBeforeFailure = load.calls;
    for(int failure : {0, 1, 2}) {
        entry.callException = failure == 0 ? 1 : 0;
        entry.callStatus = failure == 1 ? TJS_E_FAIL : TJS_S_OK;
        kag.setStatus = failure == 2 ? TJS_E_FAIL : TJS_S_OK;
        request();
        assert(krkr2_host_load_bookmark_state() ==
            (failure == 0 ? kScriptException : kMethodUnavailable));
        assert(load.calls == callsBeforeFailure);
        balanced();
    }
    entry.callStatus = TJS_S_OK; kag.setStatus = TJS_S_OK;
    entry.function = false;
    request();
    assert(krkr2_host_load_bookmark_state() == kMethodUnavailable);
    assert(load.calls == callsBeforeFailure);
    balanced();
    entry.function = true;

    // A failing property writer must not retain the receiver or mask failure.
    // The remaining callable wrapper owns only the original function.
    for(bool throws : {false, true}) {
        request();
        kag.setThrows = throws;
        kag.setStatus = throws ? TJS_S_OK : TJS_E_FAIL;
        {
            auto observer = kag.properties.at(L"loadBookMark");
            assert(invoke(observer, 1999) == TJS_E_FAIL);
            assert(krkr2_host_load_bookmark_state() == kScriptException);
        }
        assert(!pending_load && kag.references == receiverReferences);
        kag.setThrows = false; kag.setStatus = TJS_S_OK;
        kag.properties[L"loadBookMark"] = tTJSVariant(&load);
        balanced();
    }

    // A loader override can discard its base loader's return value. A void
    // return is successful only with a causal, completed restoreFlags call
    // inside this requested load; neither old stable state nor an unrelated
    // restore before the request is evidence of this load succeeding.
    iTJSDispatch2 restore;
    restore.function = true;
    kag.properties[L"restoreFlags"] = tTJSVariant(&restore);
    const auto restoreNow = [&] {
        const auto method = kag.properties.at(L"restoreFlags");
        tTJSVariant result;
        return method.AsObjectNoAddRef()->FuncCall(
            0, nullptr, nullptr, &result, 0, nullptr, &kag);
    };
    load.onCall = [&](tTJSVariant *result, int, tTJSVariant **) {
        assert(restoreNow() == TJS_S_OK);
        *result = tTJSVariant();
    };
    request();
    invoke(kag.properties.at(L"loadBookMark"), 1999);
    assert(krkr2_host_load_bookmark_state() == kLoadSucceeded);
    balanced();
    assert(kag.properties.at(L"restoreFlags").AsObjectNoAddRef() == &restore);
    assert(restore.references == 2);

    // The same completion contract applies without a framework entry.
    global.properties.erase(L"loadFunction");
    request();
    assert(krkr2_host_load_bookmark_state() == kLoadSucceeded);
    balanced();
    global.properties[L"loadFunction"] = tTJSVariant(&entry);

    restoreNow();
    load.onCall = [](tTJSVariant *result, int, tTJSVariant **) { *result = tTJSVariant(); };
    request();
    invoke(kag.properties.at(L"loadBookMark"), 1999);
    assert(krkr2_host_load_bookmark_state() == kBookmarkRejected);
    balanced();

    for(int failure : {0, 1, 2, 3, 4, 5}) {
        load.onCall = [&](tTJSVariant *result, int, tTJSVariant **) {
            if(failure == 3) {
                restore.callStatus = TJS_E_FAIL;
                assert(restoreNow() == TJS_E_FAIL);
                restore.callStatus = TJS_S_OK;
            } else if(failure == 4) {
                restore.callResult = 0;
                assert(restoreNow() == TJS_S_OK);
                restore.callResult = 1;
            } else if(failure == 5) {
                restore.callException = 1;
                try { restoreNow(); assert(false); } catch(const std::exception &) {}
                restore.callException = 0;
            } else {
                assert(restoreNow() == TJS_S_OK);
            }
            assert(krkr2_host_load_bookmark_state() == kLoadPending);
            if(failure == 1) throw std::runtime_error("failure after restore");
            *result = failure == 0 ? tTJSVariant(0) :
                failure == 5 ? tTJSVariant(1) : tTJSVariant();
            if(failure == 2) kag.properties[L"restoreFlags"] = tTJSVariant(&replacement);
        };
        request();
        try { invoke(kag.properties.at(L"loadBookMark"), 1999); assert(failure != 1); }
        catch(...) { assert(failure == 1); }
        assert(krkr2_host_load_bookmark_state() ==
            (failure == 1 ? kScriptException : failure == 2 ? kLoadSucceeded : kBookmarkRejected));
        if(failure == 2) {
            assert(kag.properties.at(L"restoreFlags").AsObjectNoAddRef() == &replacement);
            kag.properties[L"restoreFlags"] = tTJSVariant(&restore);
        }
        balanced();
        assert(kag.properties.at(L"restoreFlags").AsObjectNoAddRef() == &restore);
        assert(restore.references == 2);
    }
    tTJSVariant retainedRestore;
    load.onCall = [&](tTJSVariant *result, int, tTJSVariant **) {
        retainedRestore = kag.properties.at(L"restoreFlags");
        assert(restoreNow() == TJS_S_OK);
        *result = tTJSVariant();
    };
    request();
    invoke(kag.properties.at(L"loadBookMark"), 1999);
    assert(krkr2_host_load_bookmark_state() == kLoadSucceeded);
    assert(!pending_load && kag.references == receiverReferences);
    assert(liveDispatches == 1);
    {
        tTJSVariant result;
        load_state.store(kBookmarkRejected);
        assert(retainedRestore.AsObjectNoAddRef()->FuncCall(
            0, nullptr, nullptr, &result, 0, nullptr, &kag) == TJS_S_OK);
        assert(krkr2_host_load_bookmark_state() == kBookmarkRejected);
    }
    retainedRestore = tTJSVariant();
    balanced();

    load.onCall = [&](tTJSVariant *result, int, tTJSVariant **) {
        assert(restoreNow() == TJS_S_OK);
        kag.setStatus = TJS_E_FAIL; // Fail removal after a completed restore.
        *result = tTJSVariant();
    };
    request();
    assert(invoke(kag.properties.at(L"loadBookMark"), 1999) == TJS_E_FAIL);
    assert(krkr2_host_load_bookmark_state() == kMethodUnavailable);
    assert(!pending_load && kag.references == receiverReferences);
    assert(liveDispatches == 1); // Leftover callback holds no receiver cycle.
    kag.setStatus = TJS_S_OK;
    kag.properties[L"restoreFlags"] = tTJSVariant(&restore);
    balanced();
    load.onCall = {};

    // A retained loader invoked with another receiver is only a passthrough.
    // It cannot consume the matching pending request or dereference null.
    iTJSDispatch2 anotherKag;
    for(auto receiver : {&anotherKag, static_cast<iTJSDispatch2 *>(nullptr)}) {
        request();
        const auto callback = kag.properties.at(L"loadBookMark");
        tTJSVariant argument(1999), result;
        tTJSVariant *params = &argument;
        assert(callback.AsObjectNoAddRef()->FuncCall(
            0, nullptr, nullptr, &result, 1, &params, receiver) == TJS_S_OK);
        assert(krkr2_host_load_bookmark_state() == kLoadPending);
        invoke(callback, 1999);
        assert(krkr2_host_load_bookmark_state() == kLoadSucceeded);
    }
    balanced();

    // Neither another receiver nor a property getter running during removal
    // may supply a witness outside the original loader's target/call scope.
    for(bool duringRemoval : {false, true}) {
        load.onCall = [&](tTJSVariant *result, int, tTJSVariant **) {
            retainedRestore = kag.properties.at(L"restoreFlags");
            if(!duringRemoval) {
                tTJSVariant unused;
                retainedRestore.AsObjectNoAddRef()->FuncCall(
                    0, nullptr, nullptr, &unused, 0, nullptr, &anotherKag);
            }
            *result = tTJSVariant();
        };
        kag.onGet = [&](const wchar_t *name) {
            if(duringRemoval && std::wstring(name) == L"restoreFlags" &&
               retainedRestore.Type() == tvtObject) {
                tTJSVariant unused;
                retainedRestore.AsObjectNoAddRef()->FuncCall(
                    0, nullptr, nullptr, &unused, 0, nullptr, &kag);
            }
        };
        request();
        invoke(kag.properties.at(L"loadBookMark"), 1999);
        assert(krkr2_host_load_bookmark_state() == kBookmarkRejected);
        kag.onGet = {};
        retainedRestore = tTJSVariant();
        balanced();
    }
    load.onCall = {};

    // Bootstrap coordination belongs after the existing framework/alias cases:
    // its conductor properties must not change those legacy fixture contracts.
    {
        global.properties.erase(L"loadFunction");
        iTJSDispatch2 conductor, waits, timeout, trigger, click, replacementKag;
        const auto tick = [&] {
            assert(cocos2d::Director::getInstance()->scheduler.queued);
            deliverQueued();
        };
        const auto configureWait = [&] {
            assert(!cocos2d::Director::getInstance()->scheduler.queued);
            global.properties[L"kag"] = tTJSVariant(&kag);
            kag.properties[L"conductor"] = tTJSVariant(&conductor);
            kag.properties[L"mainConductor"] = tTJSVariant(&conductor);
            kag.properties[L"inStable"] = tTJSVariant(0);
            kag.properties[L"isFirstProcess"] = tTJSVariant(0);
            conductor.properties.clear();
            conductor.properties[L"mStop"] = tTJSVariant(0);
            conductor.properties[L"mRun"] = tTJSVariant(1);
            conductor.properties[L"mWait"] = tTJSVariant(2);
            conductor.properties[L"status"] = tTJSVariant(2);
            conductor.properties[L"timerEnabled"] = tTJSVariant(0);
            conductor.properties[L"lastTagName"] = tTJSVariant(L"waitclick");
            conductor.properties[L"waitUntil"] = tTJSVariant(&waits);
            conductor.properties[L"timeOutTimer"] = tTJSVariant(&timeout);
            conductor.properties[L"trigger"] = tTJSVariant(&trigger);
            conductor.properties[L"curStorage"] = tTJSVariant(L"startup.ks");
            conductor.properties[L"curLine"] = tTJSVariant(12);
            conductor.properties[L"callStackDepth"] = tTJSVariant(1);
            timeout.properties.clear();
            timeout.properties[L"enabled"] = tTJSVariant(0);
            waits.properties.clear();
            waits.properties[L"click"] = tTJSVariant(&click);
            waits.countStatus = TJS_S_OK;
            click.function = trigger.function = true;
            trigger.calls = 0;
            trigger.lastReceiver = nullptr;
            trigger.callStatus = TJS_S_OK;
            trigger.callResult = 1;
            trigger.callException = 0;
            trigger.onCall = {};
        };
        const auto begin = [&] {
            load_state.store(kLoadIdle);
            assert(krkr2_host_load_bookmark(1999) == kBookmarkSucceeded);
            assert(krkr2_host_load_bookmark_state() == kLoadPending);
        };
        const auto cancelAndDrain = [&] {
            krkr2_host_cancel_bookmark_load();
            if(cocos2d::Director::getInstance()->scheduler.queued) tick();
            assert(krkr2_host_load_bookmark_state() != kLoadPending);
            assert(!cocos2d::Director::getInstance()->scheduler.queued);
            balanced();
        };

        // A fresh session/readiness poll never acknowledges even a valid wait.
        configureWait();
        const int beforeBootstrap = load.calls;
        load_state.store(kLoadIdle);
        for(int poll = 0; poll < 3; ++poll)
            assert(krkr2_host_load_bookmark_is_ready() == 1);
        assert(trigger.calls == 0 && load.calls == beforeBootstrap);
        assert(!cocos2d::Director::getInstance()->scheduler.queued);

        // Loader publication precedes first-scenario initialization. Running
        // script waits; one standard signal resumes it, then a stable phase
        // allows the original loader on a later engine tick.
        conductor.properties[L"status"] = tTJSVariant(1);
        conductor.properties[L"timerEnabled"] = tTJSVariant(1);
        conductor.properties[L"lastTagName"] = tTJSVariant(L"ch");
        begin();
        tick();
        assert(load.calls == beforeBootstrap && trigger.calls == 0);
        assert(krkr2_host_load_bookmark_state() == kLoadPending);
        conductor.properties[L"status"] = tTJSVariant(2);
        conductor.properties[L"timerEnabled"] = tTJSVariant(0);
        conductor.properties[L"lastTagName"] = tTJSVariant(L"waitclick");
        trigger.onCall = [&](tTJSVariant *, int count, tTJSVariant **params) {
            assert(count == 1 && params[0]->Type() == tvtString);
            assert(params[0]->string.value == L"click");
            conductor.properties[L"status"] = tTJSVariant(1);
        };
        tick();
        assert(trigger.calls == 1 && trigger.lastReceiver == &conductor);
        assert(load.calls == beforeBootstrap);
        tick();
        assert(trigger.calls == 1 && load.calls == beforeBootstrap);
        // A default stable flag cannot override an actively running script.
        kag.properties[L"inStable"] = tTJSVariant(1);
        tick();
        assert(load.calls == beforeBootstrap);
        conductor.properties[L"status"] = tTJSVariant(2);
        tick();
        assert(load.calls == beforeBootstrap + 1);
        assert(krkr2_host_load_bookmark_state() == kLoadSucceeded);
        balanced();

        // Stable loaders need no conductor/save label. Plain KAG can also
        // await host input before starting its first scenario at all.
        for(int ready = 0; ready < 3; ++ready) {
            configureWait();
            kag.properties[L"currentLabel"] = tTJSVariant(L"");
            if(ready == 0) {
                kag.properties[L"inStable"] = tTJSVariant(1);
                kag.properties.erase(L"conductor");
            } else {
                kag.properties[L"isFirstProcess"] = tTJSVariant(1);
                if(ready == 1) kag.properties.erase(L"conductor");
                else conductor.properties[L"status"] = tTJSVariant(0);
            }
            const int before = load.calls;
            begin();
            tick();
            assert(load.calls == before + 1 && trigger.calls == 0);
            assert(krkr2_host_load_bookmark_state() == kLoadSucceeded);
            assert(krkr2_host_bookmark_is_ready() == 0);
            balanced();
        }

        // No body/page/stop input, timed/ambiguous wait, malformed callback,
        // extra conductor or unknown phase may be promoted to a click barrier.
        for(int blocked = 0; blocked < 21; ++blocked) {
            configureWait();
            switch(blocked) {
            case 0: conductor.properties[L"lastTagName"] = tTJSVariant(L"p"); break;
            case 1: conductor.properties[L"lastTagName"] = tTJSVariant(L"l"); break;
            case 2: conductor.properties[L"lastTagName"] = tTJSVariant(L"s"); break;
            case 3: timeout.properties[L"enabled"] = tTJSVariant(1); break;
            case 4: waits.properties[L"timeout"] = tTJSVariant(&click); break;
            case 5: waits.properties[L"click_arg"] = tTJSVariant(1); break;
            case 6: waits.properties.clear(); break;
            case 7: click.function = false; break;
            case 8: trigger.function = false; break;
            case 9: conductor.properties.erase(L"trigger"); break;
            case 10: kag.properties[L"mainConductor"] = tTJSVariant(&anotherKag); break;
            case 11: conductor.properties[L"timerEnabled"] = tTJSVariant(1); break;
            case 12: timeout.properties.erase(L"enabled"); break;
            case 13: timeout.properties[L"enabled"] = tTJSVariant(L"0"); break;
            case 14: kag.properties.erase(L"inStable"); break;
            case 15: kag.properties[L"inStable"] = tTJSVariant(L"0"); break;
            case 16: waits.countStatus = TJS_E_FAIL; break;
            case 17: conductor.properties[L"curStorage"] = tTJSVariant(L""); break;
            case 18: conductor.properties[L"curLine"] = tTJSVariant(-1); break;
            case 19: conductor.properties[L"callStackDepth"] = tTJSVariant(-1); break;
            case 20:
                kag.properties[L"isFirstProcess"] = tTJSVariant(1);
                conductor.properties[L"status"] = tTJSVariant(1);
                break;
            }
            const int before = load.calls;
            begin();
            tick();
            tick();
            assert(trigger.calls == 0 && load.calls == before);
            assert(krkr2_host_load_bookmark_state() == kLoadPending);
            cancelAndDrain();
        }

        // A successful trigger must return a real integer true. False, void,
        // other truthy types, dispatch failures and exceptions all reject.
        for(int failure = 0; failure < 6; ++failure) {
            configureWait();
            if(failure == 0) trigger.callResult = 0;
            if(failure == 1) trigger.onCall = [](tTJSVariant *result, int, tTJSVariant **) {
                *result = tTJSVariant();
            };
            if(failure == 2) trigger.callException = 1;
            if(failure == 3) trigger.onCall = [](tTJSVariant *result, int, tTJSVariant **) {
                *result = tTJSVariant(L"true");
            };
            if(failure == 4) trigger.onCall = [&](tTJSVariant *result, int, tTJSVariant **) {
                *result = tTJSVariant(&click);
            };
            if(failure == 5) trigger.callStatus = TJS_E_FAIL;
            const int before = load.calls;
            begin();
            tick();
            assert(krkr2_host_load_bookmark_state() < 0);
            assert(load.calls == before);
            cancelAndDrain();
        }

        // Mark before sending: a callback that returns to the same dynamic
        // point cannot repeatedly consume input or spin within one tick.
        configureWait();
        int before = load.calls;
        begin();
        tick();
        tick();
        tick();
        assert(trigger.calls == 1 && load.calls == before);
        assert(krkr2_host_load_bookmark_state() == kLoadPending);
        cancelAndDrain();

        // Distinct wait points can resume bootstrap, bounded to 32 signals.
        configureWait();
        trigger.onCall = [&](tTJSVariant *, int, tTJSVariant **) {
            conductor.properties[L"curLine"] = tTJSVariant(12 + trigger.calls);
        };
        before = load.calls;
        begin();
        for(int signal = 1; signal <= 32; ++signal) {
            tick();
            assert(trigger.calls == signal && load.calls == before);
        }
        tick();
        tick();
        assert(trigger.calls == 32 && load.calls == before);
        assert(krkr2_host_load_bookmark_state() == kLoadPending);
        kag.properties[L"inStable"] = tTJSVariant(1);
        tick();
        assert(load.calls == before + 1);
        assert(krkr2_host_load_bookmark_state() == kLoadSucceeded);
        balanced();

        // Cancel must win before the first callback and after a barrier. A
        // leftover queue entry may not even read a script getter afterward.
        for(bool afterSignal : {false, true}) {
            configureWait();
            before = load.calls;
            begin();
            if(afterSignal) tick();
            krkr2_host_cancel_bookmark_load();
            int reads = 0;
            global.onGet = kag.onGet = [&](const wchar_t *) { ++reads; };
            tick();
            assert(reads == 0 && load.calls == before);
            assert(trigger.calls == (afterSignal ? 1 : 0));
            assert(krkr2_host_load_bookmark_state() == kBookmarkRejected);
            global.onGet = kag.onGet = {};
            balanced();
        }

        // A different fully callable Kag cannot inherit the old request.
        configureWait();
        conductor.properties[L"status"] = tTJSVariant(1);
        before = load.calls;
        begin();
        tick(); // Bind the original receiver while its initialization runs.
        replacementKag.properties = kag.properties;
        replacementKag.properties[L"inStable"] = tTJSVariant(1);
        global.properties[L"kag"] = tTJSVariant(&replacementKag);
        tick();
        assert(load.calls == before && trigger.calls == 0);
        assert(krkr2_host_load_bookmark_state() == kBookmarkRejected);
        global.properties[L"kag"] = tTJSVariant(&kag);
        replacementKag.properties.clear();
        balanced();

        // All bootstrap-held script references are gone before locals die.
        kag.properties.erase(L"conductor");
        kag.properties.erase(L"mainConductor");
        conductor.properties.clear();
        waits.properties.clear();
        timeout.properties.clear();
        assert(conductor.references == 1 && waits.references == 1);
        assert(timeout.references == 1 && trigger.references == 1 && click.references == 1);
    }

    // GetGlobal returns an owned reference, including when property lookup
    // fails or throws. Polling and save/load calls must release every lookup.
    assert(global.references == 1);
    global.properties.clear();
    kag.properties.clear();
    assert(kag.references == 1 && load.references == 1 && save.references == 1);
}
