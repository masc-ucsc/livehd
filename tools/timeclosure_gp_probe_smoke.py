#!/usr/bin/env python3
"""Exact-netlist GP observation: native timing, blockages and fixed IO."""
import json
import math
import re
import subprocess
import sys
import tempfile
from pathlib import Path


def main():
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        lef = root / 'cells.lef'
        lef.write_text('''VERSION 5.8 ;
BUSBITCHARS "[]" ; DIVIDERCHAR "/" ;
UNITS DATABASE MICRONS 1000 ; END UNITS
LAYER M1 TYPE ROUTING ; DIRECTION HORIZONTAL ; PITCH 0.1 ; WIDTH 0.05 ; END M1
SITE core CLASS CORE ; SIZE 1 BY 1 ; SYMMETRY X Y ; END core
MACRO INV CLASS CORE ; ORIGIN 0 0 ; SIZE 1 BY 1 ; SITE core ; SYMMETRY X Y ;
 PIN A DIRECTION INPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.1 0.1 0.2 0.2 ; END END A
 PIN Y DIRECTION OUTPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.8 0.8 0.9 0.9 ; END END Y
END INV
END LIBRARY
''')
        liberty = root / 'cells.lib'
        liberty.write_text('''library(test){time_unit:"1ps";capacitive_load_unit(1,ff);
cell(INV){area:1;pin(A){direction:input;capacitance:2;}
pin(Y){direction:output;function:"!A";timing(){related_pin:A;timing_sense:negative_unate;
cell_rise(scalar){values("10");}cell_fall(scalar){values("10");}
rise_transition(scalar){values("3");}fall_transition(scalar){values("3");}
}}}}
''')
        netlist = root / 'original.def'
        netlist.write_text('''VERSION 5.8 ;
DIVIDERCHAR "/" ; BUSBITCHARS "[]" ; DESIGN top ; UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 20000 4000 ) ;
ROW r0 core 0 0 N DO 20 BY 1 STEP 1000 0 ;
ROW r1 core 0 1000 FS DO 6 BY 1 STEP 1000 0 ;
ROW r1b core 8000 1000 FS DO 12 BY 1 STEP 1000 0 ;
ROW r3 core 0 3000 FS DO 20 BY 1 STEP 1000 0 ;
COMPONENTS 2 ;
- u INV + PLACED ( 4000 0 ) N ;
- unused INV + PLACED ( 8000 0 ) N ;
END COMPONENTS
PINS 3 ;
- z + NET nz + DIRECTION OUTPUT + LAYER M1 ( 0 0 ) ( 10 10 ) + FIXED ( 18000 4000 ) N ;
- a + NET na + DIRECTION INPUT + LAYER M1 ( 0 0 ) ( 10 10 ) + PLACED ( 2000 0 ) N ;
- clk + NET nc + DIRECTION INPUT + LAYER M1 ( 0 0 ) ( 10 10 ) + FIXED ( 0 2000 ) N ;
END PINS
NETS 3 ;
- na ( PIN a ) ( u A ) ;
- nz ( u Y ) ( PIN z ) ;
- nc ( PIN clk ) ;
END NETS
BLOCKAGES 2 ;
- PLACEMENT RECT ( 0 0 ) ( 1000 4000 ) ;
- PLACEMENT + SOFT RECT ( 1000 3000 ) ( 3000 4000 ) ;
END BLOCKAGES
END DESIGN
''')
        sdc = root / 'original.sdc'
        sdc.write_text('''current_design top
create_clock -name sys -period 100 [get_ports {clk}]
set_input_delay 10 -clock [get_clocks {sys}] -add_delay [get_ports {a}]
set_output_delay 0 -clock [get_clocks {sys}] -add_delay [get_ports {z}]
''')
        config = root / 'config.json'
        cfg = dict(liberty=[str(liberty)], lefs=[str(lef)], sdc=str(sdc),
                   sdc_units=dict(time_ps=1, capacitance_ff=1),
                   wire_rc=dict(resistance_ohm_um=0, capacitance_ff_um=0),
                   **{'def': str(netlist)}, output=str(root/'run'), iterations=20,
                   observe_interval=10, bins=4, density=.5)
        config.write_text(json.dumps(cfg))
        original_bytes = netlist.read_bytes()
        subprocess.run([binary, config], check=True)
        assert netlist.read_bytes() == original_bytes
        summary = json.loads((root/'run/summary.json').read_text())
        assert summary['signal_instances'] == 2
        assert summary['placement_blockages'] == 2
        assert summary['solver_placement_blockages'] == 3
        assert summary['solver_rows'] == 4
        assert summary['samples'] == 2
        assert summary['minimum_gp_iterations'] == summary['maximum_gp_iterations'] == 20
        assert summary['legalized'] and not summary['synthesis_resynthesis']
        assert not summary['optimizer_feedback_enabled']
        samples = [json.loads(line) for line in (root/'run/observations.jsonl').read_text().splitlines()]
        assert [s['iteration'] for s in samples] == [10, 20]
        for sample in samples:
            assert sample['certified'], sample['unsupported']
            assert math.isclose(sample['wns_ps'], 80, rel_tol=1e-9, abs_tol=1e-9), sample
            assert math.isclose(sample['observed_achievable_period_ps'], 20, rel_tol=1e-9, abs_tol=1e-9), sample
            assert not sample['optimizer_feedback_enabled']
        output = (root/'run/out.def').read_text()
        assert 'COMPONENTS 2 ;' in output and 'BLOCKAGES 2 ;' in output
        assert '- nc ( PIN clk )' in output  # isolated clock net retained
        assert '+ PLACED ( 2000 0 ) N' in output  # temporary IO freeze restored
        assert 'ROW r1b core 8000 1000 FS DO 12' in output  # original segments restored
        assert subprocess.run([binary, config], capture_output=True).returncode != 0
        budget = json.loads((root/'run/placement/virtual_filler_budget.json').read_text())
        assert budget['policy'] == 'relative_whitespace_charge'
        assert budget['fraction'] == 1  # default full whitespace charge
        assert budget['requested_charge_area_um2'] == budget['available_charge_area_um2']
        # Archived absolute-count configurations must fail explicitly rather
        # than silently use a different policy with the current executable.
        invalid = dict(cfg, output=str(root/'obsolete_count'), virtual_filler_target_count=1000000)
        invalid_config = root/'obsolete_count.json'
        invalid_config.write_text(json.dumps(invalid))
        failed = subprocess.run([binary, invalid_config], capture_output=True, text=True)
        assert failed.returncode != 0 and 'area-relative virtual_filler_fraction' in failed.stderr
        # Same deterministic placement and full native timing with opt-in tree
        # reuse: no numerical change at either checkpoint or the final export.
        cfg.update(output=str(root/'cached'), reuse_steiner_rc=True, verify_reused_steiner_rc=True)
        cached_config = root/'cached.json'
        cached_config.write_text(json.dumps(cfg))
        subprocess.run([binary, cached_config], check=True)
        cached = [json.loads(line) for line in (root/'cached/observations.jsonl').read_text().splitlines()]
        for direct, reused in zip(samples, cached, strict=True):
            for field in ['iteration', 'rc_identity', 'wns_ps', 'tns_ps', 'observed_achievable_period_ps',
                          'certified', 'driver_fit_failures', 'hpwl_um', 'unsupported']:
                assert direct[field] == reused[field], (field, direct, reused)
            assert direct['rc_reuse'] is None
            assert reused['rc_reuse_verified'] and not direct['rc_reuse_verified']
            stats = reused['rc_reuse']
            assert stats['nets_inspected'] == 2 and stats['pins_inspected'] == 4
            assert stats['rebuilt'] + stats['reused'] == 2
        assert cached[0]['rc_reuse']['rebuilt'] == 2
        assert (root/'run/native_rc.json').read_bytes() == (root/'cached/native_rc.json').read_bytes()
        assert (root/'run/out.def').read_bytes() == (root/'cached/out.def').read_bytes()
        cfg.update(output=str(root/'star'), continuous_star_wire=True, compare_continuous_star=True)
        star_config = root/'star.json'
        star_config.write_text(json.dumps(cfg))
        subprocess.run([binary, star_config], check=True)
        stars = [json.loads(line) for line in (root/'star/observations.jsonl').read_text().splitlines()]
        assert stars[0]['star_calibration']['calibrated'] == 2
        for sample in stars:
            assert sample['wire_model'] == 'wire_scaled_star_elmore_v1'
            assert math.isfinite(sample['physical_density_overflow'])
            assert sample['star_calibration']['pins_evaluated'] == 4
            for metric in ['arrival_ps', 'required_ps', 'slew_ps', 'load_ff']:
                check = sample['star_steiner_comparison'][metric]
                assert check['coverage_disagreements'] == 0
                assert check['maximum_absolute_error'] < 1e-8, sample
                if metric != 'load_ff':
                    assert check['numerical_tolerance_failures'] == 0
            comparison = sample['star_steiner_comparison']
            assert comparison['numerical_gates_pass'] and comparison['supported_coverage_pass']
            assert comparison['near_critical']['compared_endpoint_transitions'] == 2
            # The two scalar INV endpoint transitions tie in this fixture.
            assert comparison['near_critical']['tau_b'] is None
            assert not comparison['comparison_gates_pass']
        assert (root/'run/native_rc.json').read_bytes() == (root/'star/native_rc.json').read_bytes()
        assert (root/'run/out.def').read_bytes() == (root/'star/out.def').read_bytes()
        cfg.update(output=str(root/'capture'), save_timing_checkpoints=True, capture_only=True,
                   star_admittance_correction=True, star_max_pin_displacement_fraction=.1,
                   star_branch_relative_displacement=True)
        capture_config = root/'capture.json'
        capture_config.write_text(json.dumps(cfg))
        subprocess.run([binary, capture_config], check=True)
        captured = [json.loads(line) for line in (root/'capture/observations.jsonl').read_text().splitlines()]
        assert json.loads((root/'capture/summary.json').read_text())['mode'] == 'checkpoint_capture_only'
        assert not (root/'capture/out.def').exists()
        assert not (root/'capture/placement/after_legalization.def').exists()
        checkpoints = root/'capture/checkpoints.json'
        assert len(json.loads(checkpoints.read_text())) == 2
        cfg.update(output=str(root/'replay'), save_timing_checkpoints=False, capture_only=False,
                   replay_checkpoints=str(checkpoints))
        replay_config = root/'replay.json'
        replay_config.write_text(json.dumps(cfg))
        subprocess.run([binary, replay_config], check=True)
        replayed = [json.loads(line) for line in (root/'replay/observations.jsonl').read_text().splitlines()]
        for a, b in zip(captured, replayed, strict=True):
            for field in ['iteration','rc_identity','certified','wns_ps','tns_ps','observed_achievable_period_ps',
                          'driver_fit_failures','hpwl_um','physical_density_overflow','wire_model',
                          'star_calibration','star_steiner_comparison','unsupported']:
                assert a[field] == b[field], (field,a[field],b[field])
        assert json.loads((root/'replay/summary.json').read_text())['mode'] == 'checkpoint_replay'
        assert not (root/'replay/out.def').exists()
        assert netlist.read_bytes() == original_bytes
        # Physical-only occupancy enters GP, legalization and the exported DEF
        # without introducing a cell or a timing model into the signal graph.
        occupancy = root/'occupancy.def'
        occupancy.write_text('''VERSION 5.8 ;
DESIGN top ; UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 20000 4000 ) ;
BLOCKAGES 1 ;
- PLACEMENT RECT ( 3000 0 ) ( 15000 1000 ) ;
END BLOCKAGES
END DESIGN
''')
        cfg = json.loads(config.read_text())
        cfg.update(output=str(root/'occupied'), fixed_occupancy_def=str(occupancy))
        occupied_config = root/'occupied.json'
        occupied_config.write_text(json.dumps(cfg))
        subprocess.run([binary, occupied_config], check=True)
        summary = json.loads((root/'occupied/summary.json').read_text())
        assert summary['signal_instances'] == 2 and summary['fixed_occupancy_rectangles'] == 1
        output = (root/'occupied/out.def').read_text()
        assert 'COMPONENTS 2 ;' in output and 'BLOCKAGES 3 ;' in output
        components = output.split('COMPONENTS 2 ;')[1].split('END COMPONENTS')[0]
        poses = re.findall(r'\+ PLACED \( (\d+) (\d+) \)', components)
        assert len(poses) == 2
        assert all(int(y) >= 1000 or int(x) >= 15000 or int(x) + 1000 <= 3000 for x, y in poses)
        assert (root/'occupied/fixed_occupancy.def').exists()
        occupancy.write_text(occupancy.read_text().replace('DESIGN top', 'DESIGN other'))
        cfg.update(output=str(root/'wrong_frame'))
        occupied_config.write_text(json.dumps(cfg))
        failed = subprocess.run([binary, occupied_config], capture_output=True, text=True)
        assert failed.returncode != 0 and 'matching DEF frame' in failed.stderr
        # An explicit smaller minimum permits the ordinary native convergence
        # decision. Leaving it unset preserves the bounded-observation default.
        # Use continuous whitespace here; the segmented-row fixture above
        # measures exclusion handling and does not promise GP convergence.
        convergence_def = root/'convergence.def'
        continuous = netlist.read_text().replace(
            'ROW r1 core 0 1000 FS DO 6 BY 1 STEP 1000 0 ;\nROW r1b core 8000 1000 FS DO 12 BY 1 STEP 1000 0 ;',
            'ROW r1 core 0 1000 FS DO 20 BY 1 STEP 1000 0 ;\nROW r2 core 0 2000 N DO 20 BY 1 STEP 1000 0 ;')
        continuous = re.sub(r'BLOCKAGES 2 ;.*?END BLOCKAGES\n', '', continuous, flags=re.S)
        convergence_def.write_text(continuous)
        cfg = json.loads(config.read_text())
        cfg.update(output=str(root/'convergence'), iterations=200, min_iterations=20, **{'def': str(convergence_def)})
        convergence_config = root/'convergence.json'
        convergence_config.write_text(json.dumps(cfg))
        subprocess.run([binary, convergence_config], check=True)
        summary = json.loads((root/'convergence/summary.json').read_text())
        history = json.loads((root/'convergence/placement/gp_history.json').read_text())
        assert summary['minimum_gp_iterations'] == 20 and summary['maximum_gp_iterations'] == 200
        assert 20 <= history['iterations'] < 200, history
        assert history['converged'] and summary['legalized']
        cfg.update(output=str(root/'bad_minimum'), min_iterations=201)
        convergence_config.write_text(json.dumps(cfg))
        failed = subprocess.run([binary, convergence_config], capture_output=True, text=True)
        assert failed.returncode != 0 and 'min_iterations must not exceed' in failed.stderr
        cfg.update(output=str(root/'bad_capture_minimum'), min_iterations=20,
                   capture_only=True, save_timing_checkpoints=True)
        convergence_config.write_text(json.dumps(cfg))
        failed = subprocess.run([binary, convergence_config], capture_output=True, text=True)
        assert failed.returncode != 0 and 'capture_only requires min_iterations equal' in failed.stderr


if __name__ == '__main__':
    main()
