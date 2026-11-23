#pragma once

#include <atomic>

namespace opennow::video_display {

// Display geometry modes for the Switch 2 stream renderer. FIT is the default:
// the whole decoded frame stays inside the physical screen frame (letterbox /
// pillarbox instead of cropping the game).
enum class Mode : int {
    Fit = 0,
    Fill,
    Stretch,
    Zoom125,
    Zoom150,
    Zoom200,
};

inline constexpr int kModeCount = 6;

inline std::atomic<int> g_mode{static_cast<int>(Mode::Fit)};

inline const char* modeLabel(int mode)
{
    switch (mode) {
        case static_cast<int>(Mode::Fit):
            return "FIT (inside frame)";
        case static_cast<int>(Mode::Fill):
            return "FILL (crop edges)";
        case static_cast<int>(Mode::Stretch):
            return "STRETCH";
        case static_cast<int>(Mode::Zoom125):
            return "ZOOM 1.25x";
        case static_cast<int>(Mode::Zoom150):
            return "ZOOM 1.5x";
        case static_cast<int>(Mode::Zoom200):
            return "ZOOM 2x";
        default:
            return "FIT (inside frame)";
    }
}

inline float modeZoom(int mode)
{
    switch (mode) {
        case static_cast<int>(Mode::Zoom125):
            return 1.25f;
        case static_cast<int>(Mode::Zoom150):
            return 1.5f;
        case static_cast<int>(Mode::Zoom200):
            return 2.0f;
        default:
            return 1.0f;
    }
}

} // namespace opennow::video_display
