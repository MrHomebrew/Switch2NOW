#!/usr/bin/env bash
set -euo pipefail

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITA64="${DEVKITA64:-$DEVKITPRO/devkitA64}"
export PATH="$DEVKITPRO/tools/bin:$DEVKITA64/bin:$DEVKITPRO/portlibs/switch2/bin:$DEVKITPRO/portlibs/switch/bin:$PATH"

cd "$(dirname "$0")/.."
ROOT_DIR="$(pwd)"

if [[ ! -f "$DEVKITPRO/libounce/cmake/Switch2.cmake" ]]; then
  echo "ERROR: Switch2.cmake not found at $DEVKITPRO/libounce/cmake/Switch2.cmake" >&2
  exit 1
fi

BUILD_DIR="$ROOT_DIR/build/switch2"
FFMPEG_BUILD="$BUILD_DIR/ffmpeg-build"
FFMPEG_PREFIX="$BUILD_DIR/ffmpeg-install"
JOBS="${OPENNOW_BUILD_JOBS:-$(nproc)}"
SWITCHNOW_VERSION="${SWITCHNOW_VERSION:-1.3.0}"
NETWORK_DEBUG="${SWITCHNOW_NETWORK_DEBUG:-OFF}"

# Build clean Switch 2 FFmpeg archives from extern/ffmpeg/upstream without NVTEGRA
# or Switch 1 cortex-a57/libnx contamination.
if [[ ! -f "$FFMPEG_PREFIX/lib/libavcodec.a" ]] || [[ ! -f "$FFMPEG_PREFIX/lib/libavformat.a" ]]; then
  echo "==> Building Switch 2 FFmpeg static archives..."
  rm -rf "$FFMPEG_BUILD"
  mkdir -p "$FFMPEG_BUILD" "$FFMPEG_PREFIX"
  chmod +x "$ROOT_DIR/extern/ffmpeg/upstream/configure"
  find "$ROOT_DIR/extern/ffmpeg/upstream" -name '*.sh' -exec chmod +x {} +
  (
    cd "$FFMPEG_BUILD"
    bash "$ROOT_DIR/extern/ffmpeg/upstream/configure" \
      --prefix="$FFMPEG_PREFIX" \
      --enable-gpl \
      --disable-shared \
      --enable-static \
      --cross-prefix=aarch64-none-elf- \
      --enable-cross-compile \
      --arch=aarch64 \
      --cpu=cortex-a78c \
      --target-os=horizon \
      --enable-pic \
      --extra-cflags="-D__SWITCH__ -D__SWITCH2__ -D_GNU_SOURCE -O2 -mcpu=cortex-a78c+crypto -mtp=soft -ftls-model=local-exec -I${DEVKITPRO}/libounce/include -I${DEVKITPRO}/portlibs/switch2/include -I${DEVKITPRO}/portlibs/switch/include" \
      --extra-cxxflags="-D__SWITCH__ -D__SWITCH2__ -D_GNU_SOURCE -O2 -mcpu=cortex-a78c+crypto -mtp=soft -ftls-model=local-exec -I${DEVKITPRO}/libounce/include -I${DEVKITPRO}/portlibs/switch2/include -I${DEVKITPRO}/portlibs/switch/include" \
      --extra-ldflags="-fPIE -L${DEVKITPRO}/portlibs/switch2/lib -L${DEVKITPRO}/portlibs/switch/lib -L${DEVKITPRO}/libounce/lib" \
      --disable-runtime-cpudetect \
      --disable-programs \
      --disable-debug \
      --disable-doc \
      --disable-autodetect \
      --enable-asm \
      --enable-neon \
      --disable-avdevice \
      --disable-encoders \
      --disable-muxers \
      --disable-avfilter \
      --disable-postproc \
      --enable-swscale \
      --enable-swresample \
      --enable-zlib \
      --enable-bzlib \
      --disable-nvtegra \
      --enable-network \
      --enable-protocols \
      --enable-demuxers \
      --enable-parsers \
      --enable-decoders \
      --enable-bsfs
    make -j"$JOBS"
    make install
  )
fi

echo "==> Configuring SwitchNOW for Nintendo Switch 2..."
cmake -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/libounce/cmake/Switch2.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DPROJECT_VERSION_STRING="$SWITCHNOW_VERSION" \
  -DSWITCHNOW_NETWORK_DEBUG="$NETWORK_DEBUG" \
  -DSWITCHNOW_FFMPEG_DIR="$FFMPEG_PREFIX"

cmake --build "$BUILD_DIR" --target cjson mbedtls srtp2 usrsctp -j"$JOBS"
cmake --build "$BUILD_DIR" --target SwitchNOW.nro -j"$JOBS"

python3 "$ROOT_DIR/tools/verify_switch2_package.py" \
  "$BUILD_DIR/SwitchNOW-switch2.nro" \
  --link-map "$BUILD_DIR/SwitchNOW-switch2.map"

printf '\nBuilt: %s\n' "$BUILD_DIR/SwitchNOW-switch2.nro"
