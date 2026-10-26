# SPDX-License-Identifier: BSD-3-Clause
"""Resource-map presence, strict map decoding and failed-route diagnostics."""
from pathlib import Path
import tempfile
import unittest

from tools.uroute.odb import overflow_map


class OdbTest(unittest.TestCase):
    def test_resources_and_overflow(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'overflow.tsv'
            header = 'UROUTE_GRT1\n2 2 1000 0 0 4 2\n'
            rows = '0 0 1\n0 1 0\n1 0 2\n1 1 1\n'
            path.write_text(header + '2 2 2 10 12 2 1\n' + rows)
            maps, metadata = overflow_map(path)
            self.assertEqual(maps.values, [1, 0, 2, 1])
            self.assertFalse(maps.normalized)
            self.assertEqual(metadata['core'], [0, 0, 4, 2])
            self.assertEqual(metadata['overflow_tracks_sum'], 2)
            self.assertIn('not a detailed-wire', metadata['purpose'])
            # A measured clean map has resources and zero overflow. An absent
            # resource map must never turn into a fake successful zero map.
            zeros = rows.replace('0 0 1', '0 0 0').replace('1 0 2', '1 0 0').replace('1 1 1', '1 1 0')
            path.write_text(header + '2 2 2 10 8 0 0\n' + zeros)
            self.assertEqual(overflow_map(path)[0].values, [0, 0, 0, 0])
            for summary in ('2 2 2 0 0 0 0', '2 2 2 10 12 2 0', '2 2 2 10 12 0 1',
                            '2 2 2 10 1 2 1', '2 2 2 nan 12 2 1'):
                path.write_text(header + summary + '\n' + rows)
                with self.assertRaises(ValueError, msg=summary):
                    overflow_map(path)
            for bad_rows in (rows[:-4], rows.replace('1 0 2', '0 0 2'), rows + 'extra',
                             rows.replace('1 0 2', '1 0 nan'), rows.replace('1 0 2', '1 0 -2')):
                path.write_text(header + '2 2 2 10 12 2 1\n' + bad_rows)
                with self.assertRaises(ValueError):
                    overflow_map(path)


if __name__ == '__main__':
    unittest.main()
