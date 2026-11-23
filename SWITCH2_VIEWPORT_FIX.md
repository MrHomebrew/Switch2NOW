# Switch 2 UI viewport fix (local fork patch)

Base: `davidkalil10/Switch2NOW` v1.3.0 (commit `2c90cfe6`).

## Problem
After a GeForce NOW stream ends, the whole Switch2NOW UI is rendered into a
smaller viewport (typically 1280x720) until the app is closed and relaunched.
Dock/undock does not recover it.

## Cause
`app/src/stream/OpenGL/GLVideoRenderer.cpp:617` calls
`glViewport(0, 0, width, height)` with the stream view size on every rendered
video frame. On Switch 2, Borealis restores the viewport only inside
`SwitchVideoContext::resetFramebuffer()`, which `beginFrame()` calls only when a
display-resolution change event is observed. A missed event or the end of the
stream therefore leaves the shrunken viewport active for the UI.

## Patch
`extern/borealis/library/lib/platforms/switch/switch_video.cpp`
(`SwitchVideoContext::beginFrame`, inside `#ifdef __SWITCH2__`) now restores the
viewport to the current display resolution on **every** frame before NanoVG
starts drawing, with a fallback to the cached framebuffer size.

This is a one-behaviour change scoped to the Switch 2 OpenGL path. No gameplay,
network, authentication or decoding logic was modified.

## Second pass (video still small)
The first patch restored the UI after a stream, but on-console testing showed
the video itself still occupied only the top-left part of the display. Cause:
`GLVideoRenderer::drawLatest()` sets `glViewport(0, 0, width, height)` using the
**logical** Borealis view size (1280x720), while the Switch 2 EGL surface is the
full display resolution (1920x1080 docked).

The video renderer now also uses the real display resolution for its GL
viewport on `__SWITCH2__`, while keeping the logical view size only for the 16:9
UV aspect calculation. A one-shot `sdmc:/switch/SwitchNOW/viewport_fix.log`
records the view, display and chosen viewport sizes for verification.

## Third pass (fit inside the screen + transient display menu)
Testing of the second patch showed the video filling the screen but appearing
zoomed/cropped. The log recorded `view=1280x720 display=1920x1080` with a
`1280x720` decoded frame, so all aspect ratios were 16:9 and the old cover UV
math could not explain the crop. The likely cause is that
`appletGetDefaultDisplayResolution()` can report a resolution different from the
actual EGL surface after a dock change, so a viewport derived from it can be
larger than the surface.

The renderer now queries `eglQuerySurface()` for the authoritative physical
size, restores the full surface viewport in `SwitchVideoContext::beginFrame()`
from the same source, and supports a small set of modes:

- `FIT` (default): centred, aspect-correct viewport; the whole frame stays
  inside the screen with black bars on unused edges.
- `FILL`: cover mode, crops the longer edge.
- `STRETCH`: full frame stretched to the full surface.
- `ZOOM 1.25x`, `1.5x`, `2x`: centre crop at the selected zoom.

Hold **UP** on the controller for ~400 ms to reveal a small top-right panel.
Move the highlight with **LEFT/RIGHT**, confirm with **A**, or tap a row on the
touchscreen. The panel closes automatically 3 seconds after the last input;
there is no permanent on-screen control. The active mode is written to
`sdmc:/switch/SwitchNOW/viewport_fix.log` whenever it changes.

## Status
Unofficial community patch. Not reviewed or released by the upstream author.
Build artifacts come from this fork's `switch2-build.yml` workflow.
