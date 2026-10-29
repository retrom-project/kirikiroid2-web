
#include "ncbind.hpp"

#define NCB_MODULE_NAME TJS_W("win32dialog.dll")

static void InitPlugin_WIN32Dialog() {
    TVPExecuteScript(TJS_W("class WIN32Dialog {") TJS_W(
        "	function messageBox(message, caption, type) {return "
        "!System.inform(message, caption, 2);}") TJS_W("}"));
#ifdef __EMSCRIPTEN__
    static const tjs_char styles[] =
#include "win32dialog_web_constants.inc"
        ;
    TVPExecuteScript(styles);
    // Portable dialog libraries bind these template types when defining their
    // classes, before any native window is requested. Keep the data model on
    // Web even though a Win32 HWND cannot be created in the browser.
    TVPExecuteScript(TJS_W(R"TJS(
WIN32Dialog.WIN32Dialog = function(target) { _eventTarget = target; };
WIN32Dialog.finalize = function() { _eventTarget = null; };
WIN32Dialog._eventTarget = null;
WIN32Dialog.modeless = false;
// No Windows common-control registration occurs on this platform.
WIN32Dialog.initCommonControlsEx = function(flags) { return false; };
class WIN32DialogHeader {
    var helpID = 0, exStyle = 0, style = 0, dlgItems = 0;
    var x = 0, y = 0, cx = 0, cy = 0;
    var menu = "", windowClass = "", title = "";
    var pointSize = 0, weight = 0, italic = 0, charset = 0, typeFace = "";
    function store(source) {
        var fields = [];
        fields.assign(source);
        for(var i = 0; i < fields.count; i += 2)
            this[fields[i]] = fields[i + 1];
    }
}
class WIN32DialogItems {
    var helpID = 0, exStyle = 0, style = 0, id = 0;
    var x = 0, y = 0, cx = 0, cy = 0;
    var windowClass = "", title = "";
    function store(source) {
        var fields = [];
        fields.assign(source);
        for(var i = 0; i < fields.count; i += 2)
            this[fields[i]] = fields[i + 1];
    }
}
WIN32Dialog.Header = WIN32DialogHeader;
WIN32Dialog.Items = WIN32DialogItems;
delete global.WIN32DialogHeader;
delete global.WIN32DialogItems;
)TJS"));
#endif
}

NCB_PRE_REGIST_CALLBACK(InitPlugin_WIN32Dialog);
