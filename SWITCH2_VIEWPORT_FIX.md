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

## Status
Unofficial community patch. Not reviewed or released by the upstream author.
Build artifacts come from this fork's `switch2-build.yml` workflow.
