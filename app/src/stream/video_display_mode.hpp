#pragma once

#include <algorithm>
#include <atomic>
#include <string>

namespace opennow::video_display {

// Continuous centre zoom for the Switch 2 stream renderer.
//
// 100 keeps the decoded frame fitted inside the physical screen (a 1280x720
// frame on a 1920x1080 surface therefore fills it exactly with no bars).
// Below 100 the fitted frame is scaled down and centred, so black borders
// appear. Above 100 the frame is centre-cropped, i.e. zoomed in.
//
// A fixed list of FIT/FILL/STRETCH presets was tried first, but when the
// stream and the surface have the same aspect ratio (the normal 16:9 case)
// those three modes are mathematically identical, so only their ZOOM entries
// produced a visible change. Incremental zoom is both clearer and useful in
// that case.
inline std::atomic<int> g_zoom_percent{100};

inline constexpr int kZoomMin = 50;
inline constexpr int kZoomMax = 300;
inline constexpr int kZoomStep = 10;

inline int clampZoom(int percent)
{
    return std::max(kZoomMin, std::min(kZoomMax, percent));
}

inline int zoomPercent()
{
    return g_zoom_percent.load(std::memory_order_relaxed);
}

inline int setZoom(int percent)
{
    const int next = clampZoom(percent);
    g_zoom_percent.store(next, std::memory_order_relaxed);
    return next;
}

inline int adjustZoom(int delta)
{
    return setZoom(zoomPercent() + delta);
}

inline std::string zoomLabel()
{
    return std::to_string(zoomPercent()) + "%";
}

} // namespace opennow::video_display
