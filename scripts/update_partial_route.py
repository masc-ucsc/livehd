#!/usr/bin/env python3
"""Render the current routing comparison from saved evaluation evidence."""
from datetime import datetime, timezone
from hashlib import sha256
from html import escape
from math import isfinite
from pathlib import Path
import json


ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / 'tmp/route_cts_eval_20261021'
SOURCES = {}
SYNC_SOURCES = {}


def read(relative):
    path = BASE / relative
    if not path.is_file():
        return None
    data = path.read_bytes()
    value = json.loads(data)
    SOURCES[relative] = sha256(data).hexdigest()
    return value


def link(relative, label):
    return f'<a href="../tmp/route_cts_eval_20261021/{escape(relative, quote=True)}">{escape(label)}</a>'


def read_sync(relative):
    path = ROOT / 'tmp/remote_sync_20261021' / relative
    data = path.read_bytes()
    SYNC_SOURCES[relative] = sha256(data).hexdigest()
    return json.loads(data)


def sync_link(relative, label):
    return f'<a href="../tmp/remote_sync_20261021/{escape(relative, quote=True)}">{escape(label)}</a>'


def number(value, digits=3):
    return 'Pending' if value is None else f'{value:,.{digits}f}'


def cell(text, small=''):
    return f'<td>{text}' + (f'<small>{small}</small>' if small else '') + '</td>'


def performance_snapshot():
    """Keep latest-source speed measurements separate from route results."""
    base = ROOT / 'tmp/remote_sync_20261022'
    rows = []
    for design in ('cnn', 'coralnpu'):
        for variant in ('baseline', 'candidate'):
            closed = base / design / variant / 'closed.json'
            if not closed.is_file():
                continue
            data = json.loads(closed.read_text())
            assert data['status'] == 'pass_cold_and_two_warm'
            result = {r['stage']: r for r in data['results']}
            assert len(result) == 3
            cold, warm = result['cold'], result['warm2']
            relative = str(closed.relative_to(ROOT / 'tmp'))
            href = '../tmp/' + relative
            rows.append('<tr>' + cell(design.upper() if design == 'cnn' else 'CoralNPU')
                        + cell(variant)
                        + cell(number(cold['run']['wall_s'], 2))
                        + cell(number(warm['run']['wall_s'], 2))
                        + cell('<a href="' + escape(href, quote=True) + '">Closed three-run evidence</a>',
                               'Warm1 and warm2 DEF hashes match cold') + '</tr>')
    if not rows:
        return ''
    notes = []
    comparison = base / 'speed_comparison.json'
    if comparison.is_file():
        for row in json.loads(comparison.read_text())['rows']:
            if row['status'] != 'closed_match':
                continue
            change = row['improvements']['cold']
            notes.append(escape(row['design']) + ': cold wall change '
                         + number(-100 * change['wall_reduction'], 2) + '%, CPU change '
                         + number(-100 * change['task-clock_reduction'], 2) + '%, instruction change '
                         + number(-100 * change['instructions_reduction'], 2)
                         + '%. Final DEF, macro counts, cell/net counts and native setup WNS/TNS match exactly.')
    return ('<h2>Frozen pre-terminal-flow Coral / CNN speed study</h2>'
            '<p>Both arms use LiveHD e94655bb7 and DREAMPlace ac995f900. Baseline uses OpenTimer '
            '65ff460ef; candidate uses published shared-wire-response optimization 7718c2866. '
            'FP plan is disabled, macro and final placement are enabled, '
            'and incremental synthesis and resynthesis remain enabled. Each build has its own fresh '
            'workdir: cold, warm1, then authoritative warm2. Measurements include perf record/stat '
            'on a shared host; these are RTL-to-preCTS timings and do not establish route parity. '
            'This frozen source predates the subsequently merged terminal legalizer and '
            'preCTS clock/reset optimization protection.</p>'
            '<div class="scroll"><table><thead><tr><th>Design</th><th>Build</th><th>Cold wall (s)</th>'
            '<th>Warm2 wall (s)</th><th>Validation</th></tr></thead><tbody>' + ''.join(rows)
            + '</tbody></table></div><p>' + ' '.join(notes) + '</p>'
            '<p>The CNN baseline contains all 65 expected SRAMs. Its cold profile spends 18.42% '
            'of sampled CPU cycles in expm1, 8.77% in detailed-placement global swap, 6.80% in '
            'RC driver crossing, and 6.74% in OpenMP waiting. These are self sample shares, not '
            'wall-time phase percentages. The OpenTimer change reuses the common wire ramp '
            'inside each crossing sample while preserving arithmetic order and the coincident-pole '
            'fallback. The numerical crossing regression passes exactly. Whole-flow speed and QoR '
            'equivalence require a closed candidate comparison; pending candidates are omitted.</p>')


def handoff_snapshot():
    path = ROOT / 'tmp/remote_sync_20261022/liteeth_handoff/comparison.json'
    if not path.is_file():
        return ''
    data = json.loads(path.read_text())
    rows = []
    for arm, row in data['rows'].items():
        score = row['score']
        rows.append('<tr>' + cell(arm)
                    + cell(number(row['cold_wall_s'], 2))
                    + cell(number(row['flow']['macro_source_seconds'], 2))
                    + cell(number(row['flow']['final_cells_seconds'], 2))
                    + cell(number(score['setup_wns_ps']) + ' / ' + number(score['setup_tns_ps']))
                    + cell(number(score['hold_wns_ps']) + ' / ' + number(score['hold_tns_ps']))
                    + cell(number(score['hpwl_um'], 1))
                    + cell(number(row['handoff_bytes'] / 1024, 1)) + '</tr>')
    if not rows:
        return ''
    notes = []
    for arm, result in data['comparisons'].items():
        notes.append(escape(arm) + ': cold wall change '
                     + number(-100 * result['wall_reduction'], 2) + '%, CPU change '
                     + number(-100 * result['CPU_reduction'], 2) + '%, instruction change '
                     + number(-100 * result['instruction_reduction'], 2) + '%. Final DEF '
                     + ('matches' if result['same_final_DEF'] else 'differs')
                     + '; macro poses ' + ('match' if result['same_macros'] else 'differ')
                     + '; IOs ' + ('match' if result['same_IOs'] else 'differ') + '.')
    return ('<h2>LiteEth preliminary macro-to-final handoff experiment</h2>'
            '<p>All arms use one frozen validated build and the same RTL, constraints, density, halo and '
            'eight placement threads. FP plan is disabled; incremental synthesis and resynthesis remain '
            'enabled. Baseline retains macro cell detail and weight inheritance. Nodetail skips macro '
            'cell detail and exports only fixed/locked objects, IOs and frame geometry. Lean additionally '
            'starts final with synthesis weights and gates its own timing refresh on density. Final '
            'legalization and detailed placement remain enabled. Macro cell legalization is retained '
            'for the existing resynthesis handoff, which still re-runs STA after macro placement. '
            'This preliminary experiment predates the merged terminal legalizer and clock/reset protection and does not implement '
            'the requested macro-only legalization with reuse of pre-snap GP timing for resynthesis.</p>'
            '<div class="scroll"><table><thead><tr><th>Arm</th><th>Cold wall (s)</th>'
            '<th>Macro (s)</th><th>Final cells (s)</th><th>Independent setup WNS / TNS (ps)</th>'
            '<th>Independent hold WNS / TNS (ps)</th><th>HPWL (µm)</th><th>Handoff (KiB)</th>'
            '</tr></thead><tbody>' + ''.join(rows) + '</tbody></table></div><p>'
            + ' '.join(notes) + '</p><p>Stage timers describe the completed macro/final pair; cold wall '
            'also includes earlier resynthesis restarts and subsequent timing refinement and repair. '
            'Timing scores use fresh independent OpenROAD placement-RC analysis with clock, PG and '
            'coverage checks. They are not routed timing or a frequency qualification. Cold plus two '
            'warm runs verify stable DEF output. Perf measurements share the host with other jobs. '
            '<a href="../tmp/remote_sync_20261022/liteeth_handoff/comparison.json">Comparison evidence</a>.</p>')


def macro_only_snapshot(repaired=False):
    case = 'liteeth_macro_only_repaired' if repaired else 'liteeth_macro_only'
    base = ROOT / 'tmp/remote_sync_20261022' / case
    path = base / 'comparison.json'
    if not path.is_file():
        return ''
    data = json.loads(path.read_text())
    if set(data['rows']) != {'baseline', 'macro_only'}:
        return ''
    frozen = json.loads((ROOT / 'tmp/remote_sync_20261022/build_macro_only/manifest.json').read_text())
    versions = frozen['repositories']
    provenance = ('Frozen LiveHD ' + versions['livehd']['commit'][:9] + ', DREAMPlace '
                  + versions['DREAMPlace']['commit'][:9] + ' and OpenTimer '
                  + versions['OpenTimer']['commit'][:9] + '. '
                  '<a href="../tmp/remote_sync_20261022/build_macro_only/manifest.json">Build provenance</a>. ')
    rows = []
    for arm, row in data['rows'].items():
        boundary = json.loads((base / arm / 'stage_boundary.json').read_text())
        score = row['score']
        trace = base / arm / 'w/synth/place/legalize_timing.jsonl'
        placement_timing = [json.loads(line) for line in trace.read_text().splitlines() if line][-1]
        rows.append('<tr>' + cell(arm) + cell(number(row['cold_wall_s'], 2))
                    + cell(number(row['flow']['macro_source_seconds'], 2))
                    + cell(number(boundary['macro_cell_legalization_s'], 3))
                    + cell(number(boundary['macro_cell_detail_s'], 3))
                    + cell(str(boundary['macro_timing_checkpoints']))
                    + cell(str(boundary['first_final_timing_iteration']))
                    + cell(number(row['flow']['final_cells_seconds'], 2))
                    + cell(number(placement_timing['wns']) + ' / ' + number(placement_timing['tns']),
                           'Native reset-filtered ' + escape(placement_timing['stage']))
                    + cell(number(score['setup_wns_ps']) + ' / ' + number(score['setup_tns_ps']))
                    + cell(number(score['hold_wns_ps']) + ' / ' + number(score['hold_tns_ps']))
                    + cell(number(score['hpwl_um'], 1)) + '</tr>')
    delta = data['comparisons']['macro_only']
    title = 'Macro-only handoff with final timing repair' if repaired else 'Corrected macro-only legalization and GP timing handoff'
    flow = ('The matched pair explicitly sets <code>synth.place.legalize.terminal=false</code>, '
            'enabling the legacy final legalization, refinement, resize and repair flow. '
            if repaired else 'The matched pair retains the merged default final terminal legalizer, '
            'whose CLI branch skips subsequent timing refinement, resize and repair. '
            'Both terminal-flow rows have large setup violations; a separate pair explicitly '
            'enables final timing recovery for comparison. ')
    return ('<h2>' + title + '</h2><p>' + provenance + flow
            + 'FP is disabled, macro/final stages and incremental synthesis are enabled, and IO '
            'resynthesis remains enabled. The macro-only arm keeps macro GP timing checkpoints, '
            'measures the selected GP iterate before snapping for resynthesis, skips cell '
            'legalization/detail and post-legalization macro STA, and passes only fixed macro/IO '
            'geometry to final. Final starts from synthesis weights and computes its own timing '
            'after density settles. Hard-macro halo/track checks and final cell legality remain enforced.</p>'
            '<p>All these arms retain the merged preCTS clock/reset-cone protection, which prevents '
            'sizing and buffering those cones even when the later repair passes are enabled. '
            'Placer timing automatically cuts recognized reset nets before propagation, independent '
            'of SDC exceptions, and preserves physical RC loads and clock launch/capture timing. '
            'The native column shows this reset-filtered view at the last legalization/detail checkpoint; '
            'it is not an independent routed score. The separate raw OpenROAD score follows the original '
            'SDC, includes reset paths, and has reset as its worst path. It does not measure the '
            'placer objective and must not be used to infer data-path frequency. '
            'Earlier frozen-source results used a different control-network optimization policy. '
            'Routed-frequency qualification requires a matched timing policy and completed routing.</p>'
            '<div class="scroll"><table><thead><tr><th>Arm</th><th>Cold wall (s)</th>'
            '<th>Macro total (s)</th><th>Macro cell legalization (s)</th><th>Macro cell detail (s)</th>'
            '<th>Macro pre-snap STA calls</th><th>First final STA iteration</th><th>Final cells (s)</th>'
            '<th>Placement checkpoint WNS / TNS (ps; reset excluded)</th><th>Raw OpenROAD setup WNS / TNS (ps; original SDC)</th>'
            '<th>Raw OpenROAD hold WNS / TNS (ps)</th><th>HPWL (µm)</th></tr></thead><tbody>'
            + ''.join(rows) + '</tbody></table></div><p>Cold wall change '
            + number(-100 * delta['wall_reduction'], 2) + '%, CPU change '
            + number(-100 * delta['CPU_reduction'], 2) + '%, instruction change '
            + number(-100 * delta['instruction_reduction'], 2)
            + '%. Each arm completes cold plus two warm runs with matching within-arm DEF hashes. '
            'The baseline also runs resynthesis STA after macro cell legalization; that call is outside its GP timing trace. '
            'Timing scores are fresh independent OpenROAD preCTS placement-RC measurements with '
            'clock, PG and coverage checks; they do not establish routing or supported frequency. '
            'Stage timers cover the completed macro/final pair, while cold wall includes resynthesis '
            'restarts and all other invocation work. Cross-arm macro poses differ after resynthesis; '
            'each final placement preserves its own macro/IO frame. Perf measurements share the host. '
            '<a href="../tmp/remote_sync_20261022/' + case + '/comparison.json">Comparison evidence</a>.</p>')


def main():
    lite = read('liteeth/final_hold25_status.json')
    assert lite and lite['status'] == 'pass'
    rows = []
    for arm in lite['rows']:
        metrics = arm['metrics']
        label = 'LiveHD' if arm['branch'] == 'native' else 'OpenROAD'
        rows.append('<tr>' + cell('LiteEth') + cell(label)
                    + cell('<span class="good">Final extracted</span>', 'All nets routed; identity, clocks and PG verified')
                    + cell(str(arm['drc']))
                    + cell(number(metrics['setup_wns_ps']), 'TNS ' + number(metrics['setup_tns_ps']) + ' ps')
                    + cell(number(metrics['hold_wns_ps']), 'TNS ' + number(metrics['hold_tns_ps']) + ' ps')
                    + cell('Pending', 'Per-clock frequency sweep not yet certified')
                    + cell(number(arm['signal_wirelength_um'], 0))
                    + cell(number(arm['final_route_wall_seconds'], 1), 'Final-route job; peak ' + number(arm['final_route_tree_peak_gib'], 2) + ' GiB')
                    + '</tr>')

    cnn_counts = {}
    cnn_final_note = ('Extraction wrote the OpenROAD SPEF, but the subsequent IR-analysis step exceeded its '
                      '55 GiB memory cap before final reports and the PG census were exported. Its stopped '
                      'attempt is retained; final extracted timing, PG validation and frequency qualification remain pending.')
    for arm, label in [('native_clockclean', 'LiveHD'), ('or_cells_clockclean', 'OpenROAD')]:
        relative = f'cnn/{arm}/drc_completed_iterations.json'
        history = read(relative)
        assert history and history['checks']
        last = history['checks'][-1]
        cnn_counts[label] = last['violations']
        meta = read(f'cnn/{arm}/profiles/drt/metadata.json')
        routed = bool(meta and meta.get('status') == 'complete' and meta.get('exitcode') == 0 and last['violations'] == 0)
        metrics = read(f'cnn/{arm}/profiles/drt/metrics.json') if routed else None
        final = read(f'cnn/{arm}/profiles/final/metadata.json')
        final_stopped = bool(final and final.get('status') == 'incomplete_guard_stop')
        detail = (f'Completed check {last["iteration"]}; extraction complete; final reports blocked by IR resource stop'
                  if final_stopped else f'Completed check {last["iteration"]}; final timing qualification pending')
        runtime_note = ('DRT only; stopped final attempt also cost ' + number(final['wall_seconds'], 1) + ' s'
                        if final_stopped else 'DRT stage only; not whole-flow time')
        runtime = number(meta['wall_seconds'], 1) if routed else 'In progress'
        timing = None
        stage_evidence = relative
        stage = '<span class="good">DRT complete</span>' if routed else '<span class="bad">DRT unresolved</span>'
        if routed and arm == 'or_cells_clockclean':
            recovery = 'cnn/extracted_timing/recovery_postverify_v2'
            review = read(recovery + '_parent_review.json')
            if review and review.get('status') == 'approved_actual_SPEF_metrics_with_precision_bounded_census':
                read(recovery + '/manifest.json')
                proof = read(recovery + '/analysis_result.json')
                assert SOURCES[recovery + '/manifest.json'] == review['manifest_sha256']
                assert SOURCES[recovery + '/analysis_result.json'] == review['analysis_sha256']
                assert proof['original_inputs_unchanged'] and proof['original_failed_metadata_unchanged']
                assert proof['PG_census']['unconnected_pg_iterms'] == 0
                assert proof['PG_census']['wrong_net_or_sigtype_pg_iterms'] == 0
                assert proof['geometry']['original']['raw_sha256'] == proof['geometry']['reloaded']['raw_sha256']
                for kind in ('setup', 'hold'):
                    census = proof[kind]
                    assert census['intervals_compatible'] and census['WNS_inside_endpoint_minimum_interval']
                    assert census['globally_unique_endpoint_pins'] and not census['path_cap_reached']
                timing = proof['metrics']
                assert timing['timing_model'] == 'actual_extracted_spef'
                assert all(isfinite(timing[kind + suffix]) for kind in ('setup', 'hold')
                           for suffix in ('_wns_ps', '_tns_ps'))
                stage = '<span class="good">Extracted STA verified</span>'
                stage_evidence = recovery + '_parent_review.json'
                detail = 'Read-only SPEF/PG/geometry checks; endpoint sums consistent within float precision'
                cnn_final_note = ('Separate read-only recovery verified actual-SPEF setup/hold timing, '
                                  'all 14,918,070 PG associations and byte-identical final DEF geometry. '
                                  'Endpoint totals are consistent with source-derived float precision; the hold '
                                  'printed-path sum does not uniquely reconstruct the tool aggregate. The failed '
                                  'census verification and resource-stopped IR-analysis attempts are retained. '
                                  'IR/continuity signoff and frequency '
                                  'qualification remain pending. '
                                  + link(recovery + '_parent_review.json', 'Reviewed recovery evidence'))
        if not routed:
            progress = read(f'cnn/{arm}/drt_observed_progress_snapshot.json')
            if progress:
                runtime = '≥ ' + number(progress['stage_wall_seconds_elapsed_lower_bound'], 1)
                runtime_note = ('In progress; observed ' + escape(progress['observed_at_utc'])
                                + '; cumulative tool CPU ' + number(progress['tool_cpu_seconds_cumulative'], 1) + ' s')
        timing_cells = cell('Pending') * 2
        if timing:
            timing_cells = ''.join(cell(number(timing[kind + '_wns_ps']),
                                        'TNS ' + number(timing[kind + '_tns_ps']) + ' ps')
                                   for kind in ('setup', 'hold'))
        rows.append('<tr>' + cell('CNN') + cell(label)
                    + cell(stage, link(stage_evidence, detail))
                    + cell(f'{last["violations"]:,}', 'Completed full check; excludes in-pass estimates')
                    + timing_cells + cell('Pending')
                    + cell(number(metrics.get('detailedroute__route__wirelength') if metrics else None, 0))
                    + cell(runtime, runtime_note)
                    + '</tr>')

    ey_history = read('eyeriss_correct_rc_matched/direct_allowcong_guides/drt_progress.json')
    closed = [item for item in ey_history['iterations'] if item.get('status') == 'iteration_complete']
    assert closed
    ey_last = closed[-1]
    ey_closed_wall = 0
    for item in closed:
        hours, minutes, seconds = map(int, item['elapsed_time'].split(':'))
        ey_closed_wall += hours * 3600 + minutes * 60 + seconds
    ey_runtime = '≥ ' + number(ey_closed_wall, 1)
    ey_runtime_note = 'Sum of closed DRT passes only; startup and active pass excluded'
    ey_progress = read('eyeriss_correct_rc_matched/direct_allowcong_guides/drt_observed_progress_snapshot.json')
    if ey_progress and ey_progress['stage_wall_seconds'] >= ey_closed_wall:
        assert isfinite(ey_progress['stage_wall_seconds']) and ey_progress['stage_wall_seconds'] > 0
        ey_runtime = '≥ ' + number(ey_progress['stage_wall_seconds'], 1)
        ey_runtime_note = ('In progress; observed ' + escape(ey_progress['observed_utc'])
                           + '; cumulative tool CPU ' + number(ey_progress['cumulative_tool_CPU_seconds'], 1) + ' s')
    rows.append('<tr>' + cell('Eyeriss') + cell('LiveHD')
                + cell('<span class="bad">DRT unresolved</span>', link('eyeriss_correct_rc_matched/direct_allowcong_guides/drt_progress.json', f'Completed check {ey_last["iteration"]}'))
                + cell(f'{ey_last["violations"]:,}', 'Completed full check; final extraction pending')
                + cell('Pending') * 4
                + cell(ey_runtime, ey_runtime_note) + '</tr>')
    ey_boundary = read('eyeriss_or_cells_listfix_retry/replacement_boundary_clock_tag_audit.json')
    ey_summary_path = 'eyeriss_or_cells_listfix_retry/strict_placement_summary.json'
    ey_summary = read(ey_summary_path) or {}
    ey_stage = ey_summary.get('latest_stage', 'PreCTS placement saved')
    ey_placement = ey_summary.get('placement_result', {})
    ey_cts = read('eyeriss_or_cells_listfix_retry/cts_summary.json')
    ey_grt = read('eyeriss_or_cells_listfix_retry/grt_summary.json')
    ey_or_wall = ey_placement.get('wall_seconds')
    ey_or_wall_note = 'Replacement placement job only; CTS/routing cost excluded'
    ey_or_boundary_note = 'Boundary audit ' + escape(ey_boundary.get('status', 'pending') if ey_boundary else 'pending')
    if ey_cts and ey_grt:
        assert all(obj['result']['status'] == 'complete' and obj['result']['exit_code'] == 0
                   for obj in (ey_cts, ey_grt))
        assert ey_grt['total_overflow'] == 0 and ey_grt['PG_census']['unconnected_pg_iterms'] == 0
        ey_or_wall += ey_cts['result']['wall_seconds'] + ey_grt['result']['wall_seconds']
        ey_or_wall_note = 'Closed placement + CTS + GRT jobs; active DRT and research overhead excluded'
        ey_or_boundary_note += '; GRT zero overflow, antenna 0; final extraction pending'
    rows.append('<tr>' + cell('Eyeriss') + cell('OpenROAD')
                + cell(link(ey_summary_path, ey_stage), ey_or_boundary_note)
                + cell('Pending') * 5
                + cell(number(ey_or_wall, 1), ey_or_wall_note) + '</tr>')

    density_rows = []
    relative = 'cnn/matched_prects_density.json'
    cnn_density = read(relative)
    for key, label in [('native_common_seed', 'LiveHD'), ('strict_OR_replacement', 'OpenROAD')]:
        obj = cnn_density['rows'][key]['grids']['512']
        density_rows.append('<tr>' + cell('CNN') + cell(label) + cell('Matched preCTS placement')
                            + cell(f'{obj["hot_bins_above80"]:,}')
                            + cell(number(100 * obj['cell_area_fraction_in_above80_bins']) + '%')
                            + cell(link(relative, 'Exact same CORE-cell census')) + '</tr>')
    for arm, label in [('native_clockclean', 'LiveHD'), ('or_cells_clockclean', 'OpenROAD')]:
        relative = f'cnn/{arm}/drc_spatial_context.json'
        obj = read(relative)
        density_rows.append('<tr>' + cell('CNN') + cell(label) + cell('PostGRT / DRT input')
                            + cell(f'{obj["valid_hot_bins_above80"]:,}')
                            + cell(number(100 * obj['cell_area_in_valid_density_above80_fraction']) + '%')
                            + cell(link(relative, 'Density evidence')) + '</tr>')
    relative = 'eyeriss_or_cells_listfix_retry/pre_cts_density_comparison.json'
    ey_density = read(relative)
    for key, label in [('native_seed', 'LiveHD'), ('OpenROAD_replacement', 'OpenROAD')]:
        obj = ey_density[key]
        density_rows.append('<tr>' + cell('Eyeriss') + cell(label) + cell('Matched preCTS placement')
                            + cell(f'{obj["above80_bins"]:,}')
                            + cell(number(100 * obj['CORE_area_in_above80_fraction']) + '%')
                            + cell(link(relative, 'Density evidence')) + '</tr>')
    relative = 'eyeriss_or_cells_listfix_retry/post_grt_density_comparison.json'
    ey_postgrt = read(relative)
    assert ey_postgrt['status'] == 'PASS' and ey_postgrt['exact_macro_master_status_xy_orientation_equal']
    assert ey_postgrt['exact_macro_free_bin_area_array_equal']
    for key, label in [('native', 'LiveHD'), ('OpenROAD', 'OpenROAD')]:
        obj = ey_postgrt[key]
        density_rows.append('<tr>' + cell('Eyeriss') + cell(label) + cell('PostGRT / DRT input')
                            + cell(f'{obj["bins_above80"]:,}')
                            + cell(number(100 * obj['CORE_area_in_above80_fraction']) + '%')
                            + cell(link(relative, f'{obj["CORE_cells"]:,} CORE cells; arm-specific CTS changes')) + '</tr>')

    ey_markers = read('eyeriss_correct_rc_matched/direct_allowcong_guides/completed_iter10_summary.json')
    assert ey_markers['iteration10_markers'] == 10356 and ey_markers['same_DRT_input_geometry']
    marker_note = ('At Eyeriss completed check 10, ' + number(100 * ey_markers['iteration10_fraction_in_above80_density'])
                   + '% of 10,356 DRC markers lie in bins above 80% occupancy; none are within 1 µm of a macro perimeter. '
                   + link('eyeriss_correct_rc_matched/direct_allowcong_guides/completed_iter10_summary.json', 'Closed spatial comparison')
                   + '. This is spatial association on a fixed DRT input, not proof of causation.')

    cold = read_sync('liteeth_reuse_v2/run/cold/native.run.json')
    warm = read_sync('liteeth_reuse_v2/run/warm1/native.run.json')
    assert cold['status'] == 'pass' and warm['exit_code'] != 0
    rejected = read_sync('cnn_density/control/attempt001/rejected_attempt_summary.json')
    assert rejected['status'] == 'rejected_fixed_netlist_mutation_stopped'
    reuse = read_sync('liteeth_reuse_v3/run/final_summary.json')
    reuse_proof = read_sync('liteeth_reuse_v3/run/proof.json')
    reuse_review = read_sync('liteeth_reuse_v3/parent_review.json')
    assert reuse_review['status'] == 'approved_preCTS_incremental_reuse_only'
    assert SYNC_SOURCES['liteeth_reuse_v3/run/final_summary.json'] == reuse_review['summary_sha256']
    assert SYNC_SOURCES['liteeth_reuse_v3/run/proof.json'] == reuse_review['proof_sha256']
    assert reuse['status'] == 'pass_corrected_export_cold_and_two_warm'
    assert reuse_proof['status'] == 'pass_current_source_cold_and_two_warm'
    for label in ('warm1', 'warm2'):
        run = reuse_proof['runs'][label]
        assert run['reused_final_state']
        for key in ('def_sha256', 'mapped_netlist', 'compiled_graphs', 'exported_lefs'):
            assert run[key] == reuse_proof['runs']['cold'][key]
    reuse_times = ' / '.join(number(reuse['rows'][label]['CLI_wall_seconds_GNU_time'], 2)
                              for label in ('cold', 'warm1', 'warm2'))
    rc_perf = read_sync('rc_perf/owner_perf_summary.json')
    rc_owner = read_sync('rc-owner-opt.run.json')
    integration = read_sync('corrected-integration-opt-retry.run.json')
    debug = read_sync('corrected-debug-all.run.json')
    assert integration['status'] == debug['status'] == 'pass'
    assert rc_owner['status'] == 'pass'
    rc_exact = read_sync('rc_perf/owner_boundary_result.json')
    assert rc_exact['status'] == 'pass'
    validation = ('<tr>' + cell('LiteEth incremental') + cell('<span class="good">Cold + both warms PASS</span>')
                  + cell(reuse_times, 'Cold / warm1 / authoritative warm2 GNU CLI seconds; excludes perf-wrapper overhead')
                  + cell('Both warms: ABC 17/17 hits and STA 1 hit, zero misses; restore-only placement; exact DEF/netlist/graph/LEF identity. PreCTS reuse only. Earlier v2 export failure retained.')
                  + cell(sync_link('liteeth_reuse_v3/run/final_summary.json', 'Reviewed reuse summary') + '; '
                         + sync_link('liteeth_reuse_v3/parent_review.json', 'Parent verification') + '; '
                         + sync_link('liteeth_reuse_v2/run/warm1/native.run.json', 'Preserved v2 failure')) + '</tr>'
                  + '<tr>' + cell('CNN density control') + cell('Rejected; stopped at iteration 450')
                  + cell(number(rejected['measurement']['wall_s'], 1), 'Partial invalid attempt seconds')
                  + cell('Imported shared tie expansion added 1,306 cells; violates the fixed-netlist comparison. Opt-in preservation fix and its regressions now pass. Both fresh seed-import preflights pass; the replacement GP study is active below.')
                  + cell(sync_link('cnn_density/control/attempt001/rejected_attempt_summary.json', 'Rejection and perf evidence')) + '</tr>'
                  + '<tr>' + cell('OpenTimer RC kernel') + cell('Published; owner RC tests passed')
                  + cell(number(100 * rc_perf['task_clock_ms']['reduction'], 1) + '% less CPU', 'Median of three isolated paired runs')
                  + cell(number(100 * rc_perf['instructions']['reduction'], 1) + '% fewer instructions. Prepared constants reused across thresholds; 220,378 output comparisons checked, including 16 paired NaNs. No full-flow gain claim.')
                  + cell(sync_link('rc_perf/owner_perf_summary.json', 'Paired counters') + '; ' + sync_link('rc_perf/owner_boundary_result.json', 'Numerical boundary check')) + '</tr>')
    stage_opt_path = 'macro-stage-isolation-opt.run.json'
    stage_opt = read_sync(stage_opt_path)
    assert stage_opt['status'] == 'pass'
    stage_debug_path = 'macro-stage-isolation-debug-all.run.json'
    stage_debug = (read_sync(stage_debug_path)
                   if (ROOT / 'tmp/remote_sync_20261021' / stage_debug_path).is_file() else None)
    stage_status = ('Optimized checks PASS; full debug build pending' if stage_debug is None
                    else 'Optimized checks PASS; full debug build ' + stage_debug['status'].upper())
    stage_evidence = sync_link(stage_opt_path, 'Optimized build and tests')
    if stage_debug is not None:
        stage_evidence += '; ' + sync_link(stage_debug_path, 'Full debug build')
    validation += ('<tr>' + cell('Macro-plan-only pressure') + cell(stage_status)
                   + cell(number(stage_opt['wall_s'], 2), 'Optimized guarded job seconds')
                   + cell('Three selected targets pass, including the placement suite and CLI smoke checks; both production tools build. Regressions cover zero FP force/time, active macro pressure, unchanged preview IO poses, and zero fixed-final field/grid time.')
                   + cell(stage_evidence) + '</tr>')

    seed_check = read_sync('cnn_or_seed_repulsion/preflight_summary.json')
    seed_review = read_sync('cnn_or_seed_repulsion/parent_preflight_review_and_GP_release.json')
    assert seed_check['status'] == 'both_retained_seed_preflights_pass_no_GP'
    assert seed_review['status'] == 'approved_six_GP_diagnostics_after_both_retained_preflights'
    assert seed_review['preflight_summary_sha256'] == SYNC_SOURCES['cnn_or_seed_repulsion/preflight_summary.json']
    macro_plan = read_sync('liteeth_macro_pressure_v2/manifest.json')
    macro_release = read_sync('liteeth_macro_pressure_v2/parent_control_release.json')
    assert macro_release['manifest_sha256'] == SYNC_SOURCES['liteeth_macro_pressure_v2/manifest.json']
    assert macro_release['released_arm'] == 'control_p0_o50'
    assert [(arm['pressure'], arm['soft_overlap']) for arm in macro_plan['arms']] == [(0, 50), (1, 50), (1, 5), (4, 5)]
    macro_native = read_sync('liteeth_macro_pressure_v2/outputs/control_p0_o50/native_profile/native.run.json')
    macro_status = read_sync('liteeth_macro_pressure_v2/outputs/control_p0_o50/status.json')
    assert macro_native['status'] == 'pass' and macro_status['status'] == 'failed_preserved_no_retry'
    native_async = read_sync('liteeth_macro_pressure_v2/outputs/control_p0_o50/native_clock_diagnostic_snapshot.json')
    assert len(native_async['rows']['closed_v3']['unique_normalized_tests']) == 14
    macro_recovered = read_sync('liteeth_macro_pressure_score_recovery_v1/run/accepted_control.json')
    macro_recovery_review = read_sync('liteeth_macro_pressure_score_recovery_v1/run/parent_review.json')
    assert macro_recovery_review['status'] == 'approved_preCTS_macro_control_with_partial_latch_coverage'
    assert macro_recovery_review['accepted_control_sha256'] == SYNC_SOURCES['liteeth_macro_pressure_score_recovery_v1/run/accepted_control.json']
    assert macro_recovered['status'] == 'pass'
    macro_positive = read_sync('liteeth_macro_pressure_v3/manifest.json')
    macro_positive_release = read_sync('liteeth_macro_pressure_v3/parent_positive_release.json')
    assert macro_positive_release['manifest_sha256'] == SYNC_SOURCES['liteeth_macro_pressure_v3/manifest.json']
    assert macro_positive_release['status'] == 'approved_three_positive_macro_diagnostics'
    assert macro_positive_release['allowed_arms'] == macro_positive['allowed_positive_arms']
    macro_trial_path = 'liteeth_macro_pressure_v3/outputs/pressure_p1_o50/closed_diagnostic_summary.json'
    macro_trial = read_sync(macro_trial_path)
    macro_correction = read_sync('liteeth_macro_pressure_v3/outputs/pressure_p1_o50/closed_diagnostic_interpretation_correction.json')
    assert macro_trial['status'] == 'closed_preCTS_diagnostic_pass'
    assert macro_trial['manifest_sha256'] == SYNC_SOURCES['liteeth_macro_pressure_v3/manifest.json']
    assert macro_correction['original_summary_sha256'] == SYNC_SOURCES[macro_trial_path]
    assert macro_trial['initial_mapping_bytes_match']
    assert macro_trial['remaining_arms']['status'] == 'held_no_launch'
    macro_phases = read_sync('liteeth_macro_pressure_phase_audit/observed_history_census.json')
    for arm in macro_phases['arms'].values():
        final_phases = [value for name, value in arm['phases'].items()
                        if name == 'w/synth/place/gp_history.json' or '/timing_place/' in name]
        assert len(final_phases) == 4
        assert all(phase[key] == 0 for phase in final_phases
                   for key in ('macro_cell_density_weight', 'macro_cell_density_force',
                               'macro_cell_density_seconds', 'local_density_grid_seconds'))
    macro_trial_rows = []
    for arm, label, runtime in (('control', 'Pressure 0 / overlap 50', macro_trial['control_native_runtime']),
                                ('pressure', 'Pressure 1 / overlap 50', macro_trial['native_runtime'])):
        final = macro_trial['stages']['final']
        assert final['coverage']['reported_coverage_names_and_masters_match']
        score = final[arm + '_score']
        density = final[arm + '_density512']
        frame = score['die_um']
        macro_trial_rows.append('<tr>' + cell(label) + cell(f'{score["instances"]:,}')
                                + cell(number(frame[2] - frame[0], 3) + ' × ' + number(frame[3] - frame[1], 3))
                                + cell(number(100 * density['CORE_area_fraction_in_hot_bins'], 2) + '%',
                                       f'{density["hot_bins_above80"]:,} hot bins; excess ' + number(density['excess_above80_um2'], 2) + ' µm²')
                                + cell(number(score['hpwl_um'], 1))
                                + cell(number(score['setup_wns_ps']) + ' / ' + number(score['setup_tns_ps']))
                                + cell(number(score['hold_wns_ps']) + ' / ' + number(score['hold_tns_ps']))
                                + cell(number(runtime['CLI_wall_seconds'], 2),
                                       'CPU ' + number(runtime['CLI_process_tree_CPU_seconds'], 2) + ' s') + '</tr>')
    control_rows = []
    for stage, label in (('first_macro', 'First macro frame'), ('accepted_macro', 'Accepted macro frame'), ('final', 'Final cells')):
        row = macro_recovered['independent'][stage]
        score = row['score']
        assert row['metadata']['status'] == 'pass' and row['PG_certificate']['missing'] == row['PG_certificate']['wrong'] == 0
        frame = score['die_um']
        control_rows.append('<tr>' + cell(label) + cell(f'{score["instances"]:,}')
                            + cell(number(frame[2] - frame[0], 3) + ' × ' + number(frame[3] - frame[1], 3))
                            + cell(number(score['setup_wns_ps']) + ' / ' + number(score['setup_tns_ps']))
                            + cell(number(score['hold_wns_ps']) + ' / ' + number(score['hold_tns_ps']))
                            + cell(number(score['hpwl_um'], 1))
                            + cell(str(macro_recovery_review['coverage'][stage]['count']))
                            + cell(number(row['metadata']['wall_s'], 2)) + '</tr>')
    gap = read_sync('liteeth_v3_macro_gap_readonly/summary.json')
    assert gap['status'] == 'closed_v3_macro_stage_readonly_diagnostic'
    gap_plot = 'liteeth_v3_macro_gap_readonly/macro_stage_density.svg'
    SYNC_SOURCES[gap_plot] = sha256((ROOT / 'tmp/remote_sync_20261021' / gap_plot).read_bytes()).hexdigest()
    gap_note = ('The saved LiteEth reuse-v3 macro-stage frame has only '
                + number(100 * gap['halo_excluded']['CORE_occupancy'], 2)
                + '% CORE occupancy in usable inter-macro corridors after excluding halos, while '
                + number(100 * gap['grids']['512']['CORE_area_fraction_in_hot_bins'], 2)
                + '% of CORE area is in bins above 80% occupancy. This points to the attraction/spreading balance '
                'as a key issue alongside macro movement. This is a closed macro-stage diagnostic before final-cell '
                'feedback, not a new candidate or routed result. ' + sync_link(gap_plot, 'Density plot') + '; '
                + sync_link('liteeth_v3_macro_gap_readonly/summary.json', 'Gap-area measurements') + '.')
    seed_rows = []
    for seed, label in (('native', 'LiveHD cells'), ('or', 'OpenROAD cells')):
        result = seed_check['seeds'][seed]
        assert result['all_structural_checks_pass'] and result['all_common_parity_checks_pass']
        score = result['source_score']
        seed_rows.append('<tr>' + cell(label) + cell(number(score['hpwl_um'], 1))
                         + cell(number(score['setup_wns_ps']) + ' / ' + number(score['setup_tns_ps']))
                         + cell(number(score['hold_wns_ps']) + ' / ' + number(score['hold_tns_ps']))
                         + cell('Exact import and pose parity; legal; timing reproduced') + '</tr>')
    placement_rows = []
    seed_plan = read_sync('cnn_or_seed_repulsion/manifest.json')
    for policy in seed_plan['priority_order']:
        relative = f'cnn_or_seed_repulsion/{policy}/attempt001/closed_certificate.json'
        if not (ROOT / 'tmp/remote_sync_20261021' / relative).is_file():
            continue
        result = read_sync(relative)
        assert result['status'] == 'closed_placement_diagnostic_pass' and all(result['checks'].values())
        options = seed_plan['policies']['primary'][policy]
        score = result['independent_preCTS_placement_timing']
        final_density = result['density_phase_trajectory'][-1]
        if 'convergence_caveat' in result:
            convergence_note = result['convergence_caveat']
        else:
            convergence = result['convergence']
            convergence_note = (f'{convergence["iterations"]} iterations; '
                                + ('converged' if convergence['converged'] else 'not converged')
                                + '; overflow target ' + str(convergence['stop_overflow']))
        placement_rows.append('<tr>' + cell(sync_link(relative, policy))
                              + cell(str(options['local_density_weight']) + ' / ' + number(options['halo_um'], 2))
                              + cell(number(100 * final_density['CORE_area_fraction_hot80'], 2) + '%',
                                     f'{final_density["over80_bins"]:,} bins above 80%')
                              + cell(number(score['hpwl_um'], 1))
                              + cell(number(score['setup_wns_ps']) + ' / ' + number(score['setup_tns_ps']))
                              + cell(number(score['hold_wns_ps']) + ' / ' + number(score['hold_tns_ps']))
                              + cell(number(result['native_measurement']['wall_s'], 1) + ' / '
                                     + number(result['score_measurement']['wall_s'], 1), 'Native / independent score jobs')
                              + cell(escape(convergence_note)) + '</tr>')

    stamp = datetime.now(timezone.utc).strftime('%Y-%m-%d %H:%M UTC')
    sources = ''.join(f'<li>{link(path, path)} <code>{digest[:12]}</code></li>' for path, digest in SOURCES.items())
    sources += ''.join(f'<li>{sync_link(path, path)} <code>{digest[:12]}</code></li>' for path, digest in SYNC_SOURCES.items())
    page = '''<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>LiveHD / OpenROAD — partial routing results</title>
<style>
:root{color-scheme:light dark;font:16px/1.5 system-ui,sans-serif}body{max-width:1500px;margin:2rem auto;padding:0 1.2rem}h1{line-height:1.15}h2{margin-top:2rem}p{max-width:1050px}a{color:#2474c7}.stamp,small{color:#677381}small{display:block;font-size:.78rem;max-width:260px;margin-top:.25rem}.scroll{overflow-x:auto;border:1px solid #8893a044;border-radius:8px}table{border-collapse:collapse;width:100%;font-variant-numeric:tabular-nums}caption{text-align:left;padding:1rem;font-weight:600}th,td{text-align:left;vertical-align:top;padding:.75rem;border-bottom:1px solid #8893a033}thead{background:#8893a018}th{white-space:nowrap}tbody tr:nth-child(even){background:#8893a009}.good{color:#218342;font-weight:600}.bad{color:#b4502a;font-weight:600}.note{padding:1rem 1.2rem;border-left:4px solid #d1993c;background:#d1993c10}code{font-size:.8rem}li{overflow-wrap:anywhere}@media(prefers-color-scheme:dark){.stamp,small{color:#a6b0bc}a{color:#78b7ff}.good{color:#7dcf93}.bad{color:#ffb285}}
</style></head><body>
<h1>LiveHD / OpenROAD: partial routing results</h1>
<p class="stamp">Snapshot: @STAMP@. Refresh with <code>python3 scripts/update_partial_route.py</code>.</p>
<p>LiveHD and OpenROAD name the placement arms; both use OpenROAD for CTS through detailed routing and extraction. These are saved experiments using frozen tools and inputs. The newly synchronized sources have not yet produced a new routed result.</p>
<p>Evaluations were stopped at the user's request on 2026-10-22 for code review and publication. Unfinished CNN/Eyeriss route numbers below are retained checkpoint evidence, not completed route results.</p>
<p class="note"><strong>Goal:</strong> if OpenROAD routes and LiveHD cannot, fix route parity first. Once both route cleanly, improve extracted timing and supported frequency against a strong reference. Missing an unattainable requested period alone does not reject a design. Keep hold checks separate: lowering frequency does not generally repair same-edge hold violations.</p>
<div class="scroll"><table><caption>Main results — timing in ps, wirelength in µm, wall time in seconds</caption>
<thead><tr><th>Design</th><th>Placement arm</th><th>Latest verified stage</th><th>Routing DRCs</th><th>Extracted setup WNS / TNS</th><th>Extracted hold WNS / TNS</th><th>Supported frequency</th><th>Signal wirelength</th><th>Measured wall time</th></tr></thead><tbody>@ROWS@</tbody></table></div>
<p><strong>CNN:</strong> the OpenROAD reference has completed zero-DRC detailed routing, while the LiveHD arm still has @CNN@ violations at its latest completed check. This is a route-parity problem. @CNN_FINAL@</p>
<p><strong>Eyeriss:</strong> LiveHD has @EY@ violations at its latest completed check. The strict OpenROAD placement is saved and audited; its downstream result is pending. Neither arm has a final extracted comparison.</p>
<p><strong>LiteEth:</strong> both arms have zero DRCs, all nets routed and independently reproduced extracted setup/hold timing. LiveHD improves setup slack by 2.712 ps and hold slack by 3.741 ps, but has 38.2% more signal wire and a 43.9% longer final-route job. This is a timing improvement with routing-cost tradeoffs; maximum-frequency optimization remains unmeasured.</p>
<h2>Density evidence</h2>
<p>All rows below use a 512 × 512 diagnostic grid and CORE-cell area divided by macro-free bin area. “Cell area in hot bins” is the fraction of total CORE area located in bins above 80% occupancy. This diagnostic grid is separate from the coarse routing-estimator grid. Comparisons within each design use identical fixed macro geometry; the measurement boundaries are explicit.</p>
<div class="scroll"><table><thead><tr><th>Design</th><th>Arm</th><th>Boundary</th><th>Bins above 80%</th><th>Cell area in hot bins</th><th>Evidence</th></tr></thead><tbody>@DENSITY@</tbody></table></div>
<p>The matched preCTS CNN pair contains exactly the same 333,888 CORE cells and 43,982.961 µm² of cell area. At 512², LiveHD has 42.911% of its cell area in hot bins, versus none for OpenROAD. At 64², that LiveHD fraction appears to be only 3.828%, showing how the coarse density view hides local crowding. The routing-estimator grid remains unchanged; this motivates finer placement-density feedback.</p>
<p>The strong density association motivates spreading experiments. It does not by itself prove marker causation: the placement policies differ, and CTS and repair add different cells and clock trees to the two arms. The separate postGRT rows show that later measurement boundary explicitly.</p>
<p>@MARKER_NOTE@</p>
<h2>New-source validation in progress</h2>
<p>These checks use the newly synchronized source snapshot and remain separate from the routed comparison above. Failed and rejected attempts are retained; they do not establish routing or reuse success.</p>
<div class="scroll"><table><thead><tr><th>Check</th><th>Status</th><th>Measured cost</th><th>Finding</th><th>Evidence</th></tr></thead><tbody>@VALIDATION@</tbody></table></div>
<p>The synchronized baseline passed all 22 optimized integration tests, built both production executables, and compiled all 3,929 debug targets. Validation of the subsequent macro-plan-only fix is listed separately above. The benchmark defaults remain unchanged. These build checks do not establish routed QoR.</p>
<h2>Cell and macro spreading experiments</h2>
<p>The CNN starting-frame check below uses the same mapped netlist, IO, fixed macro geometry, SDC and placement-RC scorer. All 333,955 components and 45,027 clock endpoints survive import exactly. Each seed's four timing values reproduce exactly after import. The OpenROAD seed has lower HPWL and density but worse setup estimates in this scope; these values are not extracted timing or a routed comparison.</p>
<div class="scroll"><table><thead><tr><th>CNN starting cells</th><th>HPWL (µm)</th><th>Estimated setup WNS / TNS (ps)</th><th>Estimated hold WNS / TNS (ps)</th><th>Retained import</th></tr></thead><tbody>@SEED_ROWS@</tbody></table></div>
<p>Six placement diagnostics are released: OpenROAD seeds at local pressure 0, 1 and 4, LiveHD seeds at 0 and 4, and a separate OpenROAD pressure-0 halo sensitivity. The primary arms use the same nominal 2 µm halo; native row/site snapping enlarges that clearance to 2.052 × 2.160 µm. Movement of the 712 OpenROAD-seed cells affected by that adaptation is tracked separately. The 4.59 µm sensitivity is not pooled with primary results. Raw PG associations remain identically deferred in this import check and must be connected before physical acceptance.</p>
<div class="scroll"><table><caption>Closed CNN placement diagnostics — estimated timing only</caption><thead><tr><th>Policy</th><th>Local pressure / halo (µm)</th><th>Final CORE area in hot bins</th><th>HPWL (µm)</th><th>Estimated setup WNS / TNS (ps)</th><th>Estimated hold WNS / TNS (ps)</th><th>Measured wall time (s)</th><th>Convergence</th></tr></thead><tbody>@PLACEMENT_ROWS@</tbody></table></div>
<p>The first OpenROAD-seeded pressure-0 run creates crowding during GP: 0 hot bins at input, 1,087 before legalization, 796 after legalization and 717 after detailed placement. Final hot-bin CORE area is 21.16%; the halo-adaptation cohort contributes no final area to those hot bins. Estimated worst setup improves by 239.94 ps, while setup TNS worsens by 9.681 million ps and HPWL increases 22.07%. The density, HPWL and TNS regressions coexist with better worst slack; no routing or frequency improvement is established.</p>
<p>The OpenROAD-seeded CNN pressure-1 diagnostic reduces final excess occupancy from 918.10 to 0.47 µm² and hot-bin count from 717 to 1, but worsens estimated setup WNS by 3,018.58 ps and increases HPWL by 30.65%. This strong density response has a substantial timing and wirelength cost; it is not a routed improvement.</p>
<p>The pressure-4 CNN diagnostic finishes with zero hot bins and excess occupancy, improves estimated setup WNS by 1,288.55 ps and TNS by 5.049 million ps over pressure 0, and reduces HPWL by 2.97%. Its endogenous post-density budget stops GP at 200 iterations, versus 750 for pressure 0 and 1; this is not an equal-iteration dose comparison. Hold remains positive. This candidate still needs the common physical handoff and downstream routing before it can establish route parity.</p>
<p>A separate LiteEth flow experiment has closed its control and pressure-1 / overlap-50 placement diagnostic. The remaining pressure-1 / overlap-5 and pressure-4 / overlap-5 arms are held while enforcing the user's macro-plan-only force requirement. FP plan is disabled, macro and final placement are enabled, incremental synthesis is enabled, and the 5 µm configured halo is preserved. Macros receive an area-integrated field emitted only by standard cells, so they can move toward quieter space. Macro-to-macro overlap uses a separate soft penalty, which is zero for disjoint padded footprints; reducing it may have no effect there. Global electrostatic density also separates macros and is unchanged in this comparison. The frozen tools used for these closed trials also carry macro-cell pressure into the automatic-IO preview, so IO changes are measured outcomes and effects cannot be attributed solely to macro GP. A source fix now clears both macro-pressure terms in FP and the IO preview; final placement already disables both. New trials will use a separately frozen build after validation. Corridor denominators separately exclude physical macros and halos. Feedback-derived topology and die dimensions are recorded as outcomes; both arms start from identical mapped inputs. The observer first exposes raw GP coordinates, so initial pre-GP displacement and density are unavailable.</p>
<div class="scroll"><table><caption>LiteEth closed pressure trial — final placement-RC estimates, partial timing coverage</caption><thead><tr><th>Policy</th><th>Instances</th><th>Die size (µm)</th><th>Final CORE area in hot bins</th><th>All non-PG HPWL (µm)</th><th>Estimated setup WNS / TNS (ps)</th><th>Estimated hold WNS / TNS (ps)</th><th>Native CLI wall (s)</th></tr></thead><tbody>@MACRO_TRIAL_ROWS@</tbody></table></div>
<p>Final density is mixed: hot-bin CORE-area fraction improves by 5.26 percentage points, while hot-bin count and excess area increase. Estimated setup, HPWL, cell count, frame size and runtime regress. All three frames retain the same 46 unclocked latch pins and 46 unconstrained endpoints. The two macro solves spend 42.86 and 44.15 seconds computing pressure, measured as elapsed time. Final and its timing-placement rounds record zero macro-field force, field time and shared-grid time. Whole-CLI samples also show substantial OpenMP waiting and RC waveform work; those shares do not attribute CPU time to the macro field. No routing or frequency improvement is established.</p>
<p>@MACRO_GAP_NOTE@</p>
<div class="scroll"><table><caption>LiteEth macro control — independent placement-RC estimates, partial timing coverage</caption><thead><tr><th>Frame</th><th>Instances</th><th>Die size (µm)</th><th>Estimated setup WNS / TNS (ps)</th><th>Estimated hold WNS / TNS (ps)</th><th>All non-PG HPWL (µm)</th><th>Unclocked latch pins</th><th>Score job wall (s)</th></tr></thead><tbody>@CONTROL_ROWS@</tbody></table></div>
<p>The LiteEth control's native command completed in 404.71 s (guarded job). Its original independent scorer failed on an unsupported OpenSTA waveform property; that attempt remains unchanged. A separate corrected checker passed all three saved frames without rerunning placement, including the four defined clock roots, periods, waveforms, leaf memberships and full PG association checks. The final DEF matches reuse-v3 byte for byte. All three frames also report 46 unclocked DHL transparent latch pins and 46 unconstrained endpoints, so four defined clocks do not establish complete timing coverage. Track their census and any changes across policies; frequency qualification remains pending. Native OpenTimer separately skips nonsequential asynchronous set/reset checks on 14 normalized cell-pin pairs, an existing modeling limitation reproduced in reuse-v3.</p>
<h2>Comparison and performance scope</h2>
<p>The placement comparisons share their design's mapped starting netlist, macro/IO frame and authoritative constraints. CTS and repair may make arm-specific changes, which remain audited. CNN uses 65 SRAMs, Eyeriss 1,040 and LiteEth 5. The LiteEth reference uses routability-only placement after a preserved timing-driven rebuffering crash; this qualification prevents a whole-flow dominance claim. Both LiteEth arms configured a 1% repair-buffer option, but that option did not act as an observed cumulative hard bound.</p>
<p>Timing values in the main table are final extracted results only. Pending entries are not zero and placement/GRT estimates are not substituted. A supported-frequency claim needs fresh extracted per-clock STA, consistent generated-clock relationships, IO budgets and timing exceptions, and explicit setup/hold reporting. CNN uses the shared ASAP7 RVT FF 0.77 V / 0 °C standard-cell corner with normalized SRAM models and fixed 10 ps IO budgets; this is not an all-corner frequency claim.</p>
<p><code>perf record</code> and <code>perf stat</code> accompany the physical evaluations. Closed-stage wall times are shown with their scope; an in-progress job has only a timestamped observed lower bound. These jobs share a 32-CPU host with other evaluations/builds, so their wall times do not represent exclusive hardware. The LiteEth final-route numbers exclude upstream placement and research lineage: the separately retained selected-job ledgers total 5,818.296 s for LiveHD and 4,601.480 s for OpenROAD, excluding their shared 335.583 s cold input. They are not minimal production-flow runtimes. Performance improvements will be reported with any timing, geometry or routing changes.</p>
<p>After the initial three routed evaluations, two additional HighTide designs are authorized. Existing FP-plan-disabled, macro/final-enabled and incremental-synthesis requirements continue to apply. <a href="../todo/livehd/3r-route.html">Detailed implementation and experiment history</a>.</p>
@PERFORMANCE@
@HANDOFF@
@MACRO_ONLY@
@MACRO_ONLY_REPAIRED@
<h2>Snapshot evidence</h2><p>Links refer to local evaluation artifacts; their SHA-256 prefixes identify the exact bytes used to render this snapshot.</p><ul>@SOURCES@</ul>
</body></html>
'''
    for key, value in {'STAMP': stamp, 'ROWS': ''.join(rows), 'DENSITY': ''.join(density_rows),
                       'CNN': f'{cnn_counts["LiveHD"]:,}', 'CNN_FINAL': cnn_final_note,
                       'EY': f'{ey_last["violations"]:,}', 'VALIDATION': validation, 'SEED_ROWS': ''.join(seed_rows),
                       'PLACEMENT_ROWS': ''.join(placement_rows), 'CONTROL_ROWS': ''.join(control_rows),
                       'MACRO_TRIAL_ROWS': ''.join(macro_trial_rows),
                       'MACRO_GAP_NOTE': gap_note, 'MARKER_NOTE': marker_note, 'SOURCES': sources,
                       'PERFORMANCE': performance_snapshot(), 'HANDOFF': handoff_snapshot(),
                       'MACRO_ONLY': macro_only_snapshot(),
                       'MACRO_ONLY_REPAIRED': macro_only_snapshot(True)}.items():
        page = page.replace('@' + key + '@', value)
    output = ROOT / 'docs/partial_route.html'
    output.write_text(page)
    print(f'Updated {output} from {len(SOURCES) + len(SYNC_SOURCES)} evidence files at {stamp}')


if __name__ == '__main__':
    main()
