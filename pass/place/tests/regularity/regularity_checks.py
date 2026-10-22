#!/usr/bin/env python3
"""Fast fixture/observer checks; the actual placer runs in the placement tag."""
import json
from pathlib import Path
import tempfile
import unittest

import generate
import render


class RegularityChecks(unittest.TestCase):
    def test_checked_in_sources_match_generator(self):
        for case in generate.CASES:
            self.assertEqual((generate.ROOT/(case['name']+'.prp')).read_text(), generate.source(case))
            if case.get('fixed_macros'):
                self.assertEqual((generate.ROOT/(case['name']+'.def')).read_text(), generate.anchor_floorplan(case))
        lib, lef = generate.technology()
        self.assertEqual((generate.ROOT/'fake.lib').read_text(), lib)
        self.assertEqual((generate.ROOT/'fake.lef').read_text(), lef)
        self.assertEqual(json.loads((generate.ROOT/'benchmarks.json').read_text()), generate.CASES)

    def test_topology_and_similarity_score(self):
        for case in generate.CASES:
            points, edges = generate.topology(case)
            cells = {}
            for i, (x, y) in enumerate(points):
                if case['pattern'] == 'hierarchy':
                    g, t = (y//2)*2+x//2, (y%2)*2+x%2
                    name = f'g{g}/t{t}/macro'
                else:
                    name = f't{i}/macro'
                # Deliberate reflection + rotation + translation + scale.
                p = complex(x, -y) * complex(3, 4) + complex(10, 20)
                cells[name] = dict(master='REG_MACRO', x=p.real, y=p.imag)
            self.assertAlmostEqual(render.scores(case, points, edges, cells)['pattern_rms_pitch'], 0)
            self.assertAlmostEqual(render.scores(case, points, edges, cells)['edge_length_cv'], 0)
            cells[next(iter(cells))]['x'] += 20
            self.assertGreater(render.scores(case, points, edges, cells)['pattern_rms_pitch'], .1)
            if case['pattern'] == 'serpentine':
                self.assertEqual(len(points), 32)
                self.assertEqual(len(edges), len(points))
                self.assertTrue(all(sum(i in e for e in edges) == 2 for i in range(len(points))))
                self.assertIn([0, 31], edges)
                self.assertNotIn([0, 15], edges)  # nearby runs must not become mesh shortcuts
                visited, pending = set(), [0]
                while pending:
                    vertex = pending.pop()
                    if vertex in visited:
                        continue
                    visited.add(vertex)
                    pending.extend(b if a == vertex else a for a, b in edges if vertex in (a, b))
                self.assertEqual(len(visited), len(points))
            elif case['pattern'] == 'hierarchy':
                self.assertEqual(len(edges), 32)
            else:
                self.assertEqual(len(edges), 2*case['side']*(case['side']-1))

    def test_four_corner_anchors_and_outside_return(self):
        case = next(c for c in generate.CASES if c['pattern'] == 'serpentine')
        points, edges = generate.topology(case)
        _, die, anchors = render.read_def(generate.ROOT/(case['name']+'.def'))
        self.assertEqual(len(anchors), 4)
        coordinates = {(c['x'], c['y']) for c in anchors.values()}
        xs, ys = {x for x, _ in coordinates}, {y for _, y in coordinates}
        self.assertEqual(len(xs), 2)
        self.assertEqual(len(ys), 2)
        self.assertEqual(coordinates, {(x, y) for x in xs for y in ys})
        self.assertTrue(all(c['status'] == 'FIXED' and c['master'] == 'REG_MACRO' for c in anchors.values()))
        self.assertTrue(all(die[0] <= x and x+40000 <= die[2] and die[1] <= y and y+40000 <= die[3] for x, y in coordinates))
        closing = next(route for a, b, route in render.reference_routes(case, points, edges) if (a, b) == (0, 31))
        self.assertEqual(closing[0], points[0])
        self.assertEqual(closing[-1], points[31])
        self.assertLess(min(x for x, _ in closing), min(x for x, _ in points))

    def test_def_and_png(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            source = directory/'tiny.def'
            source.write_text('''VERSION 5.8 ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 100000 100000 ) ;
COMPONENTS 2 ;
- t0/macro REG_MACRO + FIXED ( 10000 20000 ) N ;
- t0/logic XOR2 + PLACED ( 3000 4000 ) FS ;
END COMPONENTS
END DESIGN
''')
            units, die, cells = render.read_def(source)
            self.assertEqual(units, 1000)
            self.assertEqual(cells['t0/macro']['status'], 'FIXED')
            self.assertEqual(cells['t0/logic']['orientation'], 'FS')
            path = directory/'actual.png'
            render.actual(generate.CASES[0], units, die, cells, path)
            self.assertEqual(path.read_bytes()[:8], b'\x89PNG\r\n\x1a\n')
            with self.assertRaises(ValueError):
                render.actual(generate.CASES[0], units, die, cells, path, {})

    def test_collapsed_frame_is_not_perfect(self):
        case = generate.CASES[0]
        points, edges = generate.topology(case)
        cells = {f't{i}/macro': dict(master='REG_MACRO', x=0, y=0) for i in range(4)}
        self.assertIsNone(render.scores(case, points, edges, cells)['pattern_rms_pitch'])


if __name__ == '__main__':
    unittest.main()
