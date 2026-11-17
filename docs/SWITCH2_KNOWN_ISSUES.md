# Switch2NOW — Known Issues & Triage Log (`v1.3.0`)

Status: **Confirmed cross-platform (affects both Switch 1 & Switch 2); ABYA workaround verified by `@davidkalil10` — queued for code fixes**

## Summary of Switch 1 vs Switch 2 Comparison
- Confirmed by `@davidkalil10`: **Native login failure, empty library (`0 games`) / `ENTITLEMENT_FAILURE_STATUS` on the default `NVIDIA` provider, and post-login UI freeze also occur on Switch 1 (`SwitchNOW.nro`).** These are shared upstream/GFN auth/UI bugs, not Switch 2-specific regressions.
- Confirmed workaround: **Switching the login provider to `ABYA`** completes login properly and lists the user's owned library games (currently testing in-game queue/stream playback).

---

## Issue 1 — Native `NVIDIA` Login Failure & Cropped QR Code Dialog
- **Observed behavior (Switch 1 & Switch 2, `v1.3.0`):**
  1. Direct username/password sign-in with the default `NVIDIA` provider fails (NVIDIA 2FA / external verification flow changes).
  2. Falling back to **QR Code login** works when scanned, **but the rendered QR code is visually clipped/cut off at the bottom**.
- **Root cause found in code:**
  - `app/src/qr_login_dialog.cpp` (`QrLoginDialog::draw`, lines 714-720) hardcodes `cell_size = 6.0f` and shifts `start_y` down by `+ 82.0f` (`start_y = y + (height - qr_px_size) / 2.0f + 82.0f`), so long OAuth URLs push the bottom of the QR square outside the `720p` dialog bounds. Needs responsive module sizing clamped to the available box height/width.

## Issue 2 — Empty Library & `ENTITLEMENT_FAILURE_STATUS` on `NVIDIA` Provider (ABYA Workaround)
- **Observed behavior (Switch 1 & Switch 2, `v1.3.0`):**
  1. Using the default `NVIDIA` provider via QR code leaves `My Library` empty (`Loaded 0 games`, `Connected as David Kalil | Unknown tier`, `Library sync incomplete. Refresh to retry.`).
  2. Attempting to launch a game from `Store` fails with:
     `StartSession failed: HTTP 403, statusCode=18, statusDescription=ENTITLEMENT_FAILURE_STATUS 8A910006, unifiedErrorCode=1970208762`.
- **Verified workaround:**
  - Choosing the **ABYA** provider in `ProvidersTab` signs in cleanly and populates the owned game library.
- **Evidence photos:**
  - `media_1794911087271_90ccf853.jpg` (`Unknown tier`, `Loaded 0 games`, `Library sync incomplete`)
  - `media_1794911166257_b1a07358.jpg` (`Far Cry 6` `ENTITLEMENT_FAILURE_STATUS 8A910006`)
- **Note on Switch 2 UI styling:** Also visible in the photos: light/white fill on certain secondary/focused Borealis GLES3 buttons (`Sign out`, `Cancel session`). Worth checking `button.cpp` / theme colors later.

## Issue 3 — UI Freeze Immediately After Completing Login
- **Observed behavior (Switch 1 & Switch 2, `v1.3.0`):**
  - Right after login completes, the UI freezes and stops accepting input to advance. Closing and reopening the homebrew recovers cleanly with the saved session active.
- **Suspected code paths to fix:**
  - `app/src/qr_login_dialog.cpp` (`CompleteLogin` lines 385-398 calls `brls::Application::popActivity()` which destroys `QrLoginDialog` -> `~QrLoginDialog()` joins `worker_` on the UI thread, and `brls::sync` in `StartQrLoginProcess` line 668 captures raw `this`).
  - `app/src/providers_tab.cpp` (`OpenProviderDialog` lines 471-480 schedules `brls::sync([this]() { on_success_(); ClosePicker(); })` without guarding `alive_` or activity-stack transition timing while `QrLoginDialog` pops and `LibraryTab::ReloadLibrary()` starts).
