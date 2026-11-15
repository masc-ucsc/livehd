#!/usr/bin/env python3
"""Score an exported macro placement using the PlaceReasoner-Beta timing flow."""
import argparse
from collections import defaultdict
import csv
import hashlib
import json
import math
import re
from pathlib import Path
import runpy
import shutil
import subprocess
import sys
import time


def file_hash(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def tcl(value):
    # Literal bytes only. Paths and DEF names may contain Tcl substitutions,
    # braces, escaped slashes, bus brackets, and whitespace.
    return '[encoding convertfrom utf-8 [binary decode hex {' + str(value).encode().hex() + '}]]'


def limit_openroad_threads(limit):
    # OpenROAD's -threads controls startup. The external Tcl stages call
    # set_thread_count again; honor smaller requests while keeping the worker's share.
    return f'set ::livehd_thread_limit {limit}\n' + r"""
rename ::ord::set_thread_count ::ord::livehd_set_thread_count
proc ::ord::set_thread_count {requested} {
    if {$requested eq "max" || ([string is integer -strict $requested] && ($requested <= 0 || $requested > $::livehd_thread_limit))} {
        set requested $::livehd_thread_limit
    }
    ::ord::livehd_set_thread_count $requested
}
"""


# Run against OpenDB both immediately after applying a proposal and after the downstream flow.
# Compare DEF lower-lefts, not orientation-dependent OpenDB origins. Keep mismatches in the TSV.
POSE_AUDIT = r"""
proc livehd_pose_dbu {name axis value dbu} {
    set scaled [expr {$value * $dbu}]
    set nearest [expr {round($scaled)}]
    # Same signed DBU bounds and numerical tolerance as LiveHD saved-state replay.
    if {abs($scaled - $nearest) > 1e-6 || $nearest < -2147483648 || $nearest > 2147483647} {
        error "macro $name: $axis coordinate $value um is outside the representable DEF database grid ($dbu DBU/um)"
    }
    return $nearest
}
proc livehd_check_poses {wanted filename final} {
    set block [ord::get_db_block]
    set count 0
    foreach inst [$block getInsts] {if {[[$inst getMaster] isBlock]} {incr count}}
    if {$count != [dict size $wanted]} {error "macro inventory changed: expected [dict size $wanted], found $count"}
    set poses [open $filename w]
    set mismatch {}
    try {
        puts $poses "name_hex\trequested_x_dbu\trequested_y_dbu\tactual_x_dbu\tactual_y_dbu\torient"
        dict for {name pose} $wanted {
            lassign $pose x y orient
            set inst [$block findInst $name]
            if {$inst eq "NULL" || ![[$inst getMaster] isBlock]} {error "missing macro: $name"}
            set bbox [$inst getBBox]
            set ax [$bbox xMin]; set ay [$bbox yMin]; set ao [$inst getOrient]
            puts $poses [join [list [binary encode hex [encoding convertto utf-8 $name]] $x $y $ax $ay $ao] "\t"]
            if {$mismatch eq "" && (![$inst isPlaced] || $ax != $x || $ay != $y || $ao ne $orient
                || ($final && [$inst getPlacementStatus] ni {FIRM FIXED LOCKED}))} {
                set mismatch "macro pose mismatch: $name expected ($x,$y) $orient, got ($ax,$ay) $ao [$inst getPlacementStatus]"
            }
        }
    } finally {close $poses}
    if {$mismatch ne ""} {error $mismatch}
}
"""


def final_drc(report):
    # Earlier router iterations also log DRC counts. Only the detailed router's
    # final report establishes a score; a present empty report means zero.
    if not report.is_file():
        return None
    content = report.read_text()
    count = len(re.findall(r"^violation type:\s*\S", content, re.M))
    return count if count or not content.strip() else None


def placement_checks(log):
    def check(marker):
        if f"LIVEHD_{marker}_PASSED" in log:
            return "passed"
        return "failed" if f"LIVEHD_{marker}_START" in log else "not_run"

    # The benchmark catches some DPL errors and continues. They are useful diagnostics even if a later
    # stage repairs the placement. The final independent OpenDB check is the acceptance criterion.
    return {"applied_macro_poses": check("APPLIED_POSES"), "routed_macro_poses": check("ROUTED_POSES"),
            "final_legality": check("ROUTED_LEGALITY"),
            "legalization_failure_counts": [int(n) for n in re.findall(r"^\s*Total Placement Failures:\s*(\d+)\s*$", log, re.M)]}


# The benchmark reports timing both before and after routing. OpenSTA even reports
# WNS/TNS of zero for a design with no constrained paths. Read only a fresh,
# path-checked measurement on the final graph, with the benchmark's reporting precision.
FINAL_TIMING = r"""
# Endpoint pins, not k worst path samples. Hex fields preserve arbitrary hierarchy names.
proc livehd_hex {s} { binary encode hex [encoding convertto utf-8 $s] }
proc livehd_pin_kind {p} {
    upvar 1 regs regs blocks blocks
    if {[$p is_top_level_port]} {return io}
    set inst [$p instance]
    set master [get_property $inst ref_name]
    if {[dict exists $blocks $master]} {return macro}
    if {[dict exists $regs [get_full_name $inst]]} {return register}
    return other
}
proc livehd_endpoint_report {filename} {
    set regs [dict create]; set blocks [dict create]
    foreach inst [all_registers -cells] {dict set regs [get_full_name $inst] 1}
    foreach m [[ord::get_db] getLibs] {
        foreach master [$m getMasters] {
            if {[$master isBlock]} {dict set blocks [$master getName] 1}
        }
    }
    set cap [expr {max(1,[sta::endpoint_count])}]
    set file [open "$filename.tmp" w]
    try {
        puts $file "endpoint_hex\tclock_hex\tlaunch_hex\tfamily\tslack_ns"
        set paths [find_timing_paths -path_delay max -group_path_count $cap -endpoint_path_count 1 -unique_paths_to_endpoint]
        foreach pe $paths {
            set start [get_property $pe startpoint]; set end [get_property $pe endpoint]
            set clk [get_property $pe endpoint_clock]; set name {}
            if {$clk ne "" && $clk ne "NULL"} {set name [get_full_name $clk]}
            set a [livehd_pin_kind $start]; set b [livehd_pin_kind $end]
            set family [expr {$a eq "io" || $b eq "io" ? "io" : "${a}_to_${b}"}]
            puts $file [join [list [livehd_hex [get_full_name $end]] [livehd_hex $name] [livehd_hex [get_full_name $start]] $family [expr {1e9*[$pe slack]}]] "\t"]
        }
    } finally {close $file}
    file rename "$filename.tmp" $filename
    foreach c [all_clocks] {puts "LIVEHD_CLOCK [livehd_hex [get_full_name $c]] [get_property $c period]"}
    puts "LIVEHD_ENDPOINT_TOTAL [sta::worst_slack -max] [sta::total_negative_slack -max] $cap"
}

if {[catch {
    if {[llength [find_timing_paths -path_delay max -group_path_count 1]]} {
        set_cmd_units -time ns
        puts LIVEHD_SETUP_BEGIN
        report_wns -max
        report_tns -max
        puts LIVEHD_SETUP_END
        livehd_endpoint_report setup_endpoints.tsv
    } else {puts LIVEHD_SETUP_NO_PATH}
} why]} {puts "LIVEHD_SETUP_ERROR: $why"}
"""


def final_setup_timing(log):
    timing = {"status": "unavailable", "source": "final_downstream_sta", "unit": "ns"}
    if re.search(r"^LIVEHD_SETUP_NO_PATH$", log, re.M):
        return {**timing, "reason": "no constrained setup path"}
    section = re.search(r"^LIVEHD_SETUP_BEGIN\n(.*?)^LIVEHD_SETUP_END$", log, re.M | re.S)
    if section:
        values = {}
        for name in ("wns", "tns"):
            match = re.search(rf"^{name} (?:max )?(\S+)\s*$", section[1], re.M)
            try:
                value = float(match[1]) if match else math.nan
            except ValueError:
                value = math.nan
            if not math.isfinite(value) or value > 0:
                break
            values[name] = value
        if len(values) == 2:
            return {**timing, "status": "measured", **values}
    return {**timing, "reason": "no final setup measurement"}


def routed_endpoints(report, log):
    """Group the worst constrained path per endpoint. A physical pin may have several capture clocks."""
    marker = re.search(r'^LIVEHD_ENDPOINT_TOTAL (\S+) (\S+) (\d+)$', log, re.M)
    if not marker or not report.is_file():
        return {"status": "unavailable", "reason": "no complete routed endpoint census"}
    clocks = {bytes.fromhex(name).decode(): float(period) for name, period in
              re.findall(r'^LIVEHD_CLOCK (\S+) (\S+)$', log, re.M)}
    # Retain the least slack per physical pin and clock. Startpoint breaks exact slack ties
    # deterministically; a changed critical startpoint is evidence, not a new endpoint.
    paths = {}
    with report.open() as stream:
        for r in csv.DictReader(stream, delimiter="\t"):
            end, clock, launch = (bytes.fromhex(r[k]).decode() for k in ("endpoint_hex", "clock_hex", "launch_hex"))
            slack = float(r["slack_ns"])
            if not math.isfinite(slack) or not end:
                return {"status": "unavailable", "reason": "invalid routed endpoint"}
            key = (end, clock)
            val = (slack, launch, r["family"])
            if key not in paths or val < paths[key]:
                paths[key] = val

    def digest(keys):
        return hashlib.sha256(json.dumps(sorted(keys), separators=(",", ":")).encode()).hexdigest()

    def group(rows):
        worst = {}
        for (end, clock), (slack, launch, family) in rows.items():
            path = (slack, launch, family, clock)
            if end not in worst or path < worst[end]:
                worst[end] = path
        values = [v[0] for v in worst.values()]
        signed = min(values)
        return {"endpoints": len(worst), "population": digest(worst), "clock_population": digest(rows),
                "worst_slack_ps": 1000*signed, "wns_ps": 1000*min(0, signed),
                "tns_ps": 1000*math.fsum(min(0, v) for v in values), "failing": sum(v < 0 for v in values)}, worst

    if not paths:
        return {"status": "unavailable", "reason": "no constrained setup endpoints"}
    total, worst = group(paths)
    ws, tns = map(float, marker.group(1, 2))
    if not (math.isclose(total["worst_slack_ps"]/1000, ws, abs_tol=1e-5, rel_tol=2e-5)
            and math.isclose(total["tns_ps"]/1000, tns, abs_tol=1e-5, rel_tol=2e-5)):
        return {"status": "unavailable", "reason": "endpoint census disagrees with final STA totals"}
    by_clock, by_family = defaultdict(dict), defaultdict(dict)
    for key, value in paths.items():
        by_clock[key[1]][key] = value
    for end, (slack, launch, family, clock) in worst.items():
        by_family[family][(end, clock)] = (slack, launch, family)
    return {"status": "measured", "engine": "OpenSTA", "analysis": "final_setup",
            "clock_periods_ns": clocks, "total": total,
            "capture_clocks": {k: group(v)[0] for k,v in by_clock.items()},
            "path_families": {k: group(v)[0] for k,v in by_family.items()}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('design', type=Path)
    parser.add_argument('--aspect', required=True, choices=['1:1', '2:1'])
    parser.add_argument('--flow', required=True, type=Path, help='PlaceReasoner-Beta checkout')
    parser.add_argument('--coords', required=True, type=Path, help='export what=macros format=placereasoner')
    parser.add_argument('--openroad', default='openroad')
    parser.add_argument('--workdir', type=Path, required=True, help='new directory for this execution')
    parser.add_argument('--placement-seconds', type=float, required=True, help='elapsed time spent producing this candidate')
    parser.add_argument('--tokens-k', type=float, help='agent tokens spent on this candidate, in thousands')
    parser.add_argument('--receipt', type=Path, help='write the validation result and evidence hashes for a GUI worker')
    parser.add_argument('--threads', type=int, help='OpenROAD thread share for this worker')
    args = parser.parse_args()
    if not math.isfinite(args.placement_seconds) or args.placement_seconds < 0:
        parser.error("placement seconds must be finite and nonnegative")
    if args.threads is not None and args.threads < 1:
        parser.error("threads must be positive")
    task, work, flow = args.design.resolve(), args.workdir.resolve(), args.flow.resolve()
    templates = flow / 'skills/skills_Nangate45'
    stages = [templates / stage / 'template' / filename for stage, filename in [
        ('0.init', 'floorplan.tcl'), ('2.detail_place', 'detail_place.tcl'),
        ('3.post_detail_placement', 'cts_route.tcl')]]
    recipes = runpy.run_path(str(flow/'macroflow/recipes.py'))
    metrics = runpy.run_path(str(flow/'macroflow/metrics.py'))['parse_metrics']
    source = json.loads(args.coords.read_text())
    coords = source if isinstance(source, list) else source['macros']
    if not isinstance(source, list) and source.get('units') != 'um':
        parser.error('coordinates must be in micrometers')
    if not coords or len(coords) != len({m['name'] for m in coords}):
        parser.error('expected a nonempty list of unique macro placements')
    floorplan = task/'def'/('1_1_u60_floorplan.def' if args.aspect == '1:1' else '2_1_floorplan.def')
    script = [f'set {key} {tcl(value)}' for key, value in {
        'data_dir': task, 'tech_dir': task, 'fp_def': floorplan, 'proj_name': task.name,
        'save_dir': work/'db', 'report_dir': work/'reports'}.items()]
    if args.threads is not None:
        script.append(limit_openroad_threads(args.threads))
    script += [f'source {tcl(stages[0])}', recipes['SAFE_PLACE_PROC'], POSE_AUDIT,
               'set livehd_wanted {}', 'set dbu [[ord::get_db_tech] getDbUnitsPerMicron]',
               'puts LIVEHD_APPLIED_POSES_START']
    for m in coords:
        x, y, orient = m['x'], m['y'], m['orient']
        if not all(math.isfinite(v) for v in (x, y)) or orient not in {'R0','R90','R180','R270','MX','MY','MXR90','MYR90'}:
            parser.error(f'invalid pose for {m["name"]}')
        script += [f'set name {tcl(m["name"])}',
                   'set inst [[ord::get_db_block] findInst $name]',
                   'if {$inst eq "NULL" || ![[$inst getMaster] isBlock]} {error "unknown block macro: $name"}',
                   f'dict set livehd_wanted $name [list [livehd_pose_dbu $name x {x!r} $dbu] [livehd_pose_dbu $name y {y!r} $dbu] {orient}]',
                   f'safe_place $name {x!r} {y!r} {orient}']
    script += [f'livehd_check_poses $livehd_wanted {tcl(work/"macro_poses.tsv")} 0',
               'puts LIVEHD_APPLIED_POSES_PASSED', recipes['LOCK_MACROS_FIRM'], 'write_def [file join $save_dir macros.def]',
               f'source {tcl(stages[1])}', f'source {tcl(stages[2])}',
               'puts LIVEHD_ROUTED_POSES_START',
               f'livehd_check_poses $livehd_wanted {tcl(work/"routed_macro_poses.tsv")} 1',
               'puts LIVEHD_ROUTED_POSES_PASSED', 'puts LIVEHD_ROUTED_LEGALITY_START',
               'check_placement -verbose', 'puts LIVEHD_ROUTED_LEGALITY_PASSED', FINAL_TIMING]
    work.mkdir(parents=True, exist_ok=False)
    (work/'coords.json').write_text(json.dumps(coords, indent=2)+'\n')
    (work/'score.tcl').write_text('\n'.join(script)+'\n')
    executable = shutil.which(args.openroad)
    if executable is None:
        parser.error(f'OpenROAD executable unavailable: {args.openroad}')
    inputs = [task/(task.name+'.v'), task/'constraint.sdc', *sorted((task/'lib').glob('*.lib')), *sorted((task/'lef').glob('*.lef'))]
    flow_inputs = [*stages, flow/'macroflow/recipes.py', flow/'macroflow/metrics.py']
    manifest = {'aspect': args.aspect, 'input': str(floorplan), 'openroad': str(Path(executable).resolve()),
                'openroad_sha256': file_hash(executable), 'threads': args.threads,
                'placement_seconds': args.placement_seconds, 'coords_sha256': file_hash(args.coords),
                'floorplan_sha256': file_hash(floorplan), 'scorer_sha256': file_hash(__file__),
                'technology_constraints_sha256': {str(path.relative_to(task)): file_hash(path) for path in inputs},
                'templates_sha256': {str(path.relative_to(flow)): file_hash(path) for path in flow_inputs}}
    (work/'inputs.json').write_text(json.dumps(manifest, indent=2)+'\n')
    started = time.monotonic()
    with (work/'openroad.log').open('w') as log:
        rc = subprocess.call([str(Path(executable).resolve()), *(['-threads', str(args.threads)] if args.threads else []), '-no_init', '-exit', str(work/'score.tcl')], stdout=log, stderr=subprocess.STDOUT, cwd=work)
    elapsed = time.monotonic()-started
    log = (work/'openroad.log').read_text(errors='replace')
    checks = placement_checks(log)
    observed_threads = [int(n) for n in re.findall(r'\[INFO ORD-0030\] Using (\d+) thread\(s\)', log)]
    (work/'placement_checks.json').write_text(json.dumps(checks, indent=2)+'\n')
    complete = (rc == 0 and 'ROUTE_ALL_DONE' in log and (work/'db/routed.odb').is_file()
                and all(checks[key] == 'passed' for key in ('applied_macro_poses', 'routed_macro_poses', 'final_legality')))
    values = metrics(log) if complete else {}
    timing = final_setup_timing(log) if complete else {"status": "not_run"}
    if timing['status']=='measured':
        timing['endpoints'] = routed_endpoints(work/'setup_endpoints.tsv', log)
    (work/'setup_timing.json').write_text(json.dumps(timing, indent=2)+'\n')
    drc_file = work/'reports'/f'04_{task.name}.drc.rpt'
    drc = final_drc(drc_file) if complete else None
    row = dict(aspect=args.aspect, design=task.name, method='LiveHD', status='routed' if complete and drc is not None and drc<1000 else 'failed',
               wns=timing.get('wns'), tns=timing.get('tns'), drc=drc, wl_um=values.get('wirelength'), power_w=values.get('power'),
               area_um2=values.get('area'), runtime_s=round(args.placement_seconds+elapsed, 3), tokens_k=args.tokens_k)
    with (work/'row.csv').open('w', newline='') as output:
        writer = csv.DictWriter(output, fieldnames=list(row))
        writer.writeheader()
        writer.writerow(row)
    if args.receipt:
        files = [work/'inputs.json', work/'openroad.log', work/'row.csv', work/'macro_poses.tsv', work/'score.tcl',
                 work/'routed_macro_poses.tsv', work/'placement_checks.json', work/'setup_timing.json', work/'setup_endpoints.tsv', drc_file]
        evidence = {str(path.relative_to(work)): file_hash(path) for path in files if path.is_file()}
        args.receipt.write_text(json.dumps({'row': row, 'inputs': manifest, 'sha256': evidence, 'openroad_exit': rc,
                                            'openroad_thread_counts': observed_threads, 'placement_checks': checks, 'setup_timing': timing}))
    print(json.dumps(row), flush=True)
    return 0 if row['status'] == 'routed' else 1


if __name__ == '__main__':
    sys.exit(main())
