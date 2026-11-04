#!/usr/bin/env python3
"""Exercise group selection against real Git staged/unstaged/rename states."""

import importlib.util
import pathlib
import os
import subprocess
import tempfile
import unittest

SCRIPT = (pathlib.Path(os.environ['TEST_SRCDIR']) / os.environ.get('TEST_WORKSPACE', '_main') / 'scripts/regression.py'
          if 'TEST_SRCDIR' in os.environ else pathlib.Path(__file__).with_name('regression.py'))
spec = importlib.util.spec_from_file_location('regression', SCRIPT)
regression = importlib.util.module_from_spec(spec)
spec.loader.exec_module(regression)


class RegressionSelection(unittest.TestCase):
    def test_domains(self):
        cases = [
            (['inou/prp/parser.cpp'], 'frontend'),
            (['graph/cell.cpp'], 'frontend'),
            (['pass/place/place.cpp'], 'backend'),
            (['inou/lefdef/lef.cpp'], 'backend'),
            (['tools/uroute/pool.py'], 'backend'),
            (['pass/abc/abc.cpp'], 'all'),
            (['pass/usyn/usyn.cpp'], 'all'),
            (['pass/synth/map.cpp'], 'all'),
            (['lhd/lhd.cpp'], 'all'),
            (['MODULE.bazel'], 'all'),
            (['graph/cell.cpp', 'pass/place/place.cpp'], 'all'),
            ([], 'all'),
        ]
        for paths, expected in cases:
            with self.subTest(paths=paths):
                self.assertEqual(regression.select_group(paths), expected)

    def test_git_changes_and_renames(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            def git(*args):
                return subprocess.check_output(['git', *args], cwd=root, stderr=subprocess.DEVNULL)
            git('init', '-q')
            git('config', 'user.email', 'regression@example.invalid')
            git('config', 'user.name', 'Regression test')
            for name in ('graph/a.cpp', 'pass/place/b.cpp'):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('baseline\n')
            git('add', '.')
            git('commit', '-qm', 'baseline')
            base = git('rev-parse', 'HEAD').decode().strip()
            (root / 'graph/a.cpp').write_text('unstaged\n')
            (root / 'pass/place/new.cpp').write_text('untracked\n')
            self.assertEqual(regression.changed_paths(root), ['graph/a.cpp', 'pass/place/new.cpp'])
            git('add', '.')
            git('mv', 'pass/place/b.cpp', 'graph/b.cpp')
            git('commit', '-qm', 'changed')
            paths = regression.changed_paths(root, base)
            self.assertIn('pass/place/b.cpp', paths)
            self.assertIn('graph/b.cpp', paths)
            self.assertEqual(regression.select_group(paths), 'all')

    def test_explicit_and_missing_history(self):
        for flags, expected in [(['--backend'], 'backend'), (['--frontend'], 'frontend'),
                                (['--all'], 'all'), (['--base', 'nonexistent-regression-ref'], 'all')]:
            result = subprocess.run([str(SCRIPT), *flags, '--dry-run', '--', '-c', 'opt'],
                                    capture_output=True, text=True, check=True)
            self.assertIn('--config=' + expected, result.stdout)
            self.assertIn('-c opt', result.stdout)
            self.assertIn('//...', result.stdout)


if __name__ == '__main__':
    unittest.main()
