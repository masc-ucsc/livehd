# SPDX-License-Identifier: BSD-3-Clause
import unittest

from tools.uroute.overflow import marker_map


def marker(direction='Horizontal', counts='capacity:1 usage:3 congestion:2', box='(0, 0) - (1, 2)'):
    return f'violation type: {direction} congestion\n\tsrcs: net:a\n\tcomment: {counts}\n\tbbox = {box} on Layer -\n'


class OverflowTest(unittest.TestCase):
    def test_area_average_directions_and_core_clipping(self):
        text = marker() + marker('Vertical', 'capacity:2 usage:3 congestion:1', '(1, 0) - (2, 2)')
        maps, metadata = marker_map(text, [0, 0, 2, 2], 1, bins=2)
        self.assertEqual(maps.values, [2, 2, 1, 1])
        self.assertEqual(metadata['emitted_counter_sums']['overflow'], 3)
        self.assertIn('unknown', metadata['coverage'])
        self.assertIn('lower bound', metadata['map_role'])
        both, _ = marker_map(marker() + marker('Vertical'), [0, 0, 2, 2], 1, bins=2)
        self.assertEqual(both.values, [4, 4, 0, 0])
        # The marker covers half of each tile, so its area average halves.
        clipped, _ = marker_map(marker(box='(-1, 0) - (.5, 2)'), [0, 0, 2, 2], 1, bins=2)
        self.assertEqual(clipped.values, [1, 1, 0, 0])
        physical, _ = marker_map(text, [0, 0, 2000, 2000], 1000, bins=2)
        self.assertEqual(physical.values, maps.values)

    def test_refuse_missing_corrupt_or_different_report_types(self):
        for text in ['', marker() + marker(), marker(counts='capacity:1 usage:3 congestion:1'),
                     marker().replace('Horizontal congestion', 'Short'), marker().replace(' on Layer -', ' on Layer M2'),
                     marker().replace('\tcomment: capacity:1 usage:3 congestion:2\n', ''),
                     marker(box='(0, 0) - (0, 2)'), marker() + 'unknown',
                     marker().replace('bbox =', 'comment: capacity:1 usage:3 congestion:2\n\tbbox =')]:
            with self.assertRaises(ValueError, msg=text):
                marker_map(text, [0, 0, 2, 2], 1, bins=2)


if __name__ == '__main__':
    unittest.main()
