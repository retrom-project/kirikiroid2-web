"""Ownership regression tests for the offline packaging front end (no decoder)."""
import errno
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import zipfile

SPEC = importlib.util.spec_from_file_location('standardize', Path(__file__).with_name('standardize.py'))
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class PublicationOwnershipTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='offline-normalizer-test-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.args = SimpleNamespace(source=self.root / 'source.zip', output=self.root / 'output.zip',
                                    report=self.root / 'report.json')
        with zipfile.ZipFile(self.args.source, 'w') as package:
            package.writestr('startup.tjs', 'synthetic independent resource')
        self.source_sha = hashlib.sha256(self.args.source.read_bytes()).hexdigest()
        self.normalizer = patch.object(MODULE, 'normalize', side_effect=lambda args, files, locators, output: {'files': files})
        self.normalizer.start()
        self.addCleanup(self.normalizer.stop)

    def test_does_not_remove_a_concurrently_replaced_report_symlink(self):
        real_link = MODULE.os.link

        def concurrent_link(source, destination):
            if destination == self.args.output:
                self.args.report.unlink()
                self.args.report.symlink_to(Path(source).with_name('conversion.json'))
                self.args.output.write_bytes(b'concurrent output')
                raise FileExistsError(errno.EEXIST, 'concurrent output')
            real_link(source, destination)

        with patch.object(MODULE.os, 'link', side_effect=concurrent_link):
            with self.assertRaises(FileExistsError):
                MODULE.standardize(self.args)
        self.assertTrue(self.args.report.is_symlink(), 'cleanup removed another writer\'s symlink')
        self.assertEqual(b'concurrent output', self.args.output.read_bytes())
        self.assertEqual(self.source_sha, hashlib.sha256(self.args.source.read_bytes()).hexdigest())

    def test_output_collision_removes_only_the_owned_report(self):
        real_link = MODULE.os.link

        def concurrent_link(source, destination):
            if destination == self.args.output:
                self.args.output.write_bytes(b'concurrent output')
                raise FileExistsError(errno.EEXIST, 'concurrent output')
            real_link(source, destination)

        with patch.object(MODULE.os, 'link', side_effect=concurrent_link):
            with self.assertRaises(FileExistsError):
                MODULE.standardize(self.args)
        self.assertFalse(self.args.report.exists())
        self.assertEqual(b'concurrent output', self.args.output.read_bytes())
        self.assertEqual([], list(self.root.glob('.normalize-content-*')))

    def test_publishes_verified_store_content_and_reclaims_owned_staging(self):
        report = MODULE.standardize(self.args)
        with zipfile.ZipFile(self.args.output) as package:
            self.assertEqual(['startup.tjs'], package.namelist())
            self.assertEqual(0, package.infolist()[0].compress_type)
            self.assertEqual(b'synthetic independent resource', package.read('startup.tjs'))
        self.assertEqual('unchanged', report['status'])
        self.assertEqual(report, json.loads(self.args.report.read_text()))
        self.assertEqual([], list(self.root.glob('.normalize-content-*')))
        self.assertEqual(self.source_sha, hashlib.sha256(self.args.source.read_bytes()).hexdigest())

    def test_matched_converter_error_never_publishes_an_unchanged_package(self):
        self.normalizer.stop()
        with patch.object(MODULE, 'normalize', side_effect=MODULE.FormatError('RUNTIME_TEST_CORRUPT_RESOURCE')):
            with self.assertRaisesRegex(MODULE.FormatError, 'RUNTIME_TEST_CORRUPT_RESOURCE'):
                MODULE.standardize(self.args)
        self.assertFalse(self.args.report.exists())
        self.assertFalse(self.args.output.exists())
        self.assertEqual([], list(self.root.glob('.normalize-content-*')))
        self.assertEqual(self.source_sha, hashlib.sha256(self.args.source.read_bytes()).hexdigest())


if __name__ == '__main__':
    unittest.main()
