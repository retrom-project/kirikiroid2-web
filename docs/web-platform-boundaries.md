# Web window, fonts and native saves

These are explicit browser platform boundaries, implemented from observed Web
behavior and public contracts. They do not claim reconstruction of the four
reference binaries. Game scripts retain their original behavior.

## Script engine selection

An embedding browser can call `_krkr2_host_set_engine_mode(0)` for the original
KiriKiri2 preprocessing behavior or `(1)` for Z preprocessing, from
`Module.onRuntimeInitialized` before `main` starts. Invalid values and calls
after script-engine initialization return -1; accepted selections return 0.
The default is 0. Mode 1 sets only `kirikiriz=1` before any script compilation;
it does not assert that every Z API or native plugin is supported. The browser
cannot execute the project's Win32 PE engine to obtain this setting. No game
identity or script error chooses a mode.

Startup executes the selected entry once. An exception after partial
initialization propagates through the existing exception boundary; it does not
retry an internal project script against partially initialized globals.

On Web, `win32dialog.dll` also declares the nested `Header` and `Items` template
data models needed by portable dialog libraries at class-definition time.
Their `store` method copies the supplied template fields. These declarations
do not create a native Win32 window or claim a complete browser dialog backend.
Window, dialog and basic control style families (`WS`, `DS`, `ES`, `BS`, `SS`,
`LBS`, `CBS`, `LVS`, `TBS`, `ICC`) retain their public Windows SDK values in the
`WIN32Dialog` namespace. The fact table comes from Microsoft's
[WinUser.h](https://github.com/microsoft/win32metadata/blob/64ed9177fb76eb244fc9051c74b8a93fdfe20873/generation/WinSDK/RecompiledIdlHeaders/um/WinUser.h)
and [CommCtrl.h](https://github.com/microsoft/win32metadata/blob/64ed9177fb76eb244fc9051c74b8a93fdfe20873/generation/WinSDK/RecompiledIdlHeaders/um/CommCtrl.h).
The model constructor retains its event target, and `initCommonControlsEx`
returns false because the browser cannot register native Windows controls.

## Resource overlays

`_krkr2_host_resource_overlay_abi()` returns 1 when the explicit Web resource
search layer is supported. Before startup, the embedding runtime supplies
`Module._resourceOverlayFiles` containing original virtual `path` and
`relativePath` pairs from its validated project index. The default is empty.
The original immutable filesystem tree remains unchanged.

The layer indexes members of selected `.xp3` files individually, in lexical
archive path order, then applies loose files. Lookup precedes the ordinary
placed-path cache: resource-relative exact name first, unique basename second.
Ambiguous basenames do not choose a resource. Explicit archive-member paths
participate in the same lookup. Resources absent from the layer retain native
archive/directory resolution. The layer does not replace whole base archives,
execute a native patch loader or infer a directory from game identity.

## Window presentation

`Window.fullScreen` selects a virtual display area inside the browser surface.
It does not request browser fullscreen or require a user gesture. Presentation
fits the primary paint area once, preserves its aspect ratio and centers it.
Pointer coordinates use the inverse of the same transform. Leaving virtual
fullscreen restores the normal client size and logical zoom.

`windowEx.maximized` similarly describes a per-window virtual placement, with
normal bounds retained for restoration. It is not an alias for browser fullscreen.
The host adapter observes surface and device-pixel-ratio changes; the core owns
the logical window and paint transforms.

## Font selection and baseline

Font candidates are trimmed at their UTF-16 boundaries without removing interior
spaces or CJK characters. Web startup registers bounded loose SFNT files in the
application root and `fonts/`, retaining their complete storage paths. It does
not scan every archive or infer a font from a game title.

For a scalable, non-variable SFNT face with a valid OpenType BASE declaration,
the horizontal default ideographic EM box supplies the shared layout ascent.
Only fixed format-1 coordinates and a square EM box are supported. When the
horizontal ideographic declaration is absent, CJK faces use their valid OS/2 typographic ascent
and descent to define the ideographic EM box, as specified by OpenType. CJK
classification uses declared script ranges or version-appropriate code pages;
punctuation support alone does not select this layout. Non-CJK faces, missing
or invalid metrics, non-scalable faces and variable fonts retain the existing
ascent. Malformed or unsupported explicit BASE declarations retain the existing
ascent; a failed parse is not treated as an absent declaration. No font or game
names select a baseline.
Reported ascent, bitmap origins, draw rectangles and decoration positions use
the same value. Nominal EM size, glyph pixels, advances and caller clipping are
preserved. EM-box metrics are layout boundaries, not a guarantee that every
glyph's ink fits a caller's rectangle. See the [OpenType baseline contract](https://learn.microsoft.com/en-us/typography/opentype/spec/baselinetags)
and [FreeType glyph metrics](https://freetype.org/freetype2/docs/glyphs/glyphs-3.html).

## SQLite storage

The Web Sqlite plugin uses real SQLite queries, bindings, transactions and
statements. Read-only original databases are read through storage callbacks;
writable databases persist in the virtual user overlay. Writable snapshots are
limited to 64 MiB per database and remain subject to the host save bundle limit.
The original logical filename identifies one database: CREATE publishes a real
zero-length file, commits publish raw SQLite bytes at that path, and journal or
database deletion emits the same native unlink/tombstone used by `Storages`.
The TVP media manager converts canonical `file://` storage names to native VLFS
paths before publication. Archive member names retain their original read
streams; they are never passed to VLFS as writable filenames. A read/write
request for an existing archive database falls back to read-only with truthful
SQLite output flags. Queries remain available, while writes and archive
deletion report `SQLITE_READONLY`. See the [SQLite VFS contract](https://www.sqlite.org/c3ref/vfs.html).

Open SQLite files share their current object, but an Environment keeps only weak
references to closed files. After all database connections and statements for a
path are closed, a native delete/recreate or a newly imported native overlay is
visible to the next open even if another database retains that Environment.
Unlinking a database while it is still open is not a supported reset protocol;
SQLite [warns that unlinking an open database can corrupt it](https://www.sqlite.org/howtocorrupt.html#unlinking_or_renaming_a_database_file_while_in_use).
Close every connection and statement before deleting it. WAL remains explicitly
unsupported.

This file layout belongs to the host's `kirikiri-game-save-v1` recovery contract
(`kirikiri-game-save-v1-storage-v1` after its public storage wrapper). The previous
hidden `/savedata/sqlite/<hex>.bin` envelope is neither read nor migrated. This
keeps the existing native GAME_SAVE format; no separate format generation is
introduced for the development-only sidecar layout. Full raw database bytes and
native tombstones must be imported before the new instance starts; merely retaining SQL in a live
connection does not constitute persistence. The standalone real-SQLite and
production-VLFS tests cover this storage boundary, not every game's save UI or
an engine/browser restart.

## Native GAME_SAVE observation

`_krkr2_host_observe_game_saves()` installs a transparent observer on the current
KAG `saveBookMarkToFile` method between drained engine ticks. It returns 0 while
startup or a tick is not ready, 1 when observing native serializers and any
available KAG method, -3 on member or
installation failure, and -5 on a lookup/installation exception.

The original closure, receiver, arguments, return value and exception are
preserved. Its original engine-tick JSPI boundary also owns any cold I/O. Only
the outermost observed call emits `Module.onGameSave`: `{phase: 'begin', id}`
followed by `{phase: 'end', id, success}`. IDs are positive exact integers.
An integer true or normal void return is only a non-rejection witness; the host
must also observe real nonempty writes inside that call before identifying
progress. False, dispatch failure and exceptions report an unsuccessful end.
Notifications never request a native save or load. Teardown detaches the
observer only after the active native stack has drained.

Independent frameworks can persist an image-backed structured file through
`Array.saveStruct` or `Dictionary.saveStruct`. A Web-only scope observes a
nonempty string filename with an explicit positive `o` offset when the existing
54-byte BMP header, uncompressed 24/32-bit pixel geometry and complete physical
file size all agree with that offset. The original serializer appends data and
closes its stream before successful completion is reported. No filename,
extension, script class or object name selects this path. Nested KAG/serializer
calls share one transaction; any nested failure makes it unsuccessful.
Settings-only serialization provides no progress witness. Existing bridge tests
cover transaction behavior; actual native files cover the BMP classification in
product acceptance, without a separate standalone classifier fixture.

Native bookmark files can contain both thumbnail pixels and serialized game
data. Their names and suffixes do not identify progress. The host transfers
complete final files and related user overlays, then imports them before normal
startup. The player selects the native LOAD slot. No automatic scenario jump is
required. The [KAG save contract](https://krkrz.github.io/krkr2doc/kag3doc/contents/SaveData.html)
does not represent every live UI or VM execution state.

Before exporting a confirmed native save, the host drains the frame and invokes
the JSPI export `_krkr2_host_flush_game_saves()`. It calls the game's registered
`saveSystemVariables` closure to persist late save-menu bookkeeping as native
files. It preserves a bound receiver and emits no progress notification. An
absent KAG receiver or optional callback returns 0, completion returns 1, and errors return a
negative status that stops export. The host retains the suspended operation
through cancellation. Normal native process exit already owns final persistence
and does not reenter the script engine to flush after exit.

## Unhandled script failures

After the game's exception handler declines an exception, the Web core sends
bounded message, script, line and trace fields to `Module.onScriptException`.
This includes script errors caught by application initialization and frame
dispatch outside the ordinary event wrappers. Those Web boundaries preserve the
original thrown script value when invoking the registered exception handler.
An embedding runtime can report a fatal failure after the native callback has
returned. Ordinary dialogs and exceptions handled by the game remain ordinary
game behavior; localized alert text is not a failure protocol.
