# SPDX-License-Identifier: BSD-3-Clause
"""Hand-counted wires, capacity, detour, markers and unsupported DEF syntax."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from tools.uroute.extract import drc_markers, extract, routed_segments, verify_lengths
from tools.uroute.ingest import feature_gap
from tools.uroute.features import Maps

BINARY = str(Path(sys.argv.pop(1)).resolve()) if len(sys.argv) > 1 else None


def definition(paths):
    return 'UNITS DISTANCE MICRONS 1000 ;\nNETS 1 ;\n- n ( PIN a ) ( PIN b ) + ROUTED ' + paths + ' ;\nEND NETS\n'


def geometry():
    return {'geometry_version': 1, 'dbu': 1000, 'core': [0, 0, 4, 4], 'nodes': [],
            'layers': [{'name': 'M' + str(i), 'direction': 'VERTICAL' if i == 2 else 'HORIZONTAL',
                        'pitch_x': 1, 'pitch_y': 1, 'width': 2} for i in range(1, 4)],
            'nets': [{'name': 'n', 'enabled': True, 'pins': [[0, 1], [1, 4]]}]}


class ExtractTest(unittest.TestCase):
    def test_wire_labels(self):
        text = definition('M2 ( 1 0 1 ) ( * 4 ) NEW M3 ( 0 1 ) ( 4 * ) VIA23 NEW M2 ( 1 0 ) RECT ( -1 -1 1 1 )')
        markers = 'violation type: Short\n srcs: net:n\n bbox = (0.004, 0.004) - (0.004, 0.004) on Layer M3\n'
        labels, groups, report = extract(geometry(), text, markers, [4], 2, 2, 2, 2)
        self.assertEqual(groups.values[:4], [.5, 0, .5, 0])  # M2/3 horizontal
        self.assertEqual(groups.values[4:8], [.5, .5, 0, 0])
        self.assertEqual(labels.values[:4], [.5, .5, .5, 0])
        self.assertEqual(labels.values[4:8], [0, 0, 0, 1])
        self.assertEqual(labels.values[8:], [2, 2, 2, 0])
        self.assertEqual(report['nets'][0]['wirelength_dbu'], 8)
        self.assertEqual((report['vias'], report['rect_patches']), (1, 1))
        # Labels are measured directly on the requested network grid.
        large, _, _ = extract(geometry(), text, markers, [4], 2, 2, 8, 8)
        self.assertEqual(sum(large.values[64:128]), 1)

    def test_union_and_reject(self):
        routes, _ = routed_segments(definition('M3 ( 0 1 ) ( 3 * ) NEW M3 ( 1 1 ) ( 4 * )'), {'M3'})
        self.assertEqual(routes['n'], [('M3', 0, 1, 4, 1)])
        for path in ['M3 ( * 0 ) ( 4 0 )', 'M3 ( 0 0 ) ( 4 4 )', 'M3 ( 0 0 ) VIA23 ( 4 0 )',
                     'M9 ( 0 0 ) ( 4 0 )', 'M3 ( 0 0 ) RECT ( 1 2 )', 'M3 ( 0 0 ) VIRTUAL ( 4 0 )']:
            with self.assertRaises(ValueError, msg=path):
                routed_segments(definition(path), {'M3'})
        for report in ['not a report', 'violation type: Short\nsrcs: net:n',
                       'violation type: Short\nsrcs: net:n\nbbox = (1, 0) - (0, 0) on Layer M2']:
            with self.assertRaises(ValueError):
                drc_markers(report, 1000)
        self.assertEqual(drc_markers('', 1000), [])
        with self.assertRaisesRegex(ValueError, 'no wires'):
            extract(geometry(), definition('M2 ( 1 1 ) VIA23'), '', [4], 2, 2, 2, 2)

    def test_shape_length_and_ignored_rules(self):
        text = definition('M3 ( 0 0 0 ) ( 10 0 2 ) NEW M3 ( 10 0 ) RECT ( -3 -1 3 1 )')
        routes, report = routed_segments(text, {'M3'}, {'M3': 4})
        self.assertEqual(report['net_shape_lengths']['n'], 12)
        self.assertEqual(routes['n'], [('M3', 0, 0, 10, 0)])
        ndr = definition('M3 ( 0 0 ) ( 10 0 ) + NONDEFAULTRULE wide')
        with self.assertRaises(ValueError):
            routed_segments(ndr, {'M3'})
        self.assertEqual(routed_segments(ndr, {'M3'}, selected_nets=set())[0], {'n': []})

    def test_independent_lengths(self):
        report = {'nets': [{'net': 'n', 'wirelength_dbu': 1234}]}
        self.assertEqual(verify_lengths(report, 'tool net total_wl #pins \ndrt: n 1.234 2\n', 1000)['parsed_total_dbu'], 1234)
        for text in ['drt: n 2 2', 'drt: other 1.234 2', 'drt: n 1.234 2\ndrt: n 1.234 2']:
            with self.assertRaises(ValueError):
                verify_lengths(report, text, 1000)

    def test_native_flute(self):
        if not BINARY:
            self.skipTest('native binary not supplied')
        doc = geometry()
        doc['nets'].append({'name': 'square', 'pins': [[0, 0], [10, 0], [10, 10], [0, 10]], 'enabled': True})
        doc['nets'].append({'name': 'clock', 'pins': [[0, 0], [10, 10]], 'enabled': False})
        with tempfile.TemporaryDirectory() as scratch:
            source, output = Path(scratch) / 'geometry.json', Path(scratch) / 'flute.json'
            source.write_text(json.dumps(doc))
            subprocess.run([BINARY, 'trees', str(source), str(output)], check=True, timeout=10)
            self.assertEqual(json.loads(output.read_text()), [4, 30, None])

    def test_gap(self):
        before = Maps(4, 1, 1, True, [0, 0, 0, 0], [(0, 1)] * 4)
        after = Maps(4, 1, 1, True, [1, 0, .5, 0], [(0, 2)] * 4)
        self.assertEqual([v['normalized_rmse'] for v in feature_gap(before, after)], [1, 0, .5, 0])


if __name__ == '__main__':
    unittest.main()
