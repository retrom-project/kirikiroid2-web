#include "cpp/core/environ/web/WindowPresentation.h"
#include <cassert>
#include <cmath>
#include <initializer_list>

static void near(double actual, double expected) {
    assert(std::abs(actual - expected) < 0.001);
}

int main() {
    using namespace KirikiriWeb;
    FullScreenWindow state;
    assert(!state.enabled());
    assert(state.setEnabled(true, {1440, 810}));
    assert(state.enabled());
    assert(!state.setEnabled(true, {960, 600}));
    assert(state.normalSize().width == 1440);
    assert(state.setEnabled(false, {960, 600}));
    assert(!state.enabled());
    assert(state.normalSize().height == 810);
    state.setEnabled(true, {1440, 810});
    state.resizeNormal({1280, 720});
    state.setEnabled(false, {960, 600});
    assert(state.normalSize().width == 1280);

    // The game requests fullscreen after selecting a fractional window zoom.
    // Fit the primary paint area to the virtual display, independently of that
    // window zoom; use this same node transform for input and output.
    for(const auto view : {WindowSize{960, 600}, WindowSize{1920, 1080},
                           WindowSize{960, 1969}, WindowSize{2560, 1440}}) {
        const auto fit = fitFullScreen(view, {1920, 1080});
        const double scale = double(fit.numer) / fit.denom;
        near(1920 * scale + 2 * fit.x, view.width);
        near(1080 * scale + 2 * fit.y, view.height);
        assert(fit.x >= 0 && fit.y >= 0);
        assert(fit.x < 0.001 || fit.y < 0.001);
        for(const auto p : {WindowSize{0, 0}, WindowSize{1920, 1080},
                             WindowSize{960, 540}, WindowSize{1820, 990}}) {
            const double screenX = fit.x + p.width * scale;
            const double screenY = fit.y + p.height * scale;
            near((screenX - fit.x) / scale, p.width);
            near((screenY - fit.y) / scale, p.height);
        }
    }
    const auto fit = fitFullScreen({960, 600}, {1920, 1080});
    assert(fit.numer == 1 && fit.denom == 2);
    near(fit.x, 0); near(fit.y, 30);
    assert(!fitFullScreen({0, 600}, {1920, 1080}).valid());
}
