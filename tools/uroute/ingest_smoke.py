# SPDX-License-Identifier: BSD-3-Clause
"""Hermetic routed/failed ingestion with physical filler exclusion and replay."""
import argparse
import json
import gzip
from pathlib import Path
import sys
import tempfile

from tools.uroute.features import Maps
from tools.uroute.ingest import ingest
from tools.uroute.maps import read, write
from tools.uroute.pool import publish_sample, read_manifest
from tools.uroute.postroute import postroute


def main():
    native, physical = (Path(value).resolve() for value in sys.argv[1:])
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
        controls.write_text(json.dumps({'prects_controls': {'ports': [], 'pins': [], 'reset_ports': [], 'reset_pins': [], 'override': False}}))
        definition = root / 'routed.def'
        text = '''VERSION 5.8 ; DESIGN tiny ; UNITS DISTANCE MICRONS 1000 ;
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
'''
        definition.write_text(text)
        drc = root / 'drc.rpt'
        drc.write_text('')
        args = argparse.Namespace(definition=definition, lef=[lef], liberty=liberty, controls=controls, pool=root / 'pool',
                                  placement_metrics=physical, native=native, drc=drc, livehd_export=None, last_global_overflow=None,
                                  source='external', status='routed', utilization=.1, bins=8, wirelength_report=None)
        lengths = root / 'wirelength.txt'
        lengths.write_text('tool net total_wl #pins \ndrt: n0 4.1 2\ndrt: n1 5.1 2\n')
        args.wirelength_report = lengths
        entry = ingest(args)
        sample = args.pool / entry['directory']
        geometry = json.loads((sample / 'geometry.json').read_text())
        assert [n['name'] for n in geometry['nodes'] if n.get('mask')] == ['filler'], geometry
        assert len(geometry['blockages']) == 1
        blockage = geometry['blockages'][0]
        assert blockage['kind'] == 'placement' and blockage['soft'] and blockage['component'] == 'u'
        assert blockage['rectangles'] == [[0., 0., 9000., 1000.]], blockage
        labels = read(sample / 'labels.bin')
        assert entry['wirelength_verification']['nets'] == 2
        assert labels.channels == 3 and max(labels.values[:65536]) > 1
        report = json.loads((sample / 'extraction.json').read_text())
        assert [n['wirelength_dbu'] for n in report['nets']] == [4100, 5100], report
        assert [n['detour'] for n in report['nets']] == [1, 1], report
        # Replay is immutable and does not create new support shots.
        assert ingest(args) == entry
        assert len(read_manifest(args.pool)['samples']) == 1
        # Importing the same DEF pair through an ODB or adding auxiliary
        # verification metadata must not manufacture a second support shot.
        alternative = dict(entry, source_odb_sha256='f' * 64)
        alternative.pop('id')
        alternative.pop('directory')
        same = publish_sample(args.pool, alternative, read(sample / 'features.bin'), labels,
                              extra_files={'odb_export.json': b'{"source":"alternate container"}\n'})
        assert same['id'] == entry['id'] and len(read_manifest(args.pool)['samples']) == 1
        observation = json.loads((args.pool / same['observation']['directory'] / 'observation.json').read_text())
        assert observation['metadata']['source_odb_sha256'] == 'f' * 64
        assert publish_sample(args.pool, alternative, read(sample / 'features.bin'), labels,
                              extra_files={'odb_export.json': b'{"source":"alternate container"}\n'}) == same
        # The ORFS post-route adapter consumes retained artifacts and explicit
        # controls, including compressed Liberty, without starting a router.
        job = root / 'job'
        (job / 'results').mkdir(parents=True)
        (job / 'reports').mkdir()
        (job / 'results/6_final.def.gz').write_bytes(gzip.compress(text.encode()))
        (job / 'reports/5_route_drc.rpt').write_bytes(drc.read_bytes())
        (job / 'uroute_controls.json').write_bytes(controls.read_bytes())
        (job / 'status.json').write_text('{"status":"completed"}')
        zipped_liberty = root / 'cells.lib.gz'
        zipped_liberty.write_bytes(gzip.compress(liberty.read_bytes()))
        (job / 'tech.lef').write_bytes(lef.read_bytes())
        environment = {'TECH_LEF': ['tech.lef'], 'SC_LEF': '', 'LIB_FILES': ['../cells.lib.gz'],
                       'CORE_UTILIZATION': '10', 'INPUT_PHYSICAL': 'results/6_final.def.gz'}
        (job / 'environment.json').write_text(json.dumps(environment))
        hook = argparse.Namespace(job=job, pool=args.pool, source='external', controls=None, definition=None,
                                  lef=None, liberty=None, drc=None, livehd_export=None, wirelength_report=lengths,
                                  status='auto', last_global_overflow=None, utilization=None, bins=8,
                                  placement_metrics=physical, native=native)
        published = postroute(hook)
        assert published['sample_id'] == entry['id'] and published['utilization_source'] == 'ORFS CORE_UTILIZATION'
        assert postroute(hook)['sample_id'] == entry['id']
        assert len(read_manifest(args.pool)['samples']) == 1
        saved_controls = (job / 'uroute_controls.json').read_bytes()
        (job / 'uroute_controls.json').unlink()
        (job / 'results/qor.json').write_bytes(saved_controls)
        assert postroute(hook)['sample_id'] == entry['id']
        (job / 'results/qor.json').unlink()
        try:
            postroute(hook)
        except ValueError as error:
            assert 'control metadata' in str(error), error
        else:
            raise AssertionError('missing controls silently guessed')
        (job / 'uroute_controls.json').write_bytes(saved_controls)
        (job / 'status.json').write_text('{"status":"failed"}')
        (job / 'final_report.complete').touch()  # A stale marker cannot override terminal failure.
        try:
            postroute(hook)
        except ValueError as error:
            assert 'not complete' in str(error), error
        else:
            raise AssertionError('failed job silently treated as routed')
        # Features exported from this same physical geometry yield zero gap.
        export = root / 'export'
        export.mkdir()
        (export / 'features.bin').write_bytes((sample / 'features.bin').read_bytes())
        (export / 'metadata.json').write_text(json.dumps(dict(entry, features_sha256=entry['features_sha256'])))
        args.livehd_export = export
        paired = ingest(args)
        observed = json.loads((args.pool / paired['observation']['directory'] / 'observation.json').read_text())
        assert all(g['normalized_rmse'] == 0 for g in observed['metadata']['feature_gap'])
        assert len(read_manifest(args.pool)['samples']) == 1
        # An explicit failed result retains its last overflow, without labels.
        args.livehd_export = None
        args.status = 'unrouted'
        args.last_global_overflow = root / 'overflow.bin'
        write(args.last_global_overflow, Maps(1, 2, 2, False, [0, 1, 0, 2], [(0, 2)]))
        definition.write_text(text.replace(' + ROUTED M3 ( 0 500 ) ( 4100 * )', '').replace(' + ROUTED M3 ( 4900 500 ) ( 10000 * )', ''))
        failed = ingest(args)
        failure_dir = args.pool / failed['directory']
        assert failed['status'] == 'unrouted' and not (failure_dir / 'labels.bin').exists()
        assert (failure_dir / 'last_global_overflow.bin').read_bytes() == args.last_global_overflow.read_bytes()
        args.last_global_overflow = None
        args.overflow_unavailable = 'Pin access failed before any global-route resource map was populated'
        unavailable = ingest(args)
        unavailable_dir = args.pool / unavailable['directory']
        assert unavailable['last_global_overflow_status'] == 'unavailable'
        assert unavailable['last_global_overflow_reason'] == args.overflow_unavailable
        assert not (unavailable_dir / 'labels.bin').exists() and not (unavailable_dir / 'last_global_overflow.bin').exists()
        args.overflow_unavailable = None
        try:
            ingest(args)
        except ValueError as error:
            assert '--last-global-overflow' in str(error), error
        else:
            raise AssertionError('unavailability silently inferred without an explicit reason')
        args.status, args.drc = 'routed', None
        try:
            ingest(args)
        except ValueError as error:
            assert 'requires --drc' in str(error), error
        else:
            raise AssertionError('missing DRC report silently treated as clean')


if __name__ == '__main__':
    main()
