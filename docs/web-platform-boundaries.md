# Web window, fonts and native saves

These are explicit browser platform boundaries, implemented from observed Web
behavior and public contracts. They do not claim reconstruction of the four
reference binaries. The TJS VM and game scripts are unchanged.

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

This file layout belongs to the host's `kirikiri-game-save-v2` recovery contract
(`kirikiri-game-save-v2-storage-v1` after its public storage wrapper). The previous
hidden `/savedata/sqlite/<hex>.bin` envelope is neither read nor migrated. Old
save files are retained, but a core fingerprint warning alone cannot make that
previous layout compatible. Full raw database bytes and native tombstones must
be imported before the new instance starts; merely retaining SQL in a live
connection does not constitute persistence. The standalone real-SQLite and
production-VLFS tests cover this storage boundary, not every game's save UI or
an engine/browser restart.

## Native GAME_SAVE observation

`_krkr2_host_observe_game_saves()` installs a transparent observer on the current
KAG `saveBookMarkToFile` method between drained engine ticks. It returns 0 while
startup, a tick or the method is not ready, 1 when observing, -3 on member or
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
absent optional callback returns 0, completion returns 1, and errors return a
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
