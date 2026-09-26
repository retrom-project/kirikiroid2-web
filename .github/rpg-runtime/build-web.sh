#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
output=${1:?output directory is required}
mkdir -p "$output"
output=$(cd "$output" && pwd)
toolchain_root=${KIRIKIRI_RUNTIME_TOOLCHAIN_ROOT:-${XDG_CACHE_HOME:-$HOME/.cache}/kirikiroid2-runtime}
emsdk_root=${EMSDK:-$toolchain_root/emsdk-6.0.9}
vcpkg_root=${VCPKG_ROOT:-$toolchain_root/vcpkg-b1e15efef675}

prepare_emsdk() {
  if [[ ! -x "$emsdk_root/emsdk" ]]; then
    rm -rf -- "$emsdk_root"
    git clone --quiet --depth 1 --branch 6.0.9 https://github.com/emscripten-core/emsdk.git "$emsdk_root"
  fi
  if [[ ! -f "$emsdk_root/upstream/emscripten/emcc.py" ]]; then
    "$emsdk_root/emsdk" install 6.0.9
    "$emsdk_root/emsdk" activate 6.0.9
  fi
}

prepare_vcpkg() {
  local expected=b1e15efef6758eaa0beb0a8732cfa66f6a68a81d
  if [[ ! -d "$vcpkg_root/.git" ]]; then
    rm -rf -- "$vcpkg_root"
    git clone --quiet https://github.com/microsoft/vcpkg.git "$vcpkg_root"
    git -C "$vcpkg_root" checkout --quiet "$expected"
  fi
  if [[ "$(git -C "$vcpkg_root" rev-parse HEAD)" != "$expected" ]]; then
    echo "KIRIKIRI_VCPKG_COMMIT_MISMATCH" >&2
    exit 1
  fi
  if [[ ! -x "$vcpkg_root/vcpkg" ]]; then
    "$vcpkg_root/bootstrap-vcpkg.sh" -disableMetrics
  fi
}

prepare_emsdk
prepare_vcpkg
source "$emsdk_root/emsdk_env.sh" >/dev/null
export VCPKG_ROOT="$vcpkg_root"

# Match upstream's SDK port preparation before vcpkg configures Cocos. Building
# these lazily under -fwasm-exceptions hits the SDK's boolean-setting conversion.
embuilder build libpng libpng-mt libpng-legacysjlj libpng-mt-legacysjlj
embuilder build freetype freetype-legacysjlj
embuilder build harfbuzz harfbuzz-mt
embuilder build sdl2 sdl2-mt
embuilder build sdl2_ttf sdl2_ttf-mt
(cd "$root" && cmake --preset "Web Release Config")
cmake --build "$root/out/web/release" --parallel "${KIRIKIRI_BUILD_JOBS:-2}"

for asset in index.js index.wasm vlfs.js assets.zip; do
  install -m 0644 "$root/out/web/release/$asset" "$output/$asset"
done
install -m 0644 "$root/LICENSE" "$output/LICENSE"
