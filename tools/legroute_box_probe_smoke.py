"""Hermetic acceptance regressions for the box probe's calibration boundary."""
import json
import pathlib
import subprocess
import sys
import tempfile


def fixture(root):
    lef = ['VERSION 5.8 ;', 'UNITS DATABASE MICRONS 1000 ; END UNITS',
           'MANUFACTURINGGRID 0.001 ;',
           'SITE asap7sc7p5t CLASS CORE ; SIZE 0.2 BY 0.2 ; END asap7sc7p5t']
    for i in range(1, 5):
        layer = f'M{i}'
        direction = 'HORIZONTAL' if i % 2 == 0 else 'VERTICAL'
        lef.append(f'LAYER {layer} TYPE ROUTING ; DIRECTION {direction} ; '
                   f'PITCH 0.1 ; WIDTH 0.02 ; SPACING 0.02 ; END {layer}')
        if i < 4:
            lef.append(f'LAYER V{i} TYPE CUT ; WIDTH 0.01 ; SPACING 0.01 ; END V{i}')
    for i in range(1, 4):
        lef.append(f'VIA VIA{i}{i+1} DEFAULT LAYER M{i} ; RECT -0.01 -0.01 0.01 0.01 ; '
                   f'LAYER V{i} ; RECT -0.005 -0.005 0.005 0.005 ; '
                   f'LAYER M{i+1} ; RECT -0.01 -0.01 0.01 0.01 ; END VIA{i}{i+1}')
    lef.append('MACRO gate CLASS CORE ; ORIGIN 0 0 ; SIZE 0.2 BY 0.2 ; '
               'SITE asap7sc7p5t ; PIN A DIRECTION INPUT ; USE SIGNAL ; PORT '
               'LAYER M1 ; RECT 0.09 0.09 0.11 0.11 ; END END A END gate END LIBRARY')
    (root/'cells.lef').write_text('\n'.join(lef)+'\n')
    tracks = '\n'.join(f'TRACKS {"Y" if i % 2 == 0 else "X"} 100 DO '
                       f'{4 if i % 2 == 0 else 30} STEP 100 LAYER M{i} ;' for i in range(1, 5))
    (root/'input.def').write_text(f'''VERSION 5.8 ;
DESIGN probe ; UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 3000 400 ) ;
ROW r0 asap7sc7p5t 0 0 N DO 15 BY 1 STEP 200 0 ;
ROW r1 asap7sc7p5t 0 200 FS DO 15 BY 1 STEP 200 0 ;
{tracks}
COMPONENTS 2 ;
- a gate + PLACED ( 0 0 ) N ;
- b gate + PLACED ( 1000 0 ) N ;
END COMPONENTS
NETS 1 ; - n ( a A ) ( b A ) + USE SIGNAL ; END NETS
END DESIGN
''')
    box = {'col': 0, 'row': 0, 'rect_dbu': [0, 0, 1600, 400],
           'nets': [{'net': 0, 'name': 'n', 'clock': 0, 'power': 0, 'routed': 1,
                     'pins': [{'owner': 'a', 'pin': 'A', 'in_box': 1},
                              {'owner': 'b', 'pin': 'A', 'in_box': 1}],
                     'nodes': [], 'planned_shapes': [], 'planned_joins': []}],
           'foreign_planned': [], 'foreign_joins': [], 'metal': []}
    config = {'lefs': [str(root/'cells.lef')], 'def': str(root/'input.def'),
              'box': str(root/'box.json'), 'out': str(root/'result.json'),
              'strict': True, 'halo_tiles': 0, 'layers': ['M2', 'M3', 'M4'],
              'net_ms': 1000, 'patterns': 8192, 'box_ms': 5000}
    return box, config


def main():
    binary = str(pathlib.Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory() as d:
        root = pathlib.Path(d)
        box, config = fixture(root)

        def run(expected, raw_menu=None, **updates):
            request = dict(config, **updates)
            (root/'box.json').write_text(json.dumps(box))
            encoded = json.dumps(request)
            if raw_menu is not None:
                encoded = encoded[:-1] + ', \"menu\": ' + raw_menu + '}'
            (root/'config.json').write_text(encoded)
            (root/'result.json').unlink(missing_ok=True)
            p = subprocess.run([binary, str(root/'config.json')], capture_output=True,
                               text=True, timeout=20)
            assert p.returncode == expected, (p.returncode, p.stdout, p.stderr)
            return json.loads((root/'result.json').read_text()) if expected != 2 else p.stderr

        internal = run(0, menu={'M4': [100]})
        assert internal['internal_connectivity_connected'] == 1, internal
        assert internal['nets'][0]['connectivity_checked'], internal
        assert internal['menu_coordinates']['M4'] == [100], internal
        assert internal['certificate_status'] == 'internal_routing_connected'
        assert not internal['calibration_eligible']
        assert 'cell_placement_legality' in internal['unverified_obligations']

        # A named upper-layer budget must not disappear silently (the original probe ignored M4 menus).
        assert 'DEF track' in run(2, menu={'M4': [101]})
        assert 'invalid routing layer' in run(2, menu={'typo': [100]})
        assert 'duplicate' in run(2, menu={'M4': [100, 100]})
        assert 'duplicate routing layer' in run(2, raw_menu='{\"M4\":[100],\"M4\":[101]}')

        # An M2 obstruction requires M4 to connect the two pins; exercise the real budget gate.
        lef_path, def_path = root/'cells.lef', root/'input.def'
        original_lef, original_def = lef_path.read_text(), def_path.read_text()
        lef_path.write_text(original_lef.replace('END LIBRARY',
            'MACRO wall CLASS BLOCK ; ORIGIN 0 0 ; SIZE 0.2 BY 0.4 ; '
            'OBS LAYER M2 ; RECT 0 0 0.2 0.4 ; END END wall END LIBRARY'))
        wall_def = original_def.replace('COMPONENTS 2', 'COMPONENTS 3').replace(
            'END COMPONENTS', '- wall wall + FIXED ( 600 0 ) N ;\nEND COMPONENTS')
        def_path.write_text(wall_def)
        upper = run(0, menu={'M4': [100]})
        assert upper['nets'][0]['non_planar_length_dbu']['M4'] > 0, upper
        denied = run(1, menu={'M4': []})
        assert not denied['signal_jobs_ok'], denied
        def_path.write_text(wall_def.replace('TRACKS Y 100 DO 4 STEP 100 LAYER M4 ;', ''))
        absent = run(1, menu={'M4': []})
        assert absent['menu_coordinates']['M4'] == [], absent
        lef_path.write_text(original_lef)
        def_path.write_text(original_def)

        box['nets'][0]['routed'] = 0
        unclassified = run(1)
        assert unclassified['skipped_ordinary_routed0'] == ['n'], unclassified
        assert 'unclassified_ordinary_connections' in unclassified['unverified_obligations']
        assert not unclassified['all_ok']
        box['nets'][0]['routed'] = 1

        # Both same-net anchors exist, but nothing connects them. A successful tap is an attachment only.
        def_path.write_text(original_def.replace('( 1000 0 )', '( 2000 0 )'))
        box['nets'][0]['pins'][1]['in_box'] = 0
        box['nets'][0]['planned_shapes'] = [
            {'layer': 'M2', 'rect': [90, 90, 400, 110], 'state': 'checked'},
            {'layer': 'M2', 'rect': [1000, 90, 1500, 110], 'state': 'checked'}]
        attachment = run(1)
        assert attachment['signal_jobs_ok'], attachment
        assert not attachment['all_ok'] and not attachment['calibration_eligible']
        assert attachment['external_attachment_jobs'] == 1
        assert 'required_boundary_port_connectivity_groups' in attachment['unverified_obligations']
        assert not attachment['nets'][0]['connectivity_checked']
        print('Internal connectivity, external-island rejection, and upper-menu validation passed.')


if __name__ == '__main__':
    main()
