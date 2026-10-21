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
    rows.append('<tr>' + cell('Eyeriss') + cell('LiveHD')
                + cell('<span class="bad">DRT unresolved</span>', link('eyeriss_correct_rc_matched/direct_allowcong_guides/drt_progress.json', f'Completed check {ey_last["iteration"]}'))
                + cell(f'{ey_last["violations"]:,}', 'Completed full check; final extraction pending')
                + cell('Pending') * 4
                + cell('≥ ' + number(ey_closed_wall, 1), 'Sum of closed DRT passes only; startup and active pass excluded') + '</tr>')
    ey_boundary = read('eyeriss_or_cells_listfix_retry/replacement_boundary_clock_tag_audit.json')
    ey_summary_path = 'eyeriss_or_cells_listfix_retry/strict_placement_summary.json'
    ey_summary = read(ey_summary_path) or {}
    ey_stage = ey_summary.get('latest_stage', 'PreCTS placement saved')
    ey_placement = ey_summary.get('placement_result', {})
    rows.append('<tr>' + cell('Eyeriss') + cell('OpenROAD')
                + cell(link(ey_summary_path, ey_stage), 'Boundary audit ' + escape(ey_boundary.get('status', 'pending') if ey_boundary else 'pending') + '; final extraction pending')
                + cell('Pending') * 5
                + cell(number(ey_placement.get('wall_seconds'), 1), 'Replacement placement job only; CTS/routing cost excluded') + '</tr>')

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

    cold = read_sync('liteeth_reuse_v2/run/cold/native.run.json')
    warm = read_sync('liteeth_reuse_v2/run/warm1/native.run.json')
    assert cold['status'] == 'pass' and warm['exit_code'] != 0
    rejected = read_sync('cnn_density/control/attempt001/rejected_attempt_summary.json')
    assert rejected['status'] == 'rejected_fixed_netlist_mutation_stopped'
    rc_perf = read_sync('rc_perf/paired_summary.json')
    rc_exact = read_sync('rc_perf/owner_boundary_result.json')
    assert rc_exact['status'] == 'pass'
    validation = ('<tr>' + cell('LiteEth incremental') + cell('Cold complete; warm1 failed')
                  + cell(number(cold['wall_s'], 1) + ' / ' + number(warm['wall_s'], 1), 'Cold / failed warm1 seconds')
                  + cell('Cached placement restored, then companion LEF export failed on a read-only destination; fix under validation. Warm2 not run.')
                  + cell(sync_link('liteeth_reuse_v2/run/warm1/native.run.json', 'Preserved failure')) + '</tr>'
                  + '<tr>' + cell('CNN density control') + cell('Rejected; stopped at iteration 450')
                  + cell(number(rejected['measurement']['wall_s'], 1), 'Partial invalid attempt seconds')
                  + cell('Imported shared tie expansion added 1,306 cells; violates the fixed-netlist comparison. Opt-in preservation fix under validation; other policies not launched.')
                  + cell(sync_link('cnn_density/control/attempt001/rejected_attempt_summary.json', 'Rejection and perf evidence')) + '</tr>'
                  + '<tr>' + cell('OpenTimer RC kernel') + cell('Prototype; owner tests pending')
                  + cell(number(100 * rc_perf['task_clock_ms']['reduction'], 1) + '% less CPU', 'Median of three isolated paired runs')
                  + cell(number(100 * rc_perf['instructions']['reduction'], 1) + '% fewer instructions. Prepared constants reused across thresholds; 220,378 output comparisons checked, including 16 paired NaNs. No full-flow gain claim.')
                  + cell(sync_link('rc_perf/paired_summary.json', 'Paired counters') + '; ' + sync_link('rc_perf/owner_boundary_result.json', 'Numerical boundary check')) + '</tr>')

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
<h2>New-source validation in progress</h2>
<p>These checks use the newly synchronized source snapshot and remain separate from the routed comparison above. Failed and rejected attempts are retained; they do not establish routing or reuse success.</p>
<div class="scroll"><table><thead><tr><th>Check</th><th>Status</th><th>Measured cost</th><th>Finding</th><th>Evidence</th></tr></thead><tbody>@VALIDATION@</tbody></table></div>
<h2>Comparison and performance scope</h2>
<p>The placement comparisons share their design's mapped starting netlist, macro/IO frame and authoritative constraints. CTS and repair may make arm-specific changes, which remain audited. CNN uses 65 SRAMs, Eyeriss 1,040 and LiteEth 5. The LiteEth reference uses routability-only placement after a preserved timing-driven rebuffering crash; this qualification prevents a whole-flow dominance claim. Both LiteEth arms configured a 1% repair-buffer option, but that option did not act as an observed cumulative hard bound.</p>
<p>Timing values in the main table are final extracted results only. Pending entries are not zero and placement/GRT estimates are not substituted. A supported-frequency claim needs fresh extracted per-clock STA, consistent generated-clock relationships, IO budgets and timing exceptions, and explicit setup/hold reporting. CNN uses the shared ASAP7 RVT FF 0.77 V / 0 °C standard-cell corner with normalized SRAM models and fixed 10 ps IO budgets; this is not an all-corner frequency claim.</p>
<p><code>perf record</code> and <code>perf stat</code> accompany the physical evaluations. Closed-stage wall times are shown with their scope; an in-progress job has only a timestamped observed lower bound. These jobs share a 32-CPU host with other evaluations/builds, so their wall times do not represent exclusive hardware. The LiteEth final-route numbers exclude upstream placement and research lineage: the separately retained selected-job ledgers total 5,818.296 s for LiveHD and 4,601.480 s for OpenROAD, excluding their shared 335.583 s cold input. They are not minimal production-flow runtimes. Performance improvements will be reported with any timing, geometry or routing changes.</p>
<p>After the initial three routed evaluations, two additional HighTide designs are authorized. Existing FP-plan-disabled, macro/final-enabled and incremental-synthesis requirements continue to apply. <a href="../todo/livehd/3r-route.html">Detailed implementation and experiment history</a>.</p>
<h2>Snapshot evidence</h2><p>Links refer to local evaluation artifacts; their SHA-256 prefixes identify the exact bytes used to render this snapshot.</p><ul>@SOURCES@</ul>
</body></html>
'''
    for key, value in {'STAMP': stamp, 'ROWS': ''.join(rows), 'DENSITY': ''.join(density_rows),
                       'CNN': f'{cnn_counts["LiveHD"]:,}', 'CNN_FINAL': cnn_final_note,
                       'EY': f'{ey_last["violations"]:,}', 'VALIDATION': validation, 'SOURCES': sources}.items():
        page = page.replace('@' + key + '@', value)
    output = ROOT / 'docs/partial_route.html'
    output.write_text(page)
    print(f'Updated {output} from {len(SOURCES)} evidence files at {stamp}')


if __name__ == '__main__':
    main()
