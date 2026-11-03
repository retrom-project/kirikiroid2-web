#pragma once

namespace TJS {
class iTJSDispatch2;
class tTJSNativeClass;
}

// Web implementation of the newer Z drawing API. Registration does not alter
// the existing Layer renderer; a Window activates this path through canvas.
void TVPRegisterWebCanvasClasses(TJS::iTJSDispatch2 *global,
                                 TJS::tTJSNativeClass *windowClass);
