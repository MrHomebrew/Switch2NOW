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

## Fourth pass (display modes in the Minus+Plus menu + quit to hbmenu)
The transient UP panel is useful, but the same display modes are now also
reachable from the existing **MINUS + PLUS** stream menu:

- New **Display mode >** action opens a submenu listing all six modes.
  UP/DOWN or LEFT/RIGHT moves the highlight, **A** applies it, **B** goes back.
  The applied mode is marked with `*`.
- New **Exit to hbmenu (quit app)** action stops the stream cleanly and calls
  `brls::Application::quit()`, so the process ends and hbmenu takes over. This
  is different from **Exit stream**, which only returns to the in-app launcher.
- New two-button chord **L + R** quits to hbmenu directly, without opening the
  menu. It is distinct from `MINUS + R3` (keyboard command layer) and
  `MINUS + ZL + ZR` (exit stream only).

`brls::Application::setGlobalQuit(false)` is unchanged; the explicit `quit()`
call still works because the main loop checks `quitRequested` directly.

## Fifth pass (incremental ZOOM - / ZOOM +)
On-console testing showed that only the ZOOM entries changed the picture; FIT,
FILL and STRETCH appeared to do nothing. The new log format confirmed why:
`egl=1920x1080`, `frame=1280x720` and every recorded viewport was `0,0 1920x1080`
for modes 0/1/2. Both the decoded frame and the EGL surface are exactly 16:9, so
contain, cover and stretch are mathematically the same full-screen rectangle.
This was expected behaviour, not a stuck input.

The fixed modes were replaced with a continuous zoom percentage:

- Range **50%–300%**, step **10%**, default **100%**.
- **Below 100%**: the fitted frame is scaled down and centred, so black borders
  become visible (a real zoom-out even on a 16:9 stream).
- **Above 100%**: centre crop of the UV window (zoom-in).
- The renderer always contains the frame inside the EGL surface first, so a
  future non-16:9 stream still letterboxes correctly.

Controls:

- Hold **UP** ~400 ms: transient panel showing the current percentage plus
  **LEFT = ZOOM -**, **RIGHT = ZOOM +**, **A = RESET 100%** (also tappable).
- **MINUS + PLUS** menu -> **Display zoom >**: submenu with ZOOM -, ZOOM + and
  RESET 100%. UP/DOWN moves the highlight, A applies and keeps the submenu open
  for repeated presses, LEFT/RIGHT also zoom directly, B goes back.
- `viewport_fix.log` now records `zoom=N%` whenever the value changes.

The **Exit to hbmenu** action and the **L + R** quit chord from the fourth pass
are unchanged.

## Status
Unofficial community patch. Not reviewed or released by the upstream author.
Build artifacts come from this fork's `switch2-build.yml` workflow.
