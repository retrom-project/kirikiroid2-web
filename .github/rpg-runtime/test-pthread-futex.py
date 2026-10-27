#!/usr/bin/env python3
"""Deterministic wake publication and recursive-wait regressions."""

from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests/web/pthread-futex"


class FutexWakeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        (ROOT / "out").mkdir(exist_ok=True)
        cls.temporary = tempfile.TemporaryDirectory(prefix="futex-test-", dir=ROOT / "out")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.binary = Path(cls.temporary.name) / "interleavings"
        subprocess.run([
            "cc", "-std=gnu11", "-O1", "-pthread", "-ffunction-sections",
            "-fdata-sections", "-Wl,--gc-sections", "-I", str(FIXTURE),
            str(FIXTURE / "interleavings.c"), "-o", str(cls.binary),
        ], check=True)

    def run_case(self, case):
        result = subprocess.run(
            [str(self.binary), case], capture_output=True, text=True, timeout=10,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_wake_is_published_before_worker_continues(self):
        self.run_case("publication")

    def test_nested_wait_preserves_outer_notification(self):
        self.run_case("recursive")


if __name__ == "__main__":
    unittest.main()
