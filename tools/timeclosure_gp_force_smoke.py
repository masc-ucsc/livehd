#!/usr/bin/env python3
"""Hermetic native callback transport/geometry check; requires the co-development pin API."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        lef = root / 'cells.lef'
        lef.write_text('''VERSION 5.8 ;
BUSBITCHARS "[]" ; DIVIDERCHAR "/" ; UNITS DATABASE MICRONS 1000 ; END UNITS
LAYER M1 TYPE ROUTING ; DIRECTION HORIZONTAL ; PITCH 0.1 ; WIDTH 0.05 ; END M1
SITE core CLASS CORE ; SIZE 1 BY 1 ; SYMMETRY X Y ; END core
MACRO INV CLASS CORE ; ORIGIN 0 0 ; SIZE 1 BY 1 ; SITE core ; SYMMETRY X Y ;
 PIN A DIRECTION INPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.1 0.1 0.2 0.2 ; END END A
 PIN Y DIRECTION OUTPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.8 0.8 0.9 0.9 ; END END Y
END INV
END LIBRARY
''')
        lib = root / 'cells.lib'
        lib.write_text('''library(test){time_unit:"1ps";capacitive_load_unit(1,ff);
cell(INV){area:1;pin(A){direction:input;capacitance:2;}
pin(Y){direction:output;function:"!A";timing(){related_pin:A;timing_sense:negative_unate;
cell_rise(scalar){values("10");}cell_fall(scalar){values("12");}
rise_transition(scalar){values("3");}fall_transition(scalar){values("3");}
}}}}
''')
        design = root / 'original.def'
        design.write_text('''VERSION 5.8 ;
DIVIDERCHAR "/" ; BUSBITCHARS "[]" ; DESIGN top ; UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 20000 4000 ) ;
ROW r0 core 0 0 N DO 20 BY 1 STEP 1000 0 ;
ROW r1 core 0 1000 FS DO 20 BY 1 STEP 1000 0 ;
ROW r2 core 0 2000 N DO 20 BY 1 STEP 1000 0 ;
ROW r3 core 0 3000 FS DO 20 BY 1 STEP 1000 0 ;
COMPONENTS 2 ;
- u INV + PLACED ( 4000 0 ) N ;
- unused INV + PLACED ( 8000 0 ) N ;
END COMPONENTS
PINS 3 ;
- z + NET nz + DIRECTION OUTPUT + LAYER M1 ( 0 0 ) ( 10 10 ) + FIXED ( 18000 4000 ) N ;
- a + NET na + DIRECTION INPUT + LAYER M1 ( 0 0 ) ( 10 10 ) + FIXED ( 2000 0 ) N ;
- clk + NET nc + DIRECTION INPUT + LAYER M1 ( 0 0 ) ( 10 10 ) + FIXED ( 0 2000 ) N ;
END PINS
NETS 3 ;
- na ( PIN a ) ( u A ) ;
- nz ( u Y ) ( PIN z ) ;
- nc ( PIN clk ) ;
END NETS
END DESIGN
''')
        sdc = root / 'original.sdc'
        sdc.write_text('''current_design top
create_clock -name sys -period 100 [get_ports {clk}]
set_input_delay 10 -clock [get_clocks {sys}] -add_delay [get_ports {a}]
set_output_delay 0 -clock [get_clocks {sys}] -add_delay [get_ports {z}]
''')
        config = root / 'config.json'
        cfg = dict(liberty=[str(lib)], lefs=[str(lef)], sdc=str(sdc),
                   sdc_units=dict(time_ps=1, capacitance_ff=1),
                   wire_rc=dict(resistance_ohm_um=0, capacitance_ff_um=0),
                   **{'def': str(design)}, output=str(root/'force'), iterations=20,
                   observe_interval=10, bins=4, density=.5,
                   native_pin_timing=True, native_timing_start=0,
                   continuous_star_wire=True, compare_continuous_star=True)
        config.write_text(json.dumps(cfg))
        original = design.read_bytes()
        run = subprocess.run([binary, config], capture_output=True, text=True)
        assert run.returncode == 0, run.stderr
        summary = json.loads((root/'force/summary.json').read_text())
        assert summary['mode'] == 'exact_netlist_native_pin_force_gp_experiment'
        assert summary['optimizer_feedback_enabled'] and summary['legalized']
        assert not summary['synthesis_resynthesis'] and not summary['accepted_for_optimization']
        assert not summary['phase_controller_integrated'] and not summary['continuing_rollback_integrated']
        trace = [json.loads(line) for line in (root/'force/placement/native_gp_timing.jsonl').read_text().splitlines()]
        assert [row['iteration'] for row in trace] == [10, 20], trace
        assert trace[0]['weighted_pins'] > 0 and not trace[0]['prior_force_active']
        assert trace[1]['prior_force_active'] and trace[1]['reference_period_ps'] == trace[0]['reference_period_ps']
        print('native_pin_force_checkpoints=' + json.dumps(trace, sort_keys=True))
        assert design.read_bytes() == original
        assert (root/'force/out.def').exists()
        cfg['native_timing_rollback'] = True
        cfg['output'] = str(root/'rollback')
        config.write_text(json.dumps(cfg))
        continued = subprocess.run([binary, config], capture_output=True, text=True)
        assert continued.returncode == 0, continued.stderr
        continued_summary = json.loads((root/'rollback/summary.json').read_text())
        assert continued_summary['continuing_rollback_integrated'] and continued_summary['legalized']
        assert not continued_summary['phase_controller_integrated'] and not continued_summary['accepted_for_optimization']
        continued_trace = [json.loads(line) for line in (root/'rollback/placement/native_gp_timing.jsonl').read_text().splitlines()]
        assert [row['iteration'] for row in continued_trace] == [10, 20]
        assert (root/'rollback/out.def').exists() and design.read_bytes() == original
        cfg['native_timing_final_guard'] = True
        cfg['output'] = str(root/'legal_guard')
        config.write_text(json.dumps(cfg))
        guarded = subprocess.run([binary, config], capture_output=True, text=True)
        assert guarded.returncode == 0, guarded.stderr
        legal_guard = json.loads((root/'legal_guard/placement/native_legal_timing.json').read_text())
        assert legal_guard['reference_legal'] and legal_guard['final_evaluated']
        assert legal_guard['native_nonregression_pass'] and legal_guard['response_interval_complete']
        assert legal_guard['supported_gp_updates'] == 2
        assert legal_guard['candidate_period_ps'] == legal_guard['reference_period_ps']
        assert not (root/'legal_guard/placement/native_legalize_timing.json').exists()
        cfg.update(output=str(root/'native_legalize'),native_legalize_timing=True,
                   native_legalize_displacement_strength=9,native_legalize_net_strength=4)
        config.write_text(json.dumps(cfg))
        protected = subprocess.run([binary,config],capture_output=True,text=True)
        assert protected.returncode == 0, protected.stderr
        handoff = json.loads((root/'native_legalize/placement/native_legalize_timing.json').read_text())
        assert handoff['weighted_pins'] > 0 and handoff['protected_movable'] > 0 and handoff['protected_nets'] > 0
        assert not handoff['gp_history_updated'] and handoff['extra_gp_iterations'] == 0
        stages = [json.loads(row) for row in (root/'native_legalize/placement/native_legalize_stages.jsonl').read_text().splitlines()]
        assert [row['stage'] for row in stages] == ['legalization','detail'], stages
        assert all(row['legal'] and row['period_ps'] == legal_guard['reference_period_ps'] for row in stages)
        assert all(row['supported_gp_updates'] == 2 and not row['gp_history_updated'] for row in stages)
        protected_guard = json.loads((root/'native_legalize/placement/native_legal_timing.json').read_text())
        assert protected_guard['native_nonregression_pass'] and protected_guard['native_legalize_timing']
        assert (root/'native_legalize/out.def').exists() and design.read_bytes() == original
        cfg['native_timing_final_guard'] = False
        cfg['output'] = str(root/'invalid_legalize_guard')
        config.write_text(json.dumps(cfg))
        invalid = subprocess.run([binary,config],capture_output=True,text=True)
        assert invalid.returncode != 0 and 'final legal timing guard' in invalid.stderr
        cfg['native_timing_final_guard'] = True
        cfg['native_legalize_net_strength'] = 100
        config.write_text(json.dumps(cfg))
        invalid = subprocess.run([binary,config],capture_output=True,text=True)
        assert invalid.returncode != 0
        for name in ['native_legalize_timing','native_legalize_displacement_strength','native_legalize_net_strength']:
            del cfg[name]

        assert (root/'legal_guard/out.def').exists() and not legal_guard['accepted_for_optimization']
        # An incompatible density regime must not invent a timing observation
        # or force history. A strict final gate then rejects missing response.
        cfg.update(output=str(root/'deferred'), density=.01, native_timing_max_overflow=0,
                   native_timing_final_guard=False)
        config.write_text(json.dumps(cfg))
        deferred = subprocess.run([binary, config], capture_output=True, text=True)
        assert deferred.returncode == 0, deferred.stderr
        density_rows = [json.loads(line) for line in (root/'deferred/placement/native_gp_timing.jsonl').read_text().splitlines()]
        assert [r['iteration'] for r in density_rows] == [10, 20]
        assert all(r['deferred'] and not r['rollback'] and r['reference_period_ps'] is None for r in density_rows)
        cfg.update(output=str(root/'missing_response'), native_timing_final_guard=True)
        config.write_text(json.dumps(cfg))
        missing = subprocess.run([binary, config], capture_output=True, text=True)
        assert missing.returncode != 0 and 'response interval' in missing.stderr, missing.stderr
        rejected_final = json.loads((root/'missing_response/placement/native_legal_timing.json').read_text())
        assert rejected_final['final_evaluated'] and not rejected_final['native_nonregression_pass']
        assert rejected_final['supported_gp_updates'] == 0
        assert not (root/'missing_response/out.def').exists()
        cfg.pop('native_timing_max_overflow')
        cfg.update(density=.5, native_timing_final_guard=False)
        # Real geometry-dependent RC timing improves, then regresses. Verify
        # both requested and applied rollback, exact restored pin coordinates,
        # bounded temperature reduction and spent budget through legal output.
        cfg.update(output=str(root/'regression'), iterations=40, min_iterations=40,
                   wire_rc=dict(resistance_ohm_um=100, capacitance_ff_um=.1),
                   save_timing_checkpoints=True, star_admittance_correction=True,
                   star_max_pin_displacement_fraction=.01,
                   star_branch_relative_displacement=True)
        config.write_text(json.dumps(cfg))
        regression = subprocess.run([binary, config], capture_output=True, text=True)
        assert regression.returncode == 0, regression.stderr
        events = json.loads((root/'regression/placement/native_gp_transactions.json').read_text())
        assert events == dict(iterations=40, selected_iteration=20, rollback_history=[
            dict(iteration=30, restored_iteration=20), dict(iteration=40, restored_iteration=20)]), events
        rows = [json.loads(line) for line in (root/'regression/placement/native_gp_timing.jsonl').read_text().splitlines()]
        assert [r['iteration'] for r in rows if r.get('rollback')] == [30, 40], rows
        assert rows[1]['reference_period_ps'] < rows[0]['reference_period_ps']
        assert rows[2]['reference_period_ps'] > rows[2]['retained_period_ps']
        assert rows[3]['temperature_fraction'] < rows[2]['temperature_fraction'] < rows[1]['temperature_fraction']
        assert (root/'regression/checkpoint_20.pins.json').read_bytes() == (root/'regression/checkpoint_40.pins.json').read_bytes()
        assert (root/'regression/out.def').exists() and design.read_bytes() == original
        print('native_gp_applied_rollbacks=' + json.dumps(events, sort_keys=True))
        cfg.update(output=str(root/'legal_regression'), native_timing_final_guard=True)
        config.write_text(json.dumps(cfg))
        legal_regression = subprocess.run([binary, config], capture_output=True, text=True)
        assert legal_regression.returncode != 0 and 'final legal hard period regresses' in legal_regression.stderr, legal_regression.stderr
        failed_legal = json.loads((root/'legal_regression/placement/native_legal_timing.json').read_text())
        assert failed_legal['reference_legal'] and failed_legal['final_evaluated']
        assert failed_legal['response_interval_complete'] and not failed_legal['native_nonregression_pass']
        assert failed_legal['candidate_period_ps'] > failed_legal['reference_period_ps']
        assert not (root/'legal_regression/out.def').exists()
        print('native_legal_regression_rejected=' + json.dumps(failed_legal, sort_keys=True))
        # The reference needs actual legality proof, independently of timing.
        design.write_bytes(original.replace(b'PLACED ( 8000 0 )', b'PLACED ( 4000 0 )'))
        cfg.update(output=str(root/'illegal_reference'),
                   wire_rc=dict(resistance_ohm_um=0, capacitance_ff_um=0))
        config.write_text(json.dumps(cfg))
        bad_reference = subprocess.run([binary, config], capture_output=True, text=True)
        assert bad_reference.returncode != 0
        reference_audit = json.loads((root/'illegal_reference/placement/native_legal_timing.json').read_text())
        assert not reference_audit['reference_legal'] and not reference_audit['final_evaluated']
        assert not (root/'illegal_reference/out.def').exists()
        design.write_bytes(original)
        cfg['native_timing_final_guard'] = False
        # Ranking remains a gate: tied rise/fall timing does not become a
        # successful force run just because the continuous values agree.
        lib.write_text(lib.read_text().replace('values("12")', 'values("10")'))
        cfg['output'] = str(root/'unranked')
        config.write_text(json.dumps(cfg))
        rejected = subprocess.run([binary, config], capture_output=True, text=True)
        assert rejected.returncode != 0 and 'ranking' in rejected.stderr, rejected.stderr
        assert not (root/'unranked/out.def').exists()
        errors = [json.loads(line) for line in (root/'unranked/placement/native_gp_timing.jsonl').read_text().splitlines()]
        assert errors[-1]['rejected'] and not errors[-1]['accepted_for_optimization']
        prefix = root/'unranked/placement'/('native_gp_rejected_'+str(errors[-1]['iteration']))
        diagnostic = json.loads(Path(str(prefix)+'.comparison.json').read_text())
        assert diagnostic['diagnostic_only'] and not diagnostic['optimization_state_committed']
        comparison = diagnostic['comparison']
        assert comparison['supported_coverage_pass'] and comparison['numerical_gates_pass']
        assert not comparison['ranking_gates_pass'] and comparison['ranking']['tau_b'] is None
        assert diagnostic['reference']['pins'] and diagnostic['continuous']['pins']
        assert {p['name'] for p in diagnostic['reference']['pins']} == {p['name'] for p in diagnostic['continuous']['pins']}
        assert json.loads(Path(diagnostic['rc_frame']).read_text())
        assert json.loads(Path(diagnostic['pin_locations']).read_text())
        cfg.update(output=str(root/'unranked_retry'),native_timing_recalibrate_on_mismatch=True)
        config.write_text(json.dumps(cfg))
        retried = subprocess.run([binary, config], capture_output=True, text=True)
        assert retried.returncode != 0 and 'ranking' in retried.stderr, retried.stderr
        first = json.loads((root/'unranked_retry/placement/native_gp_rejected_10.comparison.json').read_text())
        second = json.loads((root/'unranked_retry/placement/native_gp_rejected_10_attempt_1.comparison.json').read_text())
        assert first['attempt'] == 0 and first['retry_planned']
        assert second['attempt'] == 1 and not second['retry_planned']
        assert not second['comparison']['ranking_gates_pass']
        assert not (root/'unranked_retry/out.def').exists()


if __name__ == '__main__':
    main()
