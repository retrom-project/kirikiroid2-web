#!/usr/bin/env python3
"""Validate KiriKiri browser assets and emit release metadata."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import zipfile
from pathlib import Path


TAG = re.compile(r"^retrom-core-g13dda190f837-r[1-9][0-9]*(-rc\.[1-9][0-9]*)?$")
COMMIT = re.compile(r"^[0-9a-f]{40}$")
BUNDLED_CJK = {
    "NotoSansSC-Regular.otf": (8_331_336, "faa6c9df652116dde789d351359f3d7e5d2285a2b2a1f04a2d7244df706d5ea9"),
    "NotoSansSC-Regular.LICENSE.txt": (4_301, "6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2"),
}


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            value.update(chunk)
    return value.hexdigest()


def regular_asset(path: Path, minimum: int, maximum: int) -> None:
    if path.is_symlink() or not path.is_file():
        raise SystemExit(f"RPG_RUNTIME_RELEASE_ASSET_INVALID:{path.name}")
    if not minimum <= path.stat().st_size <= maximum:
        raise SystemExit(f"RPG_RUNTIME_RELEASE_ASSET_INVALID:{path.name}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--check-existing", action="store_true",
                        help="Verify downloaded files and metadata without rewriting the manifest")
    args = parser.parse_args()
    if TAG.fullmatch(args.tag) is None or COMMIT.fullmatch(args.commit) is None:
        raise SystemExit("RPG_RUNTIME_RELEASE_IDENTITY_INVALID")

    paths = {name: args.output / name for name in (
        "index.js", "index.wasm", "vlfs.js", "assets.zip", "LICENSE",
    )}
    regular_asset(paths["index.js"], 200_000, 1_048_576)
    regular_asset(paths["index.wasm"], 10_000_000, 67_108_864)
    regular_asset(paths["vlfs.js"], 10_000, 262_144)
    regular_asset(paths["assets.zip"], 1_000_000, 16_777_216)
    regular_asset(paths["LICENSE"], 20_000, 65_536)
    if paths["index.wasm"].read_bytes()[:8] != b"\x00asm\x01\x00\x00\x00":
        raise SystemExit("RPG_RUNTIME_RELEASE_WASM_INVALID")
    if paths["assets.zip"].read_bytes()[:4] != b"PK\x03\x04":
        raise SystemExit("RPG_RUNTIME_RELEASE_ASSETS_ZIP_INVALID")
    try:
        with zipfile.ZipFile(paths["assets.zip"]) as archive:
            for name, (size, sha256) in BUNDLED_CJK.items():
                if (archive.namelist().count(name) != 1
                        or archive.getinfo(name).file_size != size
                        or hashlib.sha256(archive.read(name)).hexdigest() != sha256):
                    raise SystemExit(f"RPG_RUNTIME_RELEASE_CJK_ASSET_INVALID:{name}")
    except (zipfile.BadZipFile, KeyError) as error:
        raise SystemExit("RPG_RUNTIME_RELEASE_CJK_ASSET_INVALID") from error
    javascript = paths["index.js"].read_text(encoding="utf-8")
    if any(marker not in javascript for marker in (
        "krkr2_host_load_bookmark_is_ready",
        "krkr2_host_observe_game_saves", "onGameSave",
        "krkr2_host_flush_game_saves",
        "krkr2_host_finalize_native_exit", "onNativeExitRequested",
        "krkr2StopMainLoop",
        "krkr2_host_bookmark_is_ready", "krkr2_host_save_bookmark",
        "krkr2_host_load_bookmark", "krkr2_host_load_bookmark_state",
    )):
        raise SystemExit("RPG_RUNTIME_RELEASE_BRIDGE_INVALID")

    vlfs = paths["vlfs.js"].read_text(encoding="utf-8")
    if ("content-io-v1" not in vlfs or "registerContent(path, handle, reader)" not in vlfs
            or any(marker in vlfs for marker in ("fetch(", "XMLHttpRequest", "registerRemote(", "registerZipRemote("))):
        raise SystemExit("RPG_RUNTIME_RELEASE_CONTENT_BRIDGE_INVALID")

    assets = [
        {"filename": path.name, "observedSha256": digest(path), "sizeBytes": path.stat().st_size}
        for path in paths.values()
    ]
    metadata = {
        "adapterAbi": "kirikiri-content-io-v1",
        "assets": assets,
        "commit": args.commit,
        "digestPolicy": "OBSERVED_CACHE_INTEGRITY_ONLY",
        "repository": args.repository,
        "schemaVersion": 1,
        "sourceCommits": {"engine": "13dda190f8370d02b6cf59286a088529b355658c"},
        "tag": args.tag,
    }
    manifest = args.output / "rpg-runtime-release.json"
    if args.check_existing:
        regular_asset(manifest, 2, 65_536)
        if json.loads(manifest.read_text(encoding="utf-8")) != metadata:
            raise SystemExit("RPG_RUNTIME_RELEASE_METADATA_MISMATCH")
    else:
        manifest.write_text(
            json.dumps(metadata, ensure_ascii=True, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
