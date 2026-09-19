#!/usr/bin/env python3
"""Validate the fixed KiriKiri fork baseline and bookmark bridge."""

from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BASELINE = "338d2029f16969b84becfd163c67f99740e28296"


def require(path: str, markers: tuple[str, ...]) -> None:
    text = (ROOT / path).read_text(encoding="utf-8")
    if any(marker not in text for marker in markers):
        raise SystemExit(f"RPG_RUNTIME_SOURCE_CONTRACT_INVALID:{path}")


def main() -> int:
    manifest = json.loads((ROOT / "retrom-fork.json").read_text(encoding="utf-8"))
    expected = {
        "schemaVersion": 1,
        "forkRepository": "https://github.com/retrom-project/kirikiroid2-web",
        "defaultBranch": "retrom/g338d2029f169",
        "upstreamMirrorBranch": "web",
        "upstreams": [
            {
                "role": "engine",
                "repository": "https://github.com/fenghengzhi/kirikiroid2-web",
                "refType": "COMMIT",
                "ref": BASELINE,
                "commit": BASELINE,
            }
        ],
        "releaseTagPattern": (
            r"^retrom-core-g338d2029f169-r[1-9][0-9]*"
            r"(-rc\.[1-9][0-9]*)?$"
        ),
        "adapterAbi": "kirikiri-content-io-v1",
        "releaseAssets": [
            "index.js", "index.wasm", "vlfs.js", "assets.zip", "LICENSE",
            "rpg-runtime-release.json",
        ],
    }
    if manifest != expected:
        raise SystemExit("RPG_RUNTIME_FORK_MANIFEST_INVALID")
    require("CMakeLists.txt", (
        "_krkr2_host_bookmark_is_ready", "_krkr2_host_save_bookmark",
        "_krkr2_host_load_bookmark", "_krkr2_host_load_bookmark_state",
    ))
    require("cpp/core/environ/CMakeLists.txt", ("web/HostBookmarkBridge.cpp",))
    require("cpp/core/environ/web/HostBookmarkBridge.cpp", (
        "krkr2_host_bookmark_is_ready", "krkr2_host_save_bookmark",
        "krkr2_host_load_bookmark", "krkr2_host_load_bookmark_state",
        "findKagMethod", "kagReachedSavePoint", "scheduleKagLoad",
    ))
    require("vcpkg/ports/libgdiplus/portfile.cmake", (
        "vcpkg_cmake_config_fixup(CONFIG_PATH share/libgdiplus)",
    ))
    require("vcpkg/triplets/wasm32-emscripten.cmake", (
        'VCPKG_MAKE_BUILD_TRIPLET "--host=wasm32-unknown-emscripten"',
    ))
    require("platforms/web/vlfs.js", (
        "content-io-v1", "registerContent(path, handle, reader)",
        "9601f63ba9d1bad095b42b32a3d6167166535be246a87f0efac7c5b125ed27bf",
    ))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
