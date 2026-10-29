# Explicit Web local Steam plugin service

This analysis precedes implementation. The four maintained reference images
were checked through native IDA MCP in this conversation. The supplied Windows
plugin is additional interface evidence; it does not replace the four-image
reference check or establish a shared native Steam implementation.

## Inputs and mapping

All four binaries and companion IDBs are readable in `reference/binaries/`.
The workspace's private `reference-copies.json` records the verified source and
copy digests. Fresh health checks identify the expected modules and bases;
the IDBs retain their original fixture input-path metadata. The bytes reside
in this PFB's copied IDBs. Shared workers are adopted read-only; their owner
saves and closes them.

| Reference image | Internal plugin initialization | Lowercased module loader | Plugins.link | Steam registration/API status |
| --- | --- | --- | --- | --- |
| `Kirikiroid2_1.3.9_Android_arm64-v8a.so` | `sub_548D04@0x548D04` | `sub_701DE8@0x701DE8` | `sub_9086A0@0x9086A0` | No literal `krkrsteam.dll`, `Steam`, `initializeSteamAPI` or `restartAppIfNecessary` registration found |
| `Kirikiroid2_1.3.9_Android_armabi-v7a.so` | `sub_4A9598@0x4A9598` | `sub_5BA8E8@0x5BA8E8` | `sub_6C7E58@0x6C7E58` | Same bounded negative result |
| `Kirikiroid2_1.3.9_iOS_arm64` | `sub_100287ACC@0x100287ACC` | `sub_10029FDE4@0x10029FDE4` | `sub_1003F2210@0x1003F2210` | Same bounded negative result |
| `Kirikiroid2_1.3.9_iOS_armv7` | `sub_28A7DC@0x28A7DC` | `sub_2A48FC@0x2A48FC` | `sub_3D9884@0x3D9884` | Same bounded negative result |

All twelve functions have fresh, complete native `decompile` results. The
negative search is independently checked by raw UTF-16LE `find_bytes` for all
four names, ASCII `find` for Steam/krkrsteam/restartAppIfNecessary, and symbol
queries. UTF-16LE `xp3filter.dll` is the positive control: its full 28 bytes are
read at, respectively, `0x14BF490`, `0x4A95F4`, `0x101971B64` and `0x1763F10`.
The rooted loader decompilation confirms that the module map is populated by
static registrars, and a missing key does not execute a project PE DLL.
This establishes the absence of this named TJS plugin contract within the
searched references; it does not assert absence of every conceivable online
service under unrelated or dynamically generated names.

## Common reference pseudocode and differences

```text
internalPlugins():
    AllRegist(Pre)
    AllRegist(Class)
    AllRegist(Post)
    LoadModule("xp3filter.dll")

LoadModule(input):
    name = asciiLowercase(copy(input))
    if registeredSet.contains(name): return false
    module = internalMap.find(name)
    if absent: return false
    for line in [Pre, Class, Post]:
        for registrar in module.lists[line]: registrar.Regist()
    registeredSet.insert(name)
    return true

Plugins.link(arguments):
    if argumentCount < 1: return TJS_E_BADPARAMCOUNT
    name = string(arguments[0])
    LoadModule(name)             // bool is ignored
    return TJS_S_OK
```

Android arm64 unrolls the three registration-list walks; Android armv7 and both
iOS images use a three-iteration loop. Android uses libstdc++ list sentinels;
iOS uses libc++ list linkage. Pointer widths, stack canaries, temporary-string
destruction and iOS armv7 register-save materialization differ without changing
the public control flow. No reference makes an absent plugin a successful
external SDK initialization, and none supplies the missing Steam API contract.

`cpp/core/plugin/{ncbind,PluginImpl}.cpp` already follows that common flow and
will not change. The Web extension contributes one ordinary NCB registrar
module through the existing static-link ownership and loading sequence; it
does not change lookup, fallback, module names or default initialization.

## Additional Windows interface evidence

The private supplied `krkrsteam.dll` SHA-256 is
`4888c60c4da57483dd5815e0005aba9c196bb4761d9ce0804a0d370d669a7021`.
Fresh and retained native evidence follows its registration function
`sub_10004120@0x10004120` to these members:

- `restartAppIfNecessary` directly binds `SteamAPI_RestartAppIfNecessary`.
- `initializeSteamAPI` binds `sub_10003120`, creates the native service and
  registers callbacks/storage; `requestInitialize` requests statistics on an
  existing service, rather than constructing one.
- `initialized` binds `sub_10002750`, which reads the statistics-ready flag;
  callback `sub_10006B90` sets that flag only for the matching successful
  statistics response.
- `getAchievement` binds `sub_100027A0`/`sub_100067D0`; a ready service and a
  valid string ID or integer enumeration index produce a TJS Dictionary with
  `ach`, `achieved`, `unlockTime`, `name`, `desc`. Unsupported/unknown lookup
  returns Void. No `hidden` field is published by this wrapper.
- `setAchievement` (`sub_10002840`) and `clearAchievement` (`sub_100029F0`)
  require ready state, resolve a nonempty ID and return the actual service
  operation result. Set also requests statistics storage.
- `triggerScreenshot` dispatches to a real screenshot service. A browser
  implementation without that service must report unsupported.

The supplied `steam_api.dll` SHA-256 is
`eef1cb35fec1d67831529083eef141e602c40af0bfbbb4646609784967ca959d`.
Its actual restart/init exports construct a local `Steam_Client` object and
operate on local maps, handles and configuration, returning false for restart
and success for completed local initialization. This is additional evidence
that a real local implementation of the plugin protocol is possible, not
proof that a browser can execute PE binaries or a reason to fake online status.

## Explicit browser platform boundary and implementation plan

The browser cannot load the native PE DLL or access Valve's native SDK. The
extension is a real, opt-in local service with different, documented backing:

```text
default: local service disabled
capability export: krkr2_host_local_steam_service_abi() == 1
host selects Module.retromLocalSteamService === true before startup

restartAppIfNecessary(appId):
    require opt-in and a positive uint32 appId
    select the app namespace; reject changing a live namespace
    return false               // this local backend needs no process restart

initializeSteamAPI():
    require opt-in and a selected appId
    read bounded native local-state file through normal TVP storage
    validate complete format, appId, record counts, IDs and timestamps
    create/publish a real empty state when no state exists
    only after complete successful I/O set statisticsReady = true
    return true

requestInitialize():
    require opt-in
    return statisticsReady     // local statistics were loaded synchronously

getAchievement(id/index):
    if not ready or no known record: return Void
    return the actual local record Dictionary; absent display metadata is empty

set/clearAchievement(id):
    if not ready: return false
    validate a bounded nonempty ID
    compute a tentative copy of actual local records
    publish the complete bounded state into managed VLFS
    commit memory only after successful publication
    return the actual result
```

The local state uses `TVPDataPath`, with a directory and decimal appId derived
from the public API. No content identity, title, game class, fixed app ID,
achievement name or checksum selects behavior. The managed file enters the
existing original native-save overlay and is restored before initialization;
it does not create an independent browser localStorage database or claim an
online award. Creation/settings/achievement writes do not manufacture a game
progress notification. They travel with the next real native GAME_SAVE.

The implementation uses no asynchronous native callbacks or retained TJS
objects: local stats readiness follows synchronous native storage completion.
The module resets its C++ records at the engine's prepare-exit phase. Unsupported
cloud, entitlement, DLC, broadcast and remote-login APIs are not registered.
Screenshot requests explicitly report unsupported until an actual host capture
service is connected. Non-Web targets retain their previous build behavior.

## Local implementation comparison and validation limits

Before editing, the only existing `steamPlugin.cpp` is outside the build and
contains example names/timestamps, constant initialization success and dummy
cloud/DLC results. Enabling it would fail both the observed wrapper surface and
the truthful-service contract. The new `steam/SteamWeb.cpp` supplies that path only
for Emscripten; the existing NCB loader and all native reference behavior remain
unchanged.

| Planned browser boundary | Implemented source/data flow |
| --- | --- |
| Explicit selection, default disabled | `enabled/requireEnabled` reads strict boolean `Module.retromLocalSteamService` before initialization/restart/request |
| Positive public app namespace | `Steam::restartAppIfNecessary` validates integer range, then `LocalService::select`; live namespace changes fail |
| Real statistics readiness | `LocalService::initialize` validates complete original managed file or successfully publishes an empty state, then commits records and ready |
| No native async callback dependency | `requestInitialize/initialized` report the actual local readiness flag; prepare-exit clears the service |
| Truthful achievement writes | `LocalService::update` copies records, preserves repeated-set timestamps, encodes bounded complete bytes, publishes managed file, then commits memory |
| Native wrapper result shape | `getAchievement` creates the actual five-field TJS Dictionary and releases the temporary reference on normal/exceptional exits; absent lookup remains Void |
| Explicit unsupported service boundary | Screenshot throws a stable unsupported error; unimplemented remote methods are not registered |
| No loader change | Existing NCB module registration and whole-archive link retain the new module and KEEPALIVE capability export |

Independent Emscripten 6.0.9 `-fsyntax-only` on the current PFB compile-command
inputs succeeds (only an existing `_tss` literal-operator warning). Source
verification and diff whitespace checks pass. Full candidate build, actual ABI
export and product/save acceptance are coordinated by the owning PFB task;
syntax success does not establish runtime acceptance.

No standalone fabricated game fixture or plugin oracle is introduced. Validation
uses source/build gates and the supplied project's actual import/preview/save
flow. The private Windows evidence covers interface names and native state
gates; it does not verify the new managed-file format or every uncalled plugin
feature. The product acceptance report must distinguish direct observed calls,
state persistence checks and unexercised optional screenshots/full routes.
