# Web Win32 dialog geometry and windows

## Evidence before implementation

The four authoritative binaries and their IDBs were independently opened and
checked in this investigation. Their complete WIN32Dialog initializers were
freshly decompiled using native IDA MCP:

| Reference | Initializer | Original UTF-16 script |
| --- | --- | --- |
| Android arm64-v8a | 0x60f79c | 0x14cdf6a |
| Android armabi-v7a | 0x5269c8 | 0xd80000 |
| iOS arm64 | 0x1002a222c | 0x101972d36 |
| iOS armv7 | 0x2a7078 | 0x17650e2 |

All four complete literals were read from native IDA memory and agree:

```text
class WIN32Dialog {
    function messageBox(message, caption, type) {
        return !System.inform(message, caption, 2);
    }
}
```

Shared initializer pseudocode: construct the same UTF-16 TJS string; execute
it in the global context; destroy the temporary string. Android64 inlines its
conditional string release; Android32 and both iOS builds outline destruction.
Android uses stack-canary scaffolding; iOS armv7 additionally spills the SjLj
callee-saved floating registers. Pointer widths and runtime helpers differ;
there is no extra geometry/control registration in any of these initializers.

Independent UTF-16 searches found the module and the exact uppercase class
literal in every binary, but no `getBaseUnits`. Source registration and an
actual restored complete-package run independently confirm the same gap:
WIN32Dialog exists, its geometry method does not. This is an explicit Web
platform extension, not a reconstruction inferred from a negative grep.

## Supplemental supplied Windows protocol

The supplied original Windows DLL is PE32, 305664 bytes, SHA-256
`147238a710cf25910fdcb718861547e7df69984b3cd1a8a5ce1c130a4fc1d3ef`.
Its class registration at 0x1000b120 contains the literal `getBaseUnits` at
0x10031938. The real implementation at 0x10006370 was independently located
through its GetDialogBaseUnits import and fully decompiled. Registration's
fluent helper expression evaluation is shifted in Hex-Rays output; a nearby
binder must not be assigned to a name just from statement adjacency.

The method creates an ordinary native TJS Dictionary, calls the operating
system GetDialogBaseUnits, writes the low 16-bit horizontal metric as `h` and
the high 16-bit vertical metric as `v`, publishes the dictionary, and releases
the local reference. It does not require an open dialog or accept a font.
The adjacent mapRect implementation at 0x10006490 instead requires an open
window, reads left/top/right/bottom, invokes MapDialogRect and returns a new
Dictionary. The two methods must not be confused.

The actual original saved state reaches the no-argument getBaseUnits call
while constructing a modeless dialog template, before opening its window.
Its script uses h and v for the 4/8 dialog-unit conversions. The existing
complete package and its unmodified saved state provide runtime validation;
no test fixture or modified game asset is introduced.

## Browser platform boundary and local correspondence

A browser has neither User32 nor the Windows system font. The Web method
measures its actual CSS caption font using a temporary, invisible laid-out
alphabet element. The measured average glyph width and line height are the
Web system-font base units. It returns real positive integral h/v values via
TJSCreateDictionaryObject and native PropSet/reference ownership, matching the
public result contract. The same font and units must be used by subsequent
Web dialog layout; Windows pixel-identical font metrics are not claimed.

The Emscripten-only NCB attachment adds this real geometry method to the
existing class as an instance member, so TJS-derived dialogs copy it during
construction. A class-only STATICMEMBER is insufficient: a real restored
dialog calls this method through its instance. The four-reference common initializer and non-Web behavior
remain unchanged. No game identity, storage hash, saved setting or callback is
used to select the implementation.

Public system API semantics:
[GetDialogBaseUnits](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getdialogbaseunits)
and [MapDialogRect](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-mapdialogrect).

The original restored instance first passed getBaseUnits and exposed the next
template dependency, FW_NORMAL. The browser metadata must also expose the public GDI
font-weight constants used by dialog templates. Their integer values are
specified by [CreateFontW](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createfontw).
They are metadata only; weight 400 denotes normal text, not a placeholder
metric or a saved-state rewrite.

## Browser modeless-window backend: additional evidence before implementation

The four mobile initializers above expose only messageBox; they cannot provide
User32 windows, controls, handles, resource templates or GDI font metrics.
Their matching absence and common initializer are the explicit platform boundary
for this browser extension. The supplied original Windows DLL and the public
Win32 API determine the additional protocol; they do not replace the four
authoritative mobile references with a fabricated mobile implementation.

Fresh supplemental decompilations map makeTemplate to 0x10009bd0 (validates a
Header followed by Items, serializes/copies them before the temporary script
objects are invalidated); open raw wrapper 0x10006850 and body 0x10006ad0
(store script target before creation, modeless CreateDialog versus modal
DialogBox); close 0x100069b0 (DestroyWindow for modeless and clear handle).
DialogFunc 0x10006e90 stores HWND before delivering onInit, then forwards
WM_COMMAND and WM_SIZE as three integer message parameters through 0x10004110.
The ordinary virtual defaults at 0x10006ac0/0x10006ab0 return false; preserving
these empty default handlers is part of the real protocol.

Control access always requires an open dialog. ID zero denotes the dialog
itself: getItem 0x10004cb0, GetWindowLong 0x10004d80, SetWindowLong 0x10004df0,
GetText 0x10004fb0, Enable 0x10005120, IsEnabled 0x100051a0, Focus 0x10005210,
left/top/width/height 0x10005280/0x10005310/0x100053a0/0x10005440,
position/size 0x100054e0/0x10005550. sendItemMessage 0x10006170 accepts a
string or integer lParam and delegates at 0x100062c0 to SendDlgItemMessage,
or SendMessage for ID zero. Window position/size 0x100056f0/0x10005720
act only while open. Geometry getters 0x10005e20/0x10005e70 return real
window coordinates/extents or zero while closed. Placement at 0x10005770
publishes flags, showCmd, minLeft/minTop, maxLeft/maxTop and normal rectangle
fields only when GetWindowPlacement succeeds.

Common browser extension pseudocode:

1. The existing TJS Header/Items store metadata. makeTemplate snapshots their
   declared primitive fields; temporary objects are not retained after return.
2. A per-script-instance NCB adaptor owns one browser dialog state. Opening a
   modeless dialog builds visible semantic controls from the snapshot, measures
   its actual font, maps dialog units, sets its handle, then synchronously calls
   script onInit while still inside the ordinary JSPI script invocation.
3. Text, style, enabled/focus state, item/window geometry and supported edit/
   button messages act on the actual DOM controls. Returned opaque handles
   identify those controls; no Windows pointer is dereferenced.
4. Browser commands and resize observations enqueue ordinary TVP events.
   Script callbacks run on the engine event loop and may suspend for storage.
   C++ never calls a script from an arbitrary browser callback while its
   interpreter is suspended.
5. Close removes the dialog and observation/listeners; native-adaptor
   destruction disposes its state. Stale browser notifications are ignored
   using the live-instance lookup. No strong self-reference creates a native
   object/script ownership cycle.

Browser-specific representation: a DOM dialog in the engine frame substitutes
for HWND/User32. Public control atoms follow
[DLGITEMTEMPLATE](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-dlgitemtemplate);
the original DLL's registration machine bytes at 0x1000d29f additionally prove
BUTTON/EDIT/STATIC/LISTBOX/SCROLLBAR/COMBOBOX = 0x80..0x85. On-demand geometry,
text and styles come from the current visible controls. Unsupported native
resource loading, GDI handles and messages must raise explicit errors; they
must not be represented by success-shaped dummy results. This change initially
covers modeless text/control dialogs, not arbitrary Windows DLL execution or
all common controls. Modal-window support requires its own asynchronous wait
boundary and is not silently emulated as modeless.

The original supplied scripts first passed getBaseUnits, then FW_NORMAL, then
EDIT in successive complete-package runs with the same saved-state hash. They
prove these are actual template dependencies. The declared template fields
and numeric control IDs are consumed unchanged. No game name, hash, logMode
rewrite, source patch or special-case script path selects this backend.

The first actual browser window opens and lays out three controls, then the
original script reads icon. The supplemental icon binder 0x100174a0 identifies
its getter 0x10008a20 as a copy of the stored TJS variant, and setter 0x10008910
resolves an icon, clears the old variant, conditionally copies the supplied
variant, then sends WM_SETICON when open. The browser backend has no native
GDI icon handle. Preserve its initially void icon state and implement clearing
that visible window icon; reject nonempty native icon objects explicitly.

The real saved desktop position is left=0, top=1080, width=1920, height=540;
the browser frame is 1440 by 900. Native coordinates are not a rendering
transform error: the new dialog really lay outside that frame. The browser
window-manager boundary constrains each visible dialog's position to its own
viewport after creation, move, resize and drag. Geometry getters expose the
resulting visible rectangle. Original saved bytes are not rewritten to clear
logMode or discard the old position; new ordinary UI state can be saved by the
game through its existing save flow.

The real close-button WM_COMMAND reaches the script's defaultCommand handler;
its IDOK and IDCANCEL dependencies are the public standard result/control IDs
1 and 2. These IDs are shared across dialog libraries, not per-game aliases.

Validation with the unmodified original native saved state now reaches a real
modeless window with three controls. Its actual close command dismisses it,
the normal title appears, and the native quick-load menu finds the old progress.
Selecting that native slot and confirming loads its Chinese story text; mouse
and Enter advance to subsequent lines. The source save payload remains intact.
This validates the used modeless text/control path; arbitrary modal/resource/
common-control and native GDI support remains outside this browser extension.
