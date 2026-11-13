# Web System confirmation boundary

The previous Web registration exposed `System.inform`, but lacked a
`System.confirm` member. A compiled KAG caller invokes
`System.confirm(message, caption[, owner])` and branches on its boolean return.
This browser addition uses the existing, interactive two-button
platform dialog; it does not select an answer or modify project scripts.

## Fresh reference mapping (2026-11-13)

All four binaries and matching IDBs were checked, opened through native IDA MCP,
and verified with server_health. The registration and inform callbacks below
were decompiled in this session, with the corresponding database on every call.

| Binary | Platform System registration | inform callback | confirm mapping |
| --- | --- | --- | --- |
| Kirikiroid2_1.3.9_Android_arm64-v8a.so | sub_9108F8 @ 0x9108f8 | sub_9111C8 @ 0x9111c8 | No literal/member located by UTF-16LE and UTF-32LE byte searches and function-name search |
| Kirikiroid2_1.3.9_Android_armabi-v7a.so | sub_6CC124 @ 0x6cc124 | sub_6CC914 @ 0x6cc914 | Same search results |
| Kirikiroid2_1.3.9_iOS_arm64 | sub_1000982F4 @ 0x1000982f4 | sub_100098B5C @ 0x100098b5c | Same search results; unrelated advertising confirmation functions are not TJS members |
| Kirikiroid2_1.3.9_iOS_armv7 | sub_966E8 @ 0x966e8 | sub_970A8 @ 0x970a8 | Same search results; unrelated advertising confirmation functions are not TJS members |

The `inform` registration literal was independently read as the complete seven
UTF-16 code units (including NUL) in every binary. Truncated IDA string types
were corrected. iOS's displayed `I` and `O` arguments were independently read as
`Information` and `OK`, and their global types were corrected too; the old helper
prototypes still make Hex-Rays render abbreviated literals.

## Shared reference flow

```
inform(result, argc, argv):
    if argc < 1: return BADPARAMCOUNT
    text = ttstr(argv[0])
    caption = supplied non-void argv[1], otherwise "Information"
    if argv[2] is absent or void:
        show the one-button platform dialog; clear result
    else:
        if argv[2] is an object:
            get its native TJS Array; copy its items into button-label strings
        else:
            convert to integer; append OK if >= 1, Cancel if >= 2
        index = platform dialog(text, caption, labels)
        assign index to result when requested
    destroy local strings and label vector; return S_OK
```

Android uses the libstdc++ deque representation for the TJS Array; iOS uses the
libc++ deque representation. ARM64 variants carry 20-byte variants, ARMv7 12-byte
variants, with corresponding pointer/refcount and iterator arithmetic. These are
ABI/container expansions, not different caption defaults or button semantics.
All four callbacks share every branch above. None establishes a boolean
`System.confirm` contract: the added member is explicitly a Web platform API.

## Local correspondence and platform boundary

`TVPCreateNativeClass_System` already retains this shared `inform` flow. It is
unchanged. Its new Web-only `confirm` callback uses the same required-message
check, string conversion, optional-caption handling and Information default.
It calls `TVPShowSimpleMessageBoxYesNo`, whose existing platform implementation
constructs Yes/No labels and returns the selected index. Only index zero becomes
true; No/dismissal becomes false. A supplied owner is accepted, but the browser
cannot create or own a native HWND, so the browser dialog belongs to the current
runtime frame. Non-Web System registrations remain reference-derived.

The existing Web two-button path delegates to the real browser confirmation API.
The original compiled caller reached this real dialog and continued after
dismissal, exercising the false branch and the actual worker/main-thread path.
Its language prompt requests factual attestations; the test does not make those
attestations. Affirmative-branch coverage is therefore not claimed. Source/build
success alone is insufficient to establish dialog or project compatibility.

There is no existing public System-dialog runtime fixture in this checkout.
Per repository rules, no synthetic game or replacement fixture is fabricated;
the original full-package product reproduction and its console/dialog record
remain in ignored acceptance storage.
