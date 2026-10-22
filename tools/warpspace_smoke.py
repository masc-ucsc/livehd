"""Independent generated geometry regression for the C++ mesh warp."""
import json
import pathlib
import random
import re
import subprocess
import sys
import tempfile

BINARY = str(pathlib.Path(sys.argv[1]).resolve())


def main():
    with tempfile.TemporaryDirectory() as scratch:
        root = pathlib.Path(scratch)
        lef = root / 'cells.lef'
        lef.write_text('''VERSION 5.8 ;
UNITS DATABASE MICRONS 1000 ; END UNITS
SITE row CLASS CORE ; SIZE 1 BY 1 ; END row
MACRO cell CLASS CORE ; ORIGIN 0 0 ; SIZE 1 BY 1 ; END cell
MACRO ram CLASS BLOCK ; ORIGIN 0 0 ; SIZE 10 BY 8 ; END ram
END LIBRARY
''')
        rng = random.Random(7)
        components = ['- fixed ram + FIXED ( 10000 70000 ) N + PROPERTY keep "yes" ;',
                      '- movable ram + PLACED ( 65000 65000 ) E ;']
        for i in range(600):
            x = max(1, min(98, rng.gauss(35 if i < 400 else 65, 3)))
            y = max(1, min(98, rng.gauss(35, 3)))
            components.append(f'- c{i} cell + PLACED ( {round(x*1000)} {round(y*1000)} ) N ;')
        text = '''VERSION 5.8 ;
# This comment and all non-coordinate input text must survive unchanged.
DESIGN synthetic ; UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 100000 100000 ) ;
ROW r row 0 0 N DO 100 BY 1 STEP 1000 0 ;
COMPONENTS %d ;
%s
END COMPONENTS
NETS 2 ;
- n0 ( c0 A ) ( c400 B ) ( PIN p ) ;
- n1 ( movable A ) ( fixed B ) ;
END NETS
END DESIGN
''' % (len(components), '\n'.join(components))
        source = root / 'in.def'
        source.write_text(text)
        command = [BINARY, '--lef', str(lef), '--def', str(source), '--bins', '8', '--pixels', '900', '--rho-max', '.18', '--iters', '3', '--time-limit', '3', '--report', str(root/'report.png'), '--out-def', str(root/'out.def')]
        run = subprocess.run(command, capture_output=True, text=True, timeout=18)
        assert run.returncode == 0, run.stdout + run.stderr
        report = json.loads((root/'report.json').read_text())
        assert report['mesh_invariants_passed']
        assert report['after']['max_density'] < report['before']['max_density'] * .8, report['after']
        assert report['after']['displacement_mean'] > 0
        assert report['macros_and_fixed_objects'][0]['displacement'] == 0
        assert (root/'report.png').read_bytes().startswith(b'\x89PNG\r\n\x1a\n')
        original = source.read_text()
        result = (root/'out.def').read_text()
        pattern = r'\+ PLACED \(\s*-?\d+\s+-?\d+\s*\)'
        assert re.sub(pattern, '+ PLACED ( X Y )', original) == re.sub(pattern, '+ PLACED ( X Y )', result)
        assert components[0] in result
        # Density equilibration contracts empty strips and can release physical
        # FIXED macros, while preserving their shape and DEF status text.
        balanced = subprocess.run(command + ['--movable-macros', '--balance', '1'], capture_output=True, text=True, timeout=6)
        assert balanced.returncode == 0, balanced.stdout + balanced.stderr
        balanced_report = json.loads((root/'report.json').read_text())
        macro = balanced_report['macros_and_fixed_objects'][0]
        assert macro['input_fixed'] and not macro['fixed']
        assert macro['displacement'] > 1, macro
        assert macro['dx'] < 0 and macro['dy'] > 0, macro  # empty left/top strips shrink
        assert balanced_report['mesh_invariants_passed']
        assert '+ FIXED' in (root/'out.def').read_text()
        strain = subprocess.run(command + ['--movable-macros', '--balance', '3', '--balance-model', 'strain'], capture_output=True, text=True, timeout=6)
        assert strain.returncode == 0, strain.stdout + strain.stderr
        assert json.loads((root/'report.json').read_text())['mesh_invariants_passed']
        preview_command = [BINARY, '--lef', str(lef), '--def', str(source), '--bins', '8', '--pixels', '900', '--iters', '3', '--time-limit', '3', '--balance', '3', '--balance-model', 'strain', '--deformation-preview', '--report', str(root/'preview.png')]
        preview = subprocess.run(preview_command, capture_output=True, text=True, timeout=6)
        assert preview.returncode == 0, preview.stdout + preview.stderr
        preview_report = json.loads((root/'preview.json').read_text())
        assert preview_report['deformation_preview'] and not preview_report['macro_rigidity_enforced']
        assert preview_report['mesh_invariants_passed']
        vertices = preview_report['mesh']['vertices']
        warped_macro_area = sum(f[6] for f in preview_report['mesh']['free_faces']
                                if 10 <= vertices[f[0]][0] < 20 and 70 <= vertices[f[0]][1] < 78)
        assert warped_macro_area < 80 * .9, warped_macro_area
        replayed = subprocess.run(preview_command + ['--replay-mesh', str(root/'preview.json'), '--report', str(root/'replayed.png'), '--density-scale', 'shared'], capture_output=True, text=True, timeout=3)
        assert replayed.returncode == 0, replayed.stdout + replayed.stderr
        replay_report = json.loads((root/'replayed.json').read_text())
        assert replay_report['after']['density'] == preview_report['after']['density']
        assert replay_report['after']['hpwl_cell_centers'] is None
        assert replay_report['passes'][0]['axis'] == 'replay'
        assert replay_report['density_palette_before_max'] == replay_report['density_palette_after_max']
        # Passive macros can cross mesh faces and need not contain a vertex.
        uniform = subprocess.run(preview_command + ['--preview-grid', 'uniform', '--density-scale', 'shared', '--report', str(root/'uniform.png')], capture_output=True, text=True, timeout=6)
        assert uniform.returncode == 0, uniform.stdout + uniform.stderr
        uniform_report = json.loads((root/'uniform.json').read_text())
        assert uniform_report['mesh_vertices'] == 81
        assert uniform_report['mesh_invariants_passed']
        assert uniform_report['reassigned_cell_centers'] == 0
        # Compact mass-only input: dense LEFT space expands, so a macro left
        # of center moves RIGHT/inward. No radial attraction or legalizer.
        grid = {'die': [0, 0, 100, 100], 'bins': 8, 'mass': [0.0]*64,
                'macros': [{'name': 'anchor', 'center': [30, 50], 'size': [10, 8]},
                           {'name': 'oversized', 'center': [90, 90], 'size': [40, 40]}]}
        for idx in [24, 25, 32, 33]:
            grid['mass'][idx] = 2
        (root/'grid.json').write_text(json.dumps(grid))
        fast_command = [BINARY, '--density-input', str(root/'grid.json'), '--balance', '3', '--balance-model', 'strain', '--iters', '2', '--pixels', '900', '--report', str(root/'fast.png')]
        fast = subprocess.run(fast_command, capture_output=True, text=True, timeout=6)
        assert fast.returncode == 0, fast.stdout + fast.stderr
        fast_report = json.loads((root/'fast.json').read_text())
        assert fast_report['smooth_window'] == 32
        assert abs(fast_report['total_input_mass'] - fast_report['total_output_mass']) < 1e-7
        assert fast_report['after_max_density'] < fast_report['before_max_density']
        anchor = fast_report['macros'][0]
        assert anchor['center'][0] > 30 and anchor['radial_component'] < 0, anchor
        assert anchor['size'] == [10, 8]
        assert fast_report['mesh_invariants_passed']
        assert (root/'fast.png').read_bytes().startswith(b'\x89PNG\r\n\x1a\n')
        extract = subprocess.run([BINARY, '--lef', str(lef), '--def', str(source), '--density-grid', '--bins', '8', '--iters', '0', '--pixels', '900', '--report', str(root/'extracted.png')], capture_output=True, text=True, timeout=3)
        assert extract.returncode == 0, extract.stdout + extract.stderr
        extracted = json.loads((root/'extracted.json').read_text())
        assert abs(extracted['total_input_mass'] - 600) < 1e-7
        assert abs(extracted['total_output_mass'] - 600) < 1e-7
        assert len(extracted['macros']) == 2 and extracted['iterations'] == 0
        # Uniform cells/free-area density stays stationary, including partial
        # macro bins. Overlapping macro records must reserve their UNION once.
        occupied_grid = {'die': [0, 0, 100, 100], 'bins': 8, 'mass': [],
                         'macros': [{'name': name, 'center': [21.875, 25], 'size': [43.75, 50]}
                                    for name in ['partial', 'overlap']]}
        for y in range(8):
            for x in range(8):
                blocked = max(0, min((x+1)*12.5, 43.75)-x*12.5) * max(0, min((y+1)*12.5, 50)-y*12.5)
                occupied_grid['mass'].append(.2 * (156.25-blocked))
        (root/'occupied_grid.json').write_text(json.dumps(occupied_grid))
        occupied_run = subprocess.run([BINARY, '--density-input', str(root/'occupied_grid.json'), '--macro-space', '--smooth-window', '8', '--smooth-blend', '.5', '--balance', '3', '--balance-model', 'strain', '--iters', '2', '--pixels', '900', '--report', str(root/'occupied.png')], capture_output=True, text=True, timeout=6)
        assert occupied_run.returncode == 0, occupied_run.stdout + occupied_run.stderr
        occupied_report = json.loads((root/'occupied.json').read_text())
        assert abs(occupied_report['input_macro_area']-2187.5) < 1e-6
        assert abs(occupied_report['transported_macro_area']-2187.5) < 1e-5
        assert abs(occupied_report['before_max_density']-.2) < 1e-8
        assert abs(occupied_report['after_max_density']-.2) < 1e-5
        assert max(abs(v) for m in occupied_report['macros'] for v in m['delta']) < 1e-3
        # Blend zero preserves a nonuniform source when capacity is inactive.
        varied = json.loads(json.dumps(occupied_grid))
        varied['mass'] = [mass*(.5+.5*(i%8)/7) for i,mass in enumerate(varied['mass'])]
        (root/'varied_input.json').write_text(json.dumps(varied))
        # Full-block and oversized filters use the same global free-area mean.
        expected_rho = sum(varied['mass']) / (10000-2187.5)
        full_targets = []
        for window in [8, 32, None]:
            report = root/f'full_window{window}.png'
            window_args = [] if window is None else ['--smooth-window', str(window)]
            full = subprocess.run([BINARY, '--density-input', str(root/'varied_input.json'), '--macro-space', *window_args, '--smooth-blend', '1', '--iters', '0', '--pixels', '900', '--report', str(report)], capture_output=True, text=True, timeout=3)
            assert full.returncode == 0, full.stdout + full.stderr
            data = json.loads(report.with_suffix('.json').read_text())
            assert max(abs(rho-expected_rho) for rho in data['requested_free_density']) < 1e-10
            full_targets.append(data['target_total_area'])
        assert full_targets[0] == full_targets[1] == full_targets[2]
        # A smaller reflected filter preserves the mean even at block edges.
        edge = {'die': [0, 0, 100, 100], 'bins': 8, 'mass': [0.0]*64, 'macros': []}
        edge['mass'][0], edge['mass'][7], edge['mass'][63] = 3, 7, 11
        (root/'edge_input.json').write_text(json.dumps(edge))
        edge_run = subprocess.run([BINARY, '--density-input', str(root/'edge_input.json'), '--smooth-window', '4', '--smooth-blend', '1', '--iters', '0', '--pixels', '900', '--report', str(root/'edge.png')], capture_output=True, text=True, timeout=3)
        assert edge_run.returncode == 0, edge_run.stderr
        edge_report = json.loads((root/'edge.json').read_text())
        assert abs(sum(edge_report['requested_free_density'])/64 - 21/10000) < 1e-12
        no_blend = subprocess.run([BINARY, '--density-input', str(root/'varied_input.json'), '--macro-space', '--smooth-window', '8', '--smooth-blend', '0', '--balance', '3', '--balance-model', 'strain', '--iters', '1', '--pixels', '900', '--report', str(root/'no_blend.png')], capture_output=True, text=True, timeout=3)
        assert no_blend.returncode == 0, no_blend.stdout + no_blend.stderr
        identity = json.loads((root/'no_blend.json').read_text())
        assert max(abs(a-b) for a,b in zip(identity['input_mass'], identity['mass'])) < 1e-4
        assert max(abs(v) for m in identity['macros'] for v in m['delta']) < 1e-3
        # The even-sized smoothing kernel must not introduce a half-bin bias.
        symmetric = {'die': [0, 0, 100, 100], 'bins': 8, 'mass': [0.0]*64, 'macros': []}
        for idx in [26, 29, 34, 37]:
            symmetric['mass'][idx] = 2
        (root/'symmetric_input.json').write_text(json.dumps(symmetric))
        sym = subprocess.run([BINARY, '--density-input', str(root/'symmetric_input.json'), '--smooth-window', '8', '--smooth-blend', '.5', '--iters', '0', '--pixels', '900', '--report', str(root/'symmetric.png')], capture_output=True, text=True, timeout=3)
        assert sym.returncode == 0, sym.stderr
        targets = json.loads((root/'symmetric.json').read_text())
        for y in range(8):
            for x in range(8):
                t = targets['target_total_area']
                assert abs(t[y*8+x]-t[y*8+7-x]) < 1e-7
                assert abs(t[y*8+x]-t[(7-y)*8+x]) < 1e-7
        preview_report['mesh']['vertices'][0][0] += 1
        (root/'bad_mesh.json').write_text(json.dumps(preview_report))
        mismatched = subprocess.run(preview_command + ['--replay-mesh', str(root/'bad_mesh.json'), '--report', str(root/'bad.png')], capture_output=True, text=True, timeout=3)
        assert mismatched.returncode == 1 and 'original grid differs' in mismatched.stderr
        forbidden_def = subprocess.run(preview_command + ['--out-def', str(root/'preview.def')], capture_output=True, text=True, timeout=3)
        assert forbidden_def.returncode == 1 and not (root/'preview.def').exists()
        # Reading our own output must work, independently of the mesh solver.
        render = subprocess.run([BINARY, '--lef', str(lef), '--def', str(root/'out.def'), '--render-only', '--report', str(root/'render.png'), '--pixels', '900'], capture_output=True, text=True, timeout=3)
        assert render.returncode == 0, render.stderr
        # Fixed standard cells remain part of the measured cell-area density.
        source.write_text(text.replace('- c0 cell + PLACED', '- c0 cell + FIXED'))
        fixed_render = subprocess.run([BINARY, '--lef', str(lef), '--def', str(source), '--render-only', '--bins', '8', '--report', str(root/'fixed.png')], capture_output=True, text=True, timeout=3)
        assert fixed_render.returncode == 0, fixed_render.stderr
        fixed_metrics = json.loads((root/'fixed.json').read_text())
        assert abs(sum(fixed_metrics['before']['density']) - 600*64/10000) < 1e-9
        protected = subprocess.run(command + ['--movable-macros', '--balance', '1'], capture_output=True, text=True, timeout=6)
        assert protected.returncode == 0, protected.stdout + protected.stderr
        protected_report = json.loads((root/'report.json').read_text())
        fixed_cell = next(m for m in protected_report['macros_and_fixed_objects'] if m['name'] == 'c0')
        assert fixed_cell['fixed'] and fixed_cell['displacement'] == 0
        source.write_text(text.replace('- fixed ram + FIXED', '- fixed ram + COVER'))
        covered = subprocess.run(command + ['--movable-macros', '--balance', '1'], capture_output=True, text=True, timeout=6)
        assert covered.returncode == 0, covered.stdout + covered.stderr
        assert json.loads((root/'report.json').read_text())['macros_and_fixed_objects'][0]['displacement'] == 0
        source.write_text(text)
        # A globally impossible cap is a clear failure, not a successful "cap".
        impossible = subprocess.run(command + ['--rho-max', '.0001'], capture_output=True, text=True, timeout=3)
        assert impossible.returncode == 1 and 'globally infeasible' in impossible.stderr
        # No macros exercises the unconstrained mesh boundary independently.
        no_macros = text.replace(components[0] + '\n', '').replace(components[1] + '\n', '').replace('COMPONENTS 602', 'COMPONENTS 600').replace('NETS 2', 'NETS 1').replace('- n1 ( movable A ) ( fixed B ) ;\n', '')
        source.write_text(no_macros)
        no_macro_run = subprocess.run(command, capture_output=True, text=True, timeout=5)
        assert no_macro_run.returncode == 0, no_macro_run.stdout + no_macro_run.stderr
        assert json.loads((root/'report.json').read_text())['macros_and_fixed_objects'] == []
        # Exact macro edges can leave faces much narrower than .01 * bin pitch.
        source.write_text(text.replace('( 65000 65000 ) E', '( 20001 70000 ) E'))
        thin = subprocess.run(command + ['--rho-max', '3'], capture_output=True, text=True, timeout=5)
        assert thin.returncode == 0, thin.stdout + thin.stderr
        source.write_text(text.replace('( 65000 65000 ) E', '( 10000 70000 ) E'))
        overlap = subprocess.run(command, capture_output=True, text=True, timeout=3)
        assert overlap.returncode == 1 and 'overlapping rigid objects' in overlap.stderr
        print('density reduction, no-fold invariants, rigid macros, no-macro and thin-face cases, verbatim DEF, PNG, reread and invalid input: passed')


if __name__ == '__main__':
    main()
