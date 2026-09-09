#!/usr/bin/env bash
set -euo pipefail

HALIDE_VERSION="21.0.0"
HALIDE_ARCHIVE="Halide-${HALIDE_VERSION}-x86-64-linux-b629c80de18f1534ec71fddd8b567aa7027a0876.tar.gz"
HALIDE_SHA256="b56139ddc5d863486b9b339e1c9b7cc3f6aadd4dd8a2eff2202e79ca68706091"
HALIDE_URL="https://github.com/halide/Halide/releases/download/v${HALIDE_VERSION}/${HALIDE_ARCHIVE}"

BUILD_ROOT="${BUILD_ROOT:-$PWD/.build}"
DIST_ROOT="${DIST_ROOT:-$PWD/dist}"
mkdir -p "$BUILD_ROOT" "$DIST_ROOT"

if [[ ! -f "$BUILD_ROOT/$HALIDE_ARCHIVE" ]]; then
  curl --fail --location --retry 3 "$HALIDE_URL" --output "$BUILD_ROOT/$HALIDE_ARCHIVE"
fi
echo "$HALIDE_SHA256  $BUILD_ROOT/$HALIDE_ARCHIVE" | sha256sum --check -

if [[ ! -d "$BUILD_ROOT/halide" ]]; then
  mkdir -p "$BUILD_ROOT/halide"
  tar -xzf "$BUILD_ROOT/$HALIDE_ARCHIVE" -C "$BUILD_ROOT/halide" --strip-components=1
fi

HALIDE_ROOT="$BUILD_ROOT/halide"
HALIDE_LIBRARY_PATH="$(find "$HALIDE_ROOT" -type f -name 'libHalide.so*' -print -quit)"
if [[ -z "$HALIDE_LIBRARY_PATH" ]]; then
  echo "libHalide.so was not found" >&2
  exit 1
fi
HALIDE_LIBRARY_DIR="$(dirname "$HALIDE_LIBRARY_PATH")"

c++ -std=c++17 -O2 astro_pipeline_generator.cpp \
  -I"$HALIDE_ROOT/include" -Wl,-rpath,"$HALIDE_LIBRARY_DIR" \
  "$HALIDE_LIBRARY_PATH" -ldl -lpthread -lz -o "$BUILD_ROOT/astro_pipeline_generator"

mkdir -p "$BUILD_ROOT/generated"
LD_LIBRARY_PATH="$HALIDE_LIBRARY_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
  "$BUILD_ROOT/astro_pipeline_generator" \
    -g nebulastack_astro_pipeline -f nebulastack_astro_pipeline -e static_library,h \
    -o "$BUILD_ROOT/generated" target=wasm-32-wasmrt-wasm_simd128

em++ -std=c++17 -O3 -msimd128 --no-entry \
  wasm_wrapper.cpp "$BUILD_ROOT/generated/nebulastack_astro_pipeline.a" \
  -I"$HALIDE_ROOT/include" -I"$BUILD_ROOT/generated" \
  -sWASM=1 -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createNebulaStackHalide \
  -sENVIRONMENT=worker -sFILESYSTEM=0 -sALLOW_MEMORY_GROWTH=1 \
  -sINITIAL_MEMORY=33554432 -sMAXIMUM_MEMORY=2147483648 -sMALLOC=emmalloc -sASSERTIONS=0 \
  -sEXPORTED_FUNCTIONS=_malloc,_free,_nebulastack_process_tile \
  -o "$DIST_ROOT/astro-halide.js"

test -s "$DIST_ROOT/astro-halide.js"
test -s "$DIST_ROOT/astro-halide.wasm"
sha256sum "$DIST_ROOT/astro-halide.js" "$DIST_ROOT/astro-halide.wasm" > "$DIST_ROOT/SHA256SUMS"
