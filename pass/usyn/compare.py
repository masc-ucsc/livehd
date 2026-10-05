#!/usr/bin/env python3
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Serial, guarded comparison of native USYN and ABC on one colored snapshot."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import shutil
import subprocess


def digest(path):
    value = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def native_summary(report):
    """Definition-region estimates, kept separate from mapped cell/STA metrics."""
    regions = report["regions"]
    costs = {stage: {metric: sum(r[stage][metric] for r in regions)
                     for metric in ("static_logic", "inverters", "domino", "total")}
             for stage in ("before", "after_pairs", "after_residual", "after")}
    endpoints = [e for r in regions for e in r["endpoints"]]
    searches = [s for r in regions for s in r["search"]]
    # Nested boundary phase work is already included in boundary_work.
    work = {key: sum(s[key] for s in searches) for key in
            ("admission_work", "one_cell_work", "boundary_work", "two_phase_work", "removal_work", "local_work")}
    return {"scope": report["scope"], "totals": report["totals"], "estimated_cost": costs,
            "whole_cone_endpoints": sum(e["whole_cone"] for e in endpoints),
            "exactly_two_cell_endpoints": sum(len(e["cells"]) == 2 for e in endpoints),
            "wider_endpoints": sum(len(e["cells"]) > 2 for e in endpoints),
            "initial_endpoint_work": work,
            "logical_work": {key: sum(r["work"][key] for r in regions) for key in
                             ("admission", "p1", "selection", "pairs", "residual", "feedback", "cleanup", "cmos_cleanup", "total")},
            "native_optimization": report.get("native_optimization", {}),
            "mapping_trials": report.get("mapping_trials", []),
            "pair_work": sum(r["pairs"]["work"] for r in regions),
            "residual": {key: sum(r["residual"][key] for r in regions) for key in
                         ("cost_before", "cost_after", "rewrite_windows", "rewrite_wins",
                          "resub_windows", "resub_wins", "candidates", "depth_rejections",
                          "cost_rejections", "reference_visits", "balance_groups", "balance_wins", "sweep_confirmations", "sweep_wins",
                          "feedback_rounds",
                          "feedback_attempts", "feedback_wins")},
            "search_exhausted_regions": sum(r["search_exhausted"] for r in regions),
            # Endpoints published as their credit-free identity (starved searches).
            "identity_fallback_endpoints": sum(r["identity_fallbacks"] for r in regions)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--source-extra", action="append", default=[], type=Path,
                        help="Additional compilation unit; both mappers use the same compiled snapshot")
    parser.add_argument("--top", required=True)
    parser.add_argument("--liberty", required=True, type=Path)
    parser.add_argument("--sdc", type=Path, help="Identical timing constraints for every mapped result")
    parser.add_argument("--workdir", required=True, type=Path, help="New comparison directory")
    parser.add_argument("--lhd", type=Path, default=Path("bazel-bin/lhd/lhd"))
    parser.add_argument("--monitor", type=Path, default=Path("bazel-bin/pass/usyn/measure_synth"))
    parser.add_argument("--usyn-set", action="append", default=[], metavar="FLAG=VALUE",
                        help="Native pass.usyn setting, e.g. logical_inputs=3 or clock_phases=1")
    parser.add_argument("--compile-set", action="append", default=[], metavar="FLAG=VALUE",
                        help="Shared compile setting, e.g. pass.satopt=true; defaults remain unchanged")
    parser.add_argument("--residual-ablation", action="store_true",
                        help="Compare selection alone, residual optimization, and residual plus feedback")
    parser.add_argument("--delay", type=float, help="Identical mapping budget in ps")
    parser.add_argument("--max-gate", type=int, help="Shared synthesis color size limit")
    parser.add_argument("--seconds", type=int, default=120, help="Wall limit per command")
    parser.add_argument("--memory-mb", type=int, default=4096, help="Process-tree memory limit")
    args = parser.parse_args()
    for path in [args.source, *args.source_extra, args.liberty, args.lhd, args.monitor, *([args.sdc] if args.sdc else [])]:
        if not path.is_file():
            parser.error(f"missing file: {path}")
    if args.seconds <= 0 or args.memory_mb <= 0 or (args.max_gate is not None and args.max_gate <= 0):
        parser.error("time, memory and color-size limits must be positive")
    if args.delay is not None and (not math.isfinite(args.delay) or args.delay < 0):
        parser.error("--delay must be finite and nonnegative")
    settings = {}
    for item in args.usyn_set:
        key, separator, value = item.partition("=")
        if not separator or not key or not value:
            parser.error(f"expected --usyn-set FLAG=VALUE, got {item!r}")
        if args.residual_ablation and key in ("residual", "feedback"):
            parser.error("--residual-ablation controls residual and feedback for all three configurations")
        if (key == "tmap" and value != "abc") or (key == "target" and value != "cmos"):
            parser.error("mapped comparisons require tmap=abc and target=cmos")
        if key == "delay":
            parser.error("use --delay to give both flows the same mapping target")
        settings[key] = value
    root = args.workdir.resolve()
    root.mkdir(parents=True, exist_ok=False)
    lhd, monitor, source, liberty = [str(p.resolve()) for p in
                                   (args.lhd, args.monitor, args.source, args.liberty)]
    extra_sources = [str(p.resolve()) for p in args.source_extra]
    timing_constraints = [str(args.sdc.resolve())] if args.sdc else []
    commands = []

    def run(label, *arguments):
        command = [lhd, *map(str, arguments), "--workdir", str(root / label),
                   "--result-json", str(root / f"{label}.result.json"), "-q"]
        commands.append({"label": label, "argv": command})
        (root / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        try:
            subprocess.run([monitor, "--archive", str(root / f"{label}.measurement"),
                            "--seconds", str(args.seconds), "--memory-mb", str(args.memory_mb),
                            "--", *command], check=True)
        except subprocess.CalledProcessError:
            for path in (root / f"{label}.measurement/command.log", root / f"{label}.result.json"):
                if path.is_file():
                    print(path.read_text())
            raise
        return json.loads((root / f"{label}.result.json").read_text())

    compile_options = [arg for value in args.compile_set for arg in ("--set", value)]
    run("compile", "compile", source, *extra_sources, "--top", args.top, "--emit-dir", f"lg:{root}/colored", *compile_options)
    color_options = ["--set", "pass.color.synth.mapper=usyn"]
    if args.max_gate is not None:
        color_options += ["--set", f"pass.color.synth.max_gate={args.max_gate}"]
    run("color", "pass", "color", "synth", f"lg:{root}/colored", "--top", args.top, *color_options)
    run("models", "pass", "liberty", "gensim", liberty, "--emit-dir", f"lg:{root}/models-lg")
    variants = []
    if args.residual_ablation:
        variants += [("usyn-selection", "usyn", {"residual": "false", "feedback": "false"}),
                     ("usyn-residual", "usyn", {"residual": "true", "feedback": "false"})]
    variants += [("usyn", "usyn", {"residual": "true", "feedback": "true"} if args.residual_ablation else {}),
                 ("abc", "abc", {})]
    results = {}
    for label, mapper, overrides in variants:
        # Private identical inputs and fresh workdirs keep every variant cold.
        shutil.copytree(root / "colored", root / f"{label}-input")
        options = ["--set", f"synth.liberty={liberty}"]
        if args.delay is not None:
            options += ["--set", f"pass.{mapper}.delay={args.delay}"]
        effective = dict(settings, **overrides) if mapper == "usyn" else {}
        if mapper == "usyn":
            for key, value in effective.items():
                options += ["--set", f"pass.usyn.{key}={value}"]
        try:
            run(label, "pass", mapper, f"lg:{root}/{label}-input", "--top", args.top,
                "--emit-dir", f"lg:{root}/{label}-net", "--emit", f"verilog:{root}/{label}.v", *options)
            run(f"{label}-sta", "pass", "opentimer", f"lg:{root}/{label}-net", liberty, *timing_constraints, "--top", args.top)
            # This independent benchmark check is not a synthesis publication gate.
            proof = run(f"{label}-lec", "lec", "--impl", f"lg:{root}/{label}-net",
                        "--ref", f"lg:{root}/colored", "--lib", f"lg:{root}/models-lg", "--top", args.top)
            results[label] = {
                "status": "completed", "mapper": mapper, "usyn_overrides": effective, "lec": proof["lec"],
                "timing": json.loads((root / f"{label}-sta/timing.json").read_text()),
                "mapping_measurement": json.loads((root / f"{label}.measurement/measurement.json").read_text()),
                "netlist": str(root / f"{label}.v"),
            }
        except subprocess.CalledProcessError as error:
            failed = commands[-1]["label"]
            row = {"status": "failed", "mapper": mapper, "usyn_overrides": effective,
                   "failed_stage": failed, "exit_code": error.returncode}
            for key, path in (("mapping_measurement", root / f"{label}.measurement/measurement.json"),
                              ("failure_measurement", root / f"{failed}.measurement/measurement.json"),
                              ("failure_result", root / f"{failed}.result.json"),
                              ("timing", root / f"{label}-sta/timing.json")):
                if path.is_file():
                    row[key] = json.loads(path.read_text())
            results[label] = row
        if (root / f"{label}.v").is_file():
            results[label]["netlist"] = str(root / f"{label}.v")
        native_path = root / label / "qor.json.usyn.json"
        if mapper == "usyn" and native_path.is_file():
            results[label]["usyn_report"] = str(native_path)
            results[label]["native"] = native_summary(json.loads(native_path.read_text()))
    report = {"schema_version": 2, "top": args.top, "source": source, "liberty": liberty,
              "source_sha256": digest(source),
              "liberty_sha256": digest(liberty),
              "colors": "shared snapshot from pass.color synth mapper=usyn", "results": results,
              "residual_ablation": args.residual_ablation, "mapping_delay_ps": args.delay,
              "limits": {"seconds_per_command": args.seconds, "process_tree_memory_mb": args.memory_mb},
              "compile_settings": args.compile_set}
    report["sources"] = [{"path": p, "sha256": digest(p)} for p in [source, *extra_sources]]
    report["sdc"] = {"path": timing_constraints[0], "sha256": digest(timing_constraints[0])} if timing_constraints else None
    (root / "comparison.json").write_text(json.dumps(report, indent=2) + "\n")
    for label, result in results.items():
        if result["status"] != "completed":
            print(f"{label}: FAILED at {result['failed_stage']}; exit={result['exit_code']}")
            continue
        rows = result["timing"]["designs"]
        metrics = [{key: row.get(key) for key in ("module", "area", "cells", "max_delay",
                   "opaque_logic_nodes", "native_state_nodes", "constraints_complete", "timing_cells_complete")}
                   for row in rows]
        print(f"{label}: LEC={result['lec']['verdict']}; "
              f"time_unit={result['timing'].get('time_unit')}; metrics={json.dumps(metrics)}")
        if "native" in result:
            print(f"{label}: native={json.dumps(result['native'])}")
    print(f"Comparison: {root / 'comparison.json'}")
    if any(result["status"] != "completed" or result["lec"]["verdict"] != "proven" for result in results.values()):
        raise SystemExit("Comparison is unproven; inspect retained LEC reports")


if __name__ == "__main__":
    main()
