#!/usr/bin/env python3
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Serial, guarded comparison of pass.synth and pass.abc on identical colors."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--top", required=True)
    parser.add_argument("--liberty", required=True, type=Path)
    parser.add_argument("--workdir", required=True, type=Path, help="New comparison directory")
    parser.add_argument("--lhd", type=Path, default=Path("bazel-bin/lhd/lhd"))
    parser.add_argument("--monitor", type=Path, default=Path("bazel-bin/pass/synth/measure_synth"))
    parser.add_argument("--synth-set", action="append", default=[], metavar="FLAG=VALUE",
                        help="pass.synth option for the unate side, e.g. support=2 or max_depth=2 (repeatable)")
    parser.add_argument("--delay", type=float, help="Identical mapping budget in ps")
    parser.add_argument("--max-gate", type=int, help="Synthesis color size limit")
    parser.add_argument("--seconds", type=int, default=120, help="Wall limit per command")
    parser.add_argument("--memory-mb", type=int, default=4096, help="Process-tree memory limit")
    args = parser.parse_args()
    for path in (args.source, args.liberty, args.lhd, args.monitor):
        if not path.is_file():
            parser.error(f"missing file: {path}")
    root = args.workdir.resolve()
    root.mkdir(parents=True, exist_ok=False)
    lhd, monitor, source, liberty = [str(p.resolve()) for p in
                                   (args.lhd, args.monitor, args.source, args.liberty)]
    commands = []

    def run(label, *arguments):
        directory = root / label
        command = [lhd, *map(str, arguments), "--workdir", str(directory),
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

    run("compile", "compile", source, "--top", args.top, "--emit-dir", f"lg:{root}/colored")
    color_options = [] if args.max_gate is None else ["--set", f"pass.color.synth.max_gate={args.max_gate}"]
    run("color", "pass", "color", "synth", f"lg:{root}/colored", "--top", args.top, *color_options)
    run("models", "pass", "liberty", "gensim", liberty, "--emit-dir", f"lg:{root}/models-lg")
    results = {}
    for mapper in ("synth", "abc"):
        # Passes may prepare/mutate their input. Both receive identical snapshots.
        shutil.copytree(root / "colored", root / f"{mapper}-input")
        options = ["--set", f"synth.liberty={liberty}"]
        if args.delay is not None:
            options += ["--set", f"pass.{mapper}.delay={args.delay}"]
        if mapper == "synth":
            for item in args.synth_set:
                options += ["--set", f"pass.synth.{item}"]
        run(mapper, "pass", mapper, f"lg:{root}/{mapper}-input", "--top", args.top,
            "--emit-dir", f"lg:{root}/{mapper}-net", "--emit", f"verilog:{root}/{mapper}.v", *options)
        run(f"{mapper}-sta", "pass", "opentimer", f"lg:{root}/{mapper}-net", liberty,
            "--top", args.top)
        proof = run(f"{mapper}-lec", "lec", "--impl", f"lg:{root}/{mapper}-net",
                    "--ref", f"lg:{root}/colored", "--lib", f"lg:{root}/models-lg", "--top", args.top)
        results[mapper] = {
            "lec": proof["lec"],
            "timing": json.loads((root / f"{mapper}-sta/timing.json").read_text()),
            "mapping_measurement": json.loads((root / f"{mapper}.measurement/measurement.json").read_text()),
            "netlist": str(root / f"{mapper}.v"),
        }
    synth = json.loads((root / "synth/qor.json.synth.json").read_text())
    report = {"schema_version": 1, "top": args.top, "source": source, "liberty": liberty,
              "colors": "shared snapshot from pass.color synth", "results": results,
              "synth_totals": synth["totals"], "synth_report": str(root / "synth/qor.json.synth.json")}
    (root / "comparison.json").write_text(json.dumps(report, indent=2) + "\n")
    for mapper, result in results.items():
        rows = result["timing"]["designs"]
        metrics = [{key: row.get(key) for key in ("module", "area", "cells", "max_delay",
                   "opaque_logic_nodes", "native_state_nodes", "constraints_complete", "timing_cells_complete")}
                   for row in rows]
        print(f"{mapper}: LEC={result['lec']['verdict']}; "
              f"time_unit={result['timing'].get('time_unit')}; metrics={json.dumps(metrics)}")
    print(f"Comparison: {root / 'comparison.json'}")
    if any(result["lec"]["verdict"] != "proven" for result in results.values()):
        raise SystemExit("Comparison is unproven; inspect retained LEC reports")


if __name__ == "__main__":
    main()
