#include <cassert>
#include "cpp/plugins/WebWindowPlacement.h"

using WindowExWeb::Placement;
using WindowExWeb::Rect;

int main() {
    const Rect normal{0, 0, 1280, 720}, work{0, 0, 2048, 1280};
    Placement first, second;
    assert(!first.isMaximized(normal, work));
    assert(!second.isMaximized(normal, work));
    assert(first.setMaximized(true, normal, work) == work);
    assert(first.isMaximized(work, work));
    assert(!second.isMaximized(normal, work));
    assert(first.normalRect(work) == normal);
    // Repeated maximize must retain the original windowed size.
    assert(first.setMaximized(true, work, work) == work);
    assert(first.setMaximized(false, work, work) == normal);
    assert(!first.isMaximized(normal, work));
    // The client size is the virtual borderless window size. Explicit resizing
    // while maximized restores windowed state instead of lying about its area.
    first.setMaximized(true, normal, work);
    const Rect resized{0, 0, 1400, 900};
    assert(!first.isMaximized(resized, work));
    assert(first.normalRect(resized) == resized);
    assert(first.setMaximized(false, resized, work) == resized);
    // A logical display change also cannot leave a stale maximized flag. Host
    // fullscreen/backing-pixel changes do not alter these logical coordinates.
    first.setMaximized(true, normal, work);
    const Rect nextWork{0, 0, 1920, 1080};
    assert(!first.isMaximized(work, nextWork));
    assert(first.setMaximized(true, work, nextWork) == nextWork);
    assert(first.isMaximized(nextWork, nextWork));
    assert(first.setMaximized(false, nextWork, nextWork) == work);
}
