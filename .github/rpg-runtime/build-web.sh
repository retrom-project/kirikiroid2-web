#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
source "$root/.github/rpg-runtime/toolchain.env"
output=${1:?output directory is required}
mkdir -p "$output"
output=$(cd "$output" && pwd)
toolchain_root=${KIRIKIRI_RUNTIME_TOOLCHAIN_ROOT:-${XDG_CACHE_HOME:-$HOME/.cache}/kirikiroid2-runtime}
emsdk_root=${EMSDK:-$toolchain_root/emsdk-$KIRIKIRI_EMSDK_VERSION}
vcpkg_root=${VCPKG_ROOT:-$toolchain_root/vcpkg-${KIRIKIRI_VCPKG_COMMIT:0:12}}

phase() {
  local name=$1 started=$SECONDS
  shift
  printf '::group::%s\n' "$name"
  "$@"
  printf '::endgroup::\n'
  if [[ -n ${KIRIKIRI_BUILD_TIMINGS:-} ]]; then
    printf '%s\t%s\n' "$name" "$((SECONDS - started))" >> "$KIRIKIRI_BUILD_TIMINGS"
  fi
}

prepare_emsdk() {
  if [[ ! -x "$emsdk_root/emsdk" ]]; then
    rm -rf -- "$emsdk_root"
    git clone --quiet --depth 1 --branch "$KIRIKIRI_EMSDK_VERSION" https://github.com/emscripten-core/emsdk.git "$emsdk_root"
  fi
  if [[ ! -f "$emsdk_root/upstream/emscripten/emcc.py" ]]; then
    "$emsdk_root/emsdk" install "$KIRIKIRI_EMSDK_VERSION"
    "$emsdk_root/emsdk" activate "$KIRIKIRI_EMSDK_VERSION"
  fi
}

prepare_vcpkg() {
  local expected=$KIRIKIRI_VCPKG_COMMIT
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

phase "Emscripten SDK" prepare_emsdk
phase "vcpkg tool" prepare_vcpkg
source "$emsdk_root/emsdk_env.sh" >/dev/null
export VCPKG_ROOT="$vcpkg_root"

# Match upstream's SDK port preparation before vcpkg configures Cocos. Building
# these lazily under -fwasm-exceptions hits the SDK's boolean-setting conversion.
prepare_ports() {
  local -a ports
  while read -r -a ports; do
    embuilder build "${ports[@]}"
  done < "$root/.github/rpg-runtime/emscripten-ports.txt"
}

configure_web() {
  (cd "$root" && cmake --preset "Web Release Config")
}

phase "Emscripten ports" prepare_ports
phase "Configure and vcpkg dependencies" configure_web
phase "Core compile and link" cmake --build "$root/out/web/release" --parallel "${KIRIKIRI_BUILD_JOBS:-2}"

for asset in index.js index.wasm vlfs.js assets.zip; do
  install -m 0644 "$root/out/web/release/$asset" "$output/$asset"
done
install -m 0644 "$root/LICENSE" "$output/LICENSE"
