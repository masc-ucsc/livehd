# SPDX-License-Identifier: BSD-3-Clause
"""Hermetic end-to-end scorer run on a tiny routed DEF: geometry, FLUTE lengths, labels, baselines."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest import mock

from tools.routetruth import extract
from tools.routetruth.baseline import baseline
from tools.routetruth.maps import read
from tools.routetruth.score import absolute_channel, metrics


def main():
    flute, physical, rudy = (Path(value).resolve() for value in sys.argv[1:])
    with tempfile.TemporaryDirectory() as scratch:
        root = Path(scratch)
        lef = root / 'tech.lef'
        lef.write_text('''VERSION 5.8 ; UNITS DATABASE MICRONS 1000 ; END UNITS
SITE core CLASS CORE ; SIZE 1 BY 1 ; END core
LAYER M1 TYPE ROUTING ; DIRECTION HORIZONTAL ; PITCH 0.1 ; WIDTH 0.05 ; END M1
LAYER M2 TYPE ROUTING ; DIRECTION VERTICAL ; PITCH 0.1 ; WIDTH 0.05 ; END M2
LAYER M3 TYPE ROUTING ; DIRECTION HORIZONTAL ; PITCH 0.1 ; WIDTH 0.05 ; END M3
MACRO BUF CLASS CORE ; ORIGIN 0 0 ; SIZE 1 BY 1 ;
 PIN A DIRECTION INPUT ; PORT LAYER M1 ; RECT 0.05 0.45 0.15 0.55 ; END END A
 PIN Y DIRECTION OUTPUT ; PORT LAYER M1 ; RECT 0.85 0.45 0.95 0.55 ; END END Y
END BUF
MACRO FILL CLASS CORE SPACER ; ORIGIN 0 0 ; SIZE 1 BY 1 ; END FILL
END LIBRARY
''')
        liberty = root / 'cells.lib'
        liberty.write_text('library(test) { cell(BUF) { pin(A) { direction: input; } '
                           'pin(Y) { direction: output; function: "A"; } } }')
        controls = root / 'controls.json'
        controls.write_text(json.dumps({'prects_controls': {'ports': [], 'pins': [], 'reset_ports': [], 'reset_pins': [],
                                                            'override': False}}))
        definition = root / 'routed.def'
        definition.write_text('''VERSION 5.8 ; DESIGN tiny ; UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 10000 1000 ) ;
ROW r0 core 0 0 N DO 10 BY 1 STEP 1000 0 ;
COMPONENTS 2 ;
- u BUF + PLACED ( 4000 0 ) N ;
- filler FILL + PLACED ( 5000 0 ) N ;
END COMPONENTS
BLOCKAGES 1 ;
- PLACEMENT + SOFT + COMPONENT u RECT ( 0 0 ) ( 9000 1000 ) ;
END BLOCKAGES
PINS 2 ;
- a + NET n0 + DIRECTION INPUT + FIXED ( 0 500 ) N ;
- z + NET n1 + DIRECTION OUTPUT + FIXED ( 10000 500 ) N ;
END PINS
NETS 2 ;
- n0 ( PIN a ) ( u A ) + ROUTED M3 ( 0 500 ) ( 4100 * ) ;
- n1 ( u Y ) ( PIN z ) + ROUTED M3 ( 4900 500 ) ( 10000 * ) ;
END NETS
END DESIGN
''')
        drc = root / 'drc.rpt'
        drc.write_text('violation type: Short\n srcs: net:n0 net:n1\n bbox = ( 4.4, 0.4 ) - ( 4.6, 0.6 ) on Layer M3\n')
        lengths = root / 'wirelength.txt'
        lengths.write_text('tool net total_wl #pins \ndrt: n0 4.1 2\ndrt: n1 5.1 2\n')
        # 1. Physical geometry of the routed DEF, through LiveHD's LEF/DEF readers.
        geometry_path = root / 'geometry.json'
        command = [str(physical), '--def', str(definition), '--liberty', str(liberty), '--controls', str(controls),
                   '--output', str(root / 'physical.json'), '--route-geometry', str(geometry_path), '--lef', str(lef)]
        subprocess.run(command, check=True, timeout=120)
        geometry = json.loads(geometry_path.read_text())
        assert geometry['geometry_version'] == 1 and geometry['top'] == 'tiny', geometry
        assert [n['name'] for n in geometry['nodes'] if n.get('mask')] == ['filler'], geometry
        assert len(geometry['blockages']) == 1
        blockage = geometry['blockages'][0]
        assert blockage['kind'] == 'placement' and blockage['soft'] and blockage['component'] == 'u'
        assert blockage['rectangles'] == [[0., 0., 9000., 1000.]], blockage
        # 2. FLUTE lengths: the detour denominator.
        flute_path = root / 'flute.json'
        subprocess.run([str(flute), str(geometry_path), str(flute_path)], check=True, timeout=120)
        assert json.loads(flute_path.read_text()) == [4100, 5100]
        # 3. Labels, with the parsed wire lengths checked against the OpenROAD report.
        labels_dir = root / 'labels'

        def run_extract(output):
            arguments = ['extract', '--geometry', str(geometry_path), '--def', str(definition), '--drc', str(drc),
                         '--flute-lengths', str(flute_path), '--wirelength-report', str(lengths), '--bins', '32',
                         '--output', str(output)]
            with mock.patch.object(sys, 'argv', arguments):
                extract.main()

        run_extract(labels_dir)
        labels = read(labels_dir / 'labels.bin')
        groups = read(labels_dir / 'layer_groups.bin')
        assert (labels.channels, labels.bins_x, labels.bins_y) == (3, 32, 32) and not labels.normalized
        assert (groups.channels, groups.bins_x, groups.bins_y) == (6, 32, 32)
        assert max(absolute_channel(labels, 0)) > 1  # one M3 wire over less than one nominal M3 track per tile
        assert sum(absolute_channel(labels, 1)) == 1  # one DRC marker
        report = json.loads((labels_dir / 'extraction.json').read_text())
        assert report['label_version'] == 1 and report['label_grid'] == [32, 32], report
        assert [n['wirelength_dbu'] for n in report['nets']] == [4100, 5100], report
        assert [n['detour'] for n in report['nets']] == [1, 1], report
        assert report['drc_markers'] == 1 and report['drc_outside_core'] == 0
        assert report['wirelength_verification']['nets'] == 2
        # A wrong report is an error, not a silently unverified label.
        lengths.write_text('tool net total_wl #pins \ndrt: n0 4.2 2\ndrt: n1 5.1 2\n')
        try:
            run_extract(root / 'rejected')
        except ValueError as error:
            assert 'wire-length mismatch' in str(error), error
        else:
            raise AssertionError('a wrong wire-length report was accepted')
        assert not (root / 'rejected').exists()
        # 4. RUDY and RSMT baselines against the routed utilization.
        scores = baseline(geometry, labels, root / 'scores', rudy, bins=8)
        assert set(scores['models']) == {'rudy', 'rsmt'} and scores['label_grid'] == [32, 32]
        utilization = absolute_channel(labels, 0)
        for model, variants in scores['models'].items():
            for mode in ('base', 'enhanced'):
                values = variants[mode]
                assert values['tiles'] == 1024 and values['rmse'] >= 0, (model, mode, values)
                assert values['reference_range'] == max(utilization) - min(utilization)
        assert json.loads((root / 'scores/baseline.json').read_text()) == scores
        # 5. A map scores perfectly against itself, through the map files.
        same = metrics(utilization, absolute_channel(read(labels_dir / 'labels.bin'), 0), 32, 32)
        assert same['pearson'] == same['spearman'] == 1 and same['nrms'] == 0, same
        # Every wire tile of this fixture ties at the hotspot cutoff, so membership is fractional.
        assert abs(same['ssim'] - 1) < 1e-9 and 0 < same['hotspot_top5_overlap'] <= 1, same


if __name__ == '__main__':
    main()
