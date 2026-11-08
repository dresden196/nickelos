#!/bin/bash
# NickelOS: builds ChromeOS's Japanese input method without Native Client
# (docs/vision.md): Mozc's session handler in WebAssembly (mozc_wasm.cc, on
# Mozc as fcitx-contrib/fcitx5-mozc builds it with CMake), inside Mozc's own
# NaCl extension JavaScript (google/mozc src/chrome/nacl at its last release
# with it) started by nickel-mozc-init.js. Writes nickel-mozc-<VERSION>.tar.xz,
# the input_methods/nacl_mozc directory, which app-i18n/nickel-mozc
# installs. Runs on the build host, in WORK (default /srv/nickel/mozc-wasm);
# installs nothing on the host (emsdk, cmake and ninja live in WORK).
#   build.sh VERSION
set -eux
VERSION=${1:?version}
HERE=$(cd "$(dirname "$0")" && pwd)
W=${WORK:-/srv/nickel/mozc-wasm}
FCITX5_MOZC_COMMIT=8a4b8754f065df770a7811f94822188e5cf2581d
EMSDK_VERSION=3.1.69
MOZC_NACL_COMMIT=afb03ddf  # google/mozc, last commit with src/chrome/nacl
mkdir -p "$W" && cd "$W"

# Tools: cmake and ninja as release binaries, python for Mozc's generators.
if [ ! -x tools/bin/cmake ]; then
  mkdir -p tools/bin
  curl -fsSL https://github.com/Kitware/CMake/releases/download/v3.31.6/cmake-3.31.6-linux-x86_64.tar.gz |
    tar xz -C tools --strip-components=1
fi
if [ ! -x tools/bin/ninja ]; then
  curl -fsSL -o ninja.zip https://github.com/ninja-build/ninja/releases/download/v1.12.1/ninja-linux.zip
  python3 -c "import zipfile; zipfile.ZipFile('ninja.zip').extractall('tools/bin')"
  chmod 755 tools/bin/ninja
fi
ln -sf "$(command -v python3)" tools/bin/python
export PATH=$W/tools/bin:$PATH
[ -d emsdk ] || git clone https://github.com/emscripten-core/emsdk.git
(cd emsdk && ./emsdk install "$EMSDK_VERSION" && ./emsdk activate "$EMSDK_VERSION")

# Mozc, with abseil and protobuf, as fcitx5-mozc pins them.
if [ ! -d fcitx5-mozc ]; then
  git clone https://github.com/fcitx-contrib/fcitx5-mozc.git
fi
(cd fcitx5-mozc && git checkout -q "$FCITX5_MOZC_COMMIT" &&
  git submodule update --init --recursive &&
  cd mozc && git checkout -q -- src/base/thread.h &&
  patch -p1 < "$HERE"/mozc-thread-inline.patch)
# The dictionary as fcitx5-mozc's CI builds it (Bazel).
[ -f mozc_data.inc ] ||
  curl -fsSL -o mozc_data.inc https://github.com/fcitx-contrib/fcitx5-mozc/releases/download/latest/mozc_data.inc

# A native protoc of the same protobuf for the generated sources.
if [ ! -x protoc-native/protoc ]; then
  cmake -S fcitx5-mozc/protobuf -B protoc-native -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -Dprotobuf_BUILD_TESTS=OFF \
    -Dprotobuf_ABSL_PROVIDER=module -DABSL_ROOT_DIR="$W"/fcitx5-mozc/abseil-cpp \
    -Dprotobuf_INSTALL=OFF
  cmake --build protoc-native --target protoc
fi

# The engine.
source emsdk/emsdk_env.sh
mkdir -p build/fcitx5-mozc/data_manager/oss
cp mozc_data.inc build/fcitx5-mozc/data_manager/oss/
emcmake cmake -S "$HERE" -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DFCITX5_MOZC="$W"/fcitx5-mozc -DPROTOC_EXECUTABLE="$W"/protoc-native/protoc \
  -Dprotobuf_BUILD_PROTOC_BINARIES=OFF
cmake --build build

# The extension around it.
if [ ! -d mozc-nacl ]; then
  git clone --filter=blob:none --no-checkout https://github.com/google/mozc.git mozc-nacl
fi
(cd mozc-nacl && git checkout -q "$MOZC_NACL_COMMIT" -- src/chrome/nacl)
rm -rf ext && python3 "$HERE"/assemble.py mozc-nacl/src/chrome/nacl build ext
cp mozc-nacl/LICENSE ext/LICENSE.mozc 2>/dev/null ||
  (cd mozc-nacl && git checkout -q "$MOZC_NACL_COMMIT" -- LICENSE && cp LICENSE ../ext/LICENSE.mozc)
tar -cJf "nickel-mozc-$VERSION.tar.xz" --owner=0 --group=0 \
  --transform "s#^ext#nickel-mozc-$VERSION#" ext
ls -l "nickel-mozc-$VERSION.tar.xz"
