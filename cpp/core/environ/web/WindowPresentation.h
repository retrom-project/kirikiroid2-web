#pragma once
#include <cstdint>
#include <numeric>

namespace KirikiriWeb {

struct WindowSize { int width, height; };

// Web has one virtual display inside the canvas. Its fullscreen state does
// not request browser fullscreen or change the user's windowed zoom setting.
class FullScreenWindow {
    bool enabled_ = false;
    WindowSize normal_{0, 0};
public:
    bool enabled() const { return enabled_; }
    bool setEnabled(bool value, WindowSize current) {
        if(value == enabled_) return false;
        if(value) normal_ = current;
        enabled_ = value;
        return true;
    }
    void resizeNormal(WindowSize size) { normal_ = size; }
    WindowSize normalSize() const { return normal_; }
};

struct FullScreenFit {
    int numer = 0, denom = 1;
    double x = 0, y = 0;
    bool valid() const { return numer > 0 && denom > 0; }
};

inline FullScreenFit fitFullScreen(WindowSize view, WindowSize paint) {
    if(view.width <= 0 || view.height <= 0 || paint.width <= 0 || paint.height <= 0)
        return {};
    const bool widthFits = std::int64_t(view.width) * paint.height <=
                           std::int64_t(view.height) * paint.width;
    int numer = widthFits ? view.width : view.height;
    int denom = widthFits ? paint.width : paint.height;
    const int divisor = std::gcd(numer, denom);
    numer /= divisor; denom /= divisor;
    const double scale = double(numer) / denom;
    return {numer, denom, (view.width - paint.width * scale) / 2,
                          (view.height - paint.height * scale) / 2};
}

} // namespace KirikiriWeb
