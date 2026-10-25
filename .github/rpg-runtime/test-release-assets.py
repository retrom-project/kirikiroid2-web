#!/usr/bin/env python3
"""Exercise the build-to-publish handoff with valid and corrupted asset bundles."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
MARKERS = (
    "krkr2_host_load_bookmark_is_ready krkr2_host_observe_game_saves onGameSave "
    "krkr2_host_flush_game_saves krkr2_host_finalize_native_exit onNativeExitRequested "
    "krkr2StopMainLoop krkr2_host_bookmark_is_ready krkr2_host_save_bookmark "
    "krkr2_host_load_bookmark krkr2_host_load_bookmark_state"
)


class ReleaseAssetsTest(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.addCleanup(self.scratch.cleanup)
        self.output = Path(self.scratch.name)
        assets = {
            "index.js": MARKERS.encode().ljust(200_000, b" "),
            "index.wasm": b"\0asm\1\0\0\0".ljust(10_000_000, b"\0"),
            "vlfs.js": b"content-io-v1 registerContent(path, handle, reader)".ljust(10_000, b" "),
            "assets.zip": b"PK\3\4".ljust(1_000_000, b"\0"),
            "LICENSE": b"test license".ljust(20_000, b" "),
        }
        for name, payload in assets.items():
            (self.output / name).write_bytes(payload)
        self.command = [
            sys.executable, str(ROOT / ".github/rpg-runtime/verify-release.py"),
            "--output", str(self.output), "--repository", "https://github.com/retrom-project/kirikiroid2-web",
            "--tag", "retrom-core-g13dda190f837-r1-rc.1", "--commit", "1" * 40,
        ]
        subprocess.run(self.command, check=True, capture_output=True)

    def check_bundle(self):
        return subprocess.run(self.command + ["--check-existing"], capture_output=True).returncode

    def test_round_trip_keeps_original_manifest(self):
        manifest = self.output / "rpg-runtime-release.json"
        original = manifest.read_bytes()
        self.assertEqual(self.check_bundle(), 0)
        self.assertEqual(manifest.read_bytes(), original)

    def test_changed_payload_fails_even_if_structure_still_passes(self):
        metadata = json.loads((self.output / "rpg-runtime-release.json").read_text())
        for asset in metadata["assets"]:
            with self.subTest(asset=asset["filename"]):
                path = self.output / asset["filename"]
                original = path.read_bytes()
                path.write_bytes(original[:-1] + bytes([original[-1] ^ 1]))
                self.assertNotEqual(self.check_bundle(), 0)
                path.write_bytes(original)

    def test_wrong_release_identity_or_digests_fail(self):
        manifest = self.output / "rpg-runtime-release.json"
        original = manifest.read_text()
        for field, value in (
            ("tag", "retrom-core-g13dda190f837-r2"),
            ("commit", "2" * 40),
            ("adapterAbi", "incorrect-abi"),
            ("repository", "https://github.com/example/other"),
            ("assets", []),
        ):
            with self.subTest(field=field):
                changed = json.loads(original)
                changed[field] = value
                manifest.write_text(json.dumps(changed))
                self.assertNotEqual(self.check_bundle(), 0)
        manifest.write_text(original)


if __name__ == "__main__":
    unittest.main()
