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

## Status
Unofficial community patch. Not reviewed or released by the upstream author.
Build artifacts come from this fork's `switch2-build.yml` workflow.
