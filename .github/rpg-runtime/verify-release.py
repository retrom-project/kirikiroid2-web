#!/usr/bin/env python3
"""Validate KiriKiri browser assets and emit release metadata."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path


TAG = re.compile(r"^retrom-core-g338d2029f169-r[1-9][0-9]*(-rc\.[1-9][0-9]*)?$")
COMMIT = re.compile(r"^[0-9a-f]{40}$")


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
    javascript = paths["index.js"].read_text(encoding="utf-8")
    if any(marker not in javascript for marker in (
        "krkr2_host_bookmark_is_ready", "krkr2_host_save_bookmark",
        "krkr2_host_load_bookmark", "krkr2_host_load_bookmark_state",
    )):
        raise SystemExit("RPG_RUNTIME_RELEASE_BRIDGE_INVALID")

    assets = [
        {"filename": path.name, "observedSha256": digest(path), "sizeBytes": path.stat().st_size}
        for path in paths.values()
    ]
    metadata = {
        "adapterAbi": "kirikiri-kag-bookmark",
        "assets": assets,
        "commit": args.commit,
        "digestPolicy": "OBSERVED_CACHE_INTEGRITY_ONLY",
        "repository": args.repository,
        "schemaVersion": 1,
        "sourceCommits": {"engine": "338d2029f16969b84becfd163c67f99740e28296"},
        "tag": args.tag,
    }
    (args.output / "rpg-runtime-release.json").write_text(
        json.dumps(metadata, ensure_ascii=True, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
