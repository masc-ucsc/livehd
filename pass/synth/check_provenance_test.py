#!/usr/bin/env python3
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
import copy
import hashlib
import json
import pathlib
import tempfile
import unittest

from check_provenance import check


class ProvenanceChecker(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temp.name) / 'qor.json.provenance'
        (self.root / 'files').mkdir(parents=True)
        (self.root / 'files/0.bin').write_bytes(b'abc')
        self.manifest = {'schema_version': 1, 'kind': 'synth_provenance',
                         'scope': 'kernel_observed_inputs_before_mapping', 'dependency_closure_certified': False,
                         'capture_complete': True, 'byte_limit': 64, 'entry_limit': 4, 'bytes': 3,
                         'context': {'inputs': ['/source.v']}, 'files': [
                             {'path': '/source.v', 'status': 'captured', 'blob': 'files/0.bin', 'bytes': 3,
                              'sha256': hashlib.sha256(b'abc').hexdigest()}]}
        self.save()

    def tearDown(self):
        self.temp.cleanup()

    def save(self):
        (self.root / 'manifest.json').write_text(json.dumps(self.manifest))

    def test_integrity_and_report_binding(self):
        self.assertTrue(check(self.root)['capture_complete'])
        report = self.root.parent / 'qor.json.synth.json'
        report.write_text(json.dumps({'provenance': {'directory': self.root.name, 'capture': {
            'manifest_sha256': hashlib.sha256((self.root / 'manifest.json').read_bytes()).hexdigest(),
            'captured_files': 1, 'omitted_files': 0, 'bytes': 3, 'capture_complete': True}}}))
        check(self.root, report)
        self.manifest['context']['inputs'] = ['/different.v']
        self.save()
        with self.assertRaises(ValueError):
            check(self.root, report)

    def test_corrupt_or_escaping_blob_is_rejected(self):
        original = copy.deepcopy(self.manifest)
        for update in [{'sha256': '0' * 64}, {'blob': '../outside'}, {'bytes': 2}]:
            self.manifest = copy.deepcopy(original)
            self.manifest['files'][0].update(update)
            self.save()
            with self.subTest(update=update), self.assertRaises(ValueError):
                check(self.root)
        self.manifest = original
        self.save()
        (self.root / 'files/0.bin').write_bytes(b'bad')
        with self.assertRaises(ValueError):
            check(self.root)

    def test_symlinks_and_false_completeness_are_rejected(self):
        blob = self.root / 'files/0.bin'
        blob.unlink()
        target = self.root.parent / 'target'
        target.write_bytes(b'abc')
        blob.symlink_to(target)
        with self.assertRaises(ValueError):
            check(self.root)
        blob.unlink()
        blob.write_bytes(b'abc')
        self.manifest['files'].append({'path': '/missing', 'status': 'byte_limit'})
        self.save()
        with self.assertRaises(ValueError):
            check(self.root)
        self.manifest['capture_complete'] = False
        self.save()
        self.assertFalse(check(self.root)['capture_complete'])


if __name__ == '__main__':
    unittest.main()
