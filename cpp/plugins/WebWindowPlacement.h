#pragma once

namespace WindowExWeb {

// Browser-only virtual, borderless window placement, in the same logical
// coordinates as WebDisplayMonitors. Browser fullscreen and backing pixels do
// not change this state. This is not a four-reference native reconstruction.
struct Rect {
    int x, y, width, height;
    bool operator==(const Rect &other) const {
        return x == other.x && y == other.y && width == other.width &&
            height == other.height;
    }
};

class Placement {
    bool maximized_ = false;
    Rect normal_{0, 0, 0, 0};

public:
    bool isMaximized(const Rect &current, const Rect &work) {
        // A script can resize its virtual window independently of windowEx.
        // Report the actual resulting area instead of retaining a stale flag.
        if(maximized_ && !(current == work)) maximized_ = false;
        return maximized_;
    }

    Rect setMaximized(bool value, const Rect &current, const Rect &work) {
        const bool wasMaximized = isMaximized(current, work);
        if(value) {
            if(!wasMaximized) normal_ = current;
            maximized_ = true;
            return work;
        }
        maximized_ = false;
        return wasMaximized ? normal_ : current;
    }

    Rect normalRect(const Rect &current) const {
        return maximized_ ? normal_ : current;
    }
};

} // namespace WindowExWeb
