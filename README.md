<p align="center">
  <img src="docs/branding/switchnow-hero-2048.png" width="560" alt="SwitchNOW logo">
</p>

# SwitchNOW (Switch 1 & Switch 2)

Original project developed and maintained by **Blade-Punisher**.  
Nintendo Switch 2 port (`ounce-devkit` / `OunceVK` + hardware H.264 decoding) and Brazilian Portuguese (`pt-BR`) localization by **David Kalil Braga ([@davidkalil10](https://github.com/davidkalil10))**.

SwitchNOW is a native Nintendo Switch / Nintendo Switch 2 homebrew client for GeForce NOW. It is
distributed as an `.nro` (compatible with `hbmenu` / `ounce-hbmenu` and `kaiHEN` on Switch 2) and combines a controller-first Borealis launcher with
native WebRTC signaling, H.264 video, Opus audio and low-latency input delivery.

> [!IMPORTANT]
> SwitchNOW is an unofficial community project. It is not affiliated with or
> endorsed by NVIDIA or Nintendo. A valid GeForce NOW account is required, and
> game availability, session duration and queues depend on the selected NVIDIA
> membership and region.

## Main features

- Browse the GeForce NOW catalog and the games linked to the current account.
- Cover-art cache, incremental catalog paging, search, filters and sorting.
- Per-game store selection for titles available through multiple storefronts.
- Persistent multi-account sign-in, token refresh and optional encrypted local
  password storage for quick sign-in and automatic account recovery.
- Animated queue, setup and connection stages while a cloud rig is prepared.
- Free-tier advertisement playback with real media progress and official
  session continuation; no-ad launches retain the original fast path.
- Tegra X1 NVDEC hardware decoding with automatic software-decoder fallback.
- Bounded low-latency decode queues, damaged-frame suppression, RTCP recovery
  and keyframe resynchronization after packet loss.
- Synchronized Opus audio with configurable buffering and volume amplification.
- Xbox-compatible virtual gamepad, optional Switch-labelled face-button layout,
  touch-to-mouse input and an in-stream Nintendo keyboard.
- Performance, Balanced and Quality modes with bounded local adaptation.
- Free-tier session warnings and automatic return to the game page after a
  session ends or the connection is lost.
- Opt-in flight-recorder diagnostics. Detailed logs and overlays are disabled
  by default during normal gameplay.
- Dedicated Neverness to Everness credential helper and guided in-game login.

## Technical specification

| Component | Implementation |
| --- | --- |
| Target platform | Nintendo Switch (Horizon OS) & Nintendo Switch 2 (`ounce-devkit` / `libounce` / `OunceVK`, compatible with `hbmenu` and `kaiHEN`) |
| Package | Native `.nro` application (`SwitchNOW.nro` for Switch 1, `SwitchNOW-switch2.nro` for Switch 2) |
| UI | Borealis (Deko3D on Switch 1; OpenGL ES 3.0 via `OunceVK` EGL on Switch 2), controller-first navigation, touch support |
| Signaling and media | Secure WebSocket signaling and WebRTC with ICE |
| Video codec | H.264 |
| Video decoder | Switch 1: Tegra X1 NVDEC + software FFmpeg fallback; Switch 2: `libounce` (`GSV1/viddec`) hardware H.264 + software FFmpeg fallback |
| Stream presets | Safe: 720p30 at 8 Mbps; Balanced: 720p60 at 12 Mbps; Quality: 1080p60 at 20 Mbps |
| Custom bitrate steps | 8, 12, 16, 20 or 25 Mbps |
| Quality modes | Performance, Balanced and Quality |
| Audio | Opus (Hwopus on Switch 1, libopus + `audout` on Switch 2), 30-100 ms configurable buffer, 8x-16x gain options |
| Input | Xbox-compatible gamepad reports, relative mouse and keyboard over the stream data channel |
| Interface languages | English, Português (Brasil), Simplified Chinese, Spanish, Russian, Italian, French, Polish and Ukrainian |
| In-game languages | 30 selectable locale profiles, sent when supported by the game |
| Local data | `sdmc:/switch/SwitchNOW/` |

### Video quality modes

- **Performance** uses shorter frame waits and no local enhancement pass.
- **Balanced** is the default, based on Motion, with restrained filtering and bounded adaptive waits.
- **Quality** retains more frame-wait tolerance and light detail enhancement.

All modes distinguish damaged delivery, slow arrivals and decoder pressure. Resolution,
FPS and bitrate remain independent. Server bitrate parameters are negotiated at launch;
this build does not claim to change the server encoder bitrate during a session.
Legacy Adaptive/Motion migrate to Balanced, Clarity to Quality, Original to Performance.

`Auto` decodes with NVDEC and copies each finished frame into renderer-owned YUV
textures, avoiding the unstable cross-engine zero-copy surface path. It falls back
to software decoding if hardware initialization fails or the previous hardware
session did not shut down cleanly. The explicit `NVDEC` option keeps zero-copy for
diagnostic testing.
The app avoids building an unlimited frame backlog: on congestion it drops stale
work, requests a clean reference frame and resumes from the newest valid image.

## Controls

### Launcher

| Input | Action |
| --- | --- |
| Left stick / D-pad | Move focus through buttons, settings and game tiles |
| `A` | Select or activate the focused item |
| `B` | Go back or close a dialog |
| `L` / `R` | Switch between Store, Library and Settings |
| `Y` | Open search in Store or Library |
| `ZL` / `ZR` / `X` | Context actions shown in the toolbar, such as filter, sort and load more |

Vertical game-grid movement keeps the closest column instead of returning to
the first tile in every row.

### In-stream gamepad

| Switch input | Xbox-compatible action sent to the game |
| --- | --- |
| `A`, `B`, `X`, `Y` | Xbox face buttons according to the selected layout |
| `L` / `R` | `LB` / `RB` |
| `ZL` / `ZR` | Full-value `LT` / `RT` |
| Left / right stick | Left / right analog stick |
| Stick clicks | `L3` / `R3` |
| D-pad | Xbox D-pad |
| `-` | View / Select |
| Short `+` press | Menu / Start |
| Hold `+` for at least 500 ms | Nexus / Guide |
| `ZL + ZR + -` | Safely stop streaming and return to the game page |

The default **Xbox** layout maps the physical Switch positions to Xbox labels:
`A -> B`, `B -> A`, `X -> Y`, `Y -> X`. Select **Switch** in
`Settings > Controls` to send matching letters instead.

### Keyboard, touch and NTE helper

| Input | Action |
| --- | --- |
| `- + Y` | Open the transparent in-stream keyboard |
| `A` | Select a key / confirm inside the keyboard |
| `ZL` | Send `Esc` to the remote game while the keyboard is open |
| `+` | Submit the entered text and close the keyboard |
| `B` | Close the keyboard without submitting |
| Touch | Reposition the remote cursor to the touched point and perform a left click |
| Touch drag | Hold the left mouse button and move the remote cursor |
| `L + X` in NTE | Start the configured Neverness to Everness auto-login sequence |
| `B` during NTE auto-login | Cancel the sequence |

Game input is blocked while the keyboard or NTE login automation owns input, so
typing cannot accidentally trigger gameplay actions.

## Languages and settings

Select the launcher language in `Settings > Interface > Language` and press `X`
to save. The in-game language is configured separately in `Settings > Game`.
Supported games can also preserve graphics options changed inside a stream.

Existing installations are migrated automatically from
`sdmc:/switch/OpenNOWSwitch` to `sdmc:/switch/SwitchNOW` on first launch.

## Screenshots

### Library

![SwitchNOW game library](docs/screenshots/library.jpg)

### Game details

![SwitchNOW game details](docs/screenshots/game-details.jpg)

### Session launch

![SwitchNOW cloud session launch screen](docs/screenshots/session-launch.jpg)

## Install

1. Copy `SwitchNOW.nro` (Switch 1) or `SwitchNOW-switch2.nro` (Switch 2) and the icons to `sdmc:/switch/SwitchNOW/`.
2. On Switch 1, start Homebrew Menu in application mode by holding `R` while opening an installed game; on Switch 2, launch from `hbmenu` (`ounce-hbmenu`) or `kaiHEN`.
3. Launch SwitchNOW.

Application data, settings and optional debug logs are stored in
`sdmc:/switch/SwitchNOW/`.

## Known issues & login workaround (`v1.3.0`)

Confirmed on both **Switch 1** (`SwitchNOW.nro`) and **Switch 2** (`SwitchNOW-switch2.nro`):

- **Default `NVIDIA` provider login / empty library / `ENTITLEMENT_FAILURE_STATUS` (`HTTP 403`):**
  - Direct email/password sign-in with the default `NVIDIA` provider currently fails (NVIDIA 2FA / external verification changes), and QR fallback on the `NVIDIA` provider can leave `My Library` empty (`Loaded 0 games`) and fail game launches with `HTTP 403 ... ENTITLEMENT_FAILURE_STATUS` (`8A910006`).
  - **Recommended workaround:** Choose an **Alliance provider such as `ABYA`** in the provider picker when signing in. Signing in via the **ABYA** provider authenticates properly and lists your library games.
- **QR Code dialog cropping:**
  - On the QR login screen, the rendered QR matrix can be slightly clipped at the bottom edge (especially in `720p` handheld mode), though scanning it with a phone/PC still works.
- **UI freeze immediately after login:**
  - Right after completing login, the UI may freeze before advancing back to the library. Because the OAuth session is already saved to `sdmc:/switch/SwitchNOW/` when login completes, **closing SwitchNOW and reopening it** lands directly in the signed-in session.
  - See [docs/SWITCH2_KNOWN_ISSUES.md](docs/SWITCH2_KNOWN_ISSUES.md) for tracking details.

## Build requirements

- **Switch 1:** Windows with MSYS2 at `C:\msys64` or Unix/MSYS2 shell, devkitPro (`devkitA64`, Switch portlibs, CMake/Ninja): `.\build-switch.ps1` -> `build/switch/SwitchNOW.nro`.
- **Switch 2:** `devkitpro/devkita64` + pinned [`ounce-devkit`](https://github.com/ouncebrew/ounce-devkit) (`libounce`, `OunceVK`), CMake/Ninja: `bash scripts/build-switch2.sh` -> `build/switch2/SwitchNOW-switch2.nro`.

Create a conventional release archive:

```powershell
.\scripts\package-release.ps1 -Version 1.3.0
```

## Source layout

```text
app/src/                 Application, GFN API and streaming implementation
resources/               Fonts, icons and RomFS resources
extern/                  Pinned third-party dependencies
tests/                   Host-side policy and parsing tests
scripts/                 Switch build and release packaging
CMakeLists.txt            Switch build definition
build-switch.ps1          Windows build entry point
```

## Privacy

The repository contains no accounts, passwords, tokens, logs or device data.
Saved credentials are created only at runtime on the user's SD card. Password
storage for GFN accounts is optional and uses the encrypted local credential vault. Game/Epic auto-login profiles are optional plain-text files on SD; protect the card accordingly.

## Projects, credits and acknowledgements

- **Original `SwitchNOW` project:** Created and maintained by **Blade-Punisher** (thanks for building SwitchNOW and making the source available).
- **Nintendo Switch 2 port & Brazilian Portuguese (`pt-BR`) localization:** [David Kalil Braga (@davidkalil10)](https://github.com/davidkalil10).
- **Switch 2 SDK & ecosystem:** [ounce-devkit](https://github.com/ouncebrew/ounce-devkit), `libounce`, `OunceVK` (`ounce-hbmenu` and `kaiHEN` compatibility).

SwitchNOW would not exist without the following open-source projects. Reference projects informed the architecture and user experience. Selected adaptations from OpenNOW-Switch are acknowledged in THIRD_PARTY_NOTICES.md.

### Architecture and UX references

- [OpenNOW](https://github.com/OpenCloudGaming/OpenNOW) - GeForce NOW client,
  service flow and desktop UX reference.
- [OpenNOW-Switch](https://github.com/OpenCloudGaming/OpenNOW-Switch) - earlier
  Nintendo Switch research and implementation reference.
- [Moonlight-Switch](https://github.com/XITRIX/Moonlight-Switch) - Switch-native
  streaming, controller, rendering and hardware-decoding reference.

### Core build and runtime dependencies

- [devkitPro](https://devkitpro.org/),
  [libnx](https://github.com/switchbrew/libnx) and
  [deko3d](https://github.com/devkitPro/deko3d) - Nintendo Switch toolchain,
  platform services and graphics API.
- [Borealis](https://github.com/xfangfang/borealis) - console-oriented UI,
  navigation and rendering framework.
- [libpeer](https://github.com/sepfy/libpeer) - embedded WebRTC, ICE, DTLS,
  SRTP and data-channel foundation.
- [FFmpeg](https://github.com/FFmpeg/FFmpeg) and Averne's
  [NVTEGRA branch](https://github.com/averne/FFmpeg/tree/nvtegra) - H.264/Opus
  media processing and Tegra X1 hardware decoding support.
- [curl](https://github.com/curl/curl) - HTTPS and service requests.
- [Jansson](https://github.com/akheron/jansson) - JSON parsing and generation.
- [zlib](https://github.com/madler/zlib) - compression support.

### Bundled upstream components

The vendored Borealis and libpeer trees also contain components from
[GLFW](https://github.com/xfangfang/glfw),
[SDL](https://github.com/XITRIX/SDL),
[fmt](https://github.com/fmtlib/fmt),
[Tweeny](https://github.com/mobius3/tweeny),
[Yoga](https://github.com/facebook/yoga),
[NanoVG](https://github.com/memononen/nanovg),
[tinyxml2](https://github.com/leethomason/tinyxml2),
[libromfs](https://github.com/averne/libromfs),
[switch-libpulsar](https://github.com/p-sam/switch-libpulsar),
[libsrtp](https://github.com/cisco/libsrtp),
[usrsctp](https://github.com/sctplab/usrsctp),
[Mbed TLS](https://github.com/Mbed-TLS/mbedtls),
[cJSON](https://github.com/DaveGamble/cJSON),
[FreeRTOS coreHTTP](https://github.com/FreeRTOS/coreHTTP) and
[FreeRTOS coreMQTT](https://github.com/FreeRTOS/coreMQTT). Platform-specific and
optional components are selected by the build configuration.

Each upstream project remains subject to its own license and copyright notices.
This acknowledgement list does not replace the license files shipped with the
source and dependency trees.
