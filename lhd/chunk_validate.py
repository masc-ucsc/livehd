# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Stitch immutable home poses into the exported DEF and independently measure a final placement."""

import json
import math
import os
import re
import shutil
import sqlite3
import subprocess
import tempfile
from contextlib import closing
from pathlib import Path

from chunk_journal import Journal
from chunk_live import LivePlacement, publish_trial, revision


def placement_state(work, trial=None, branch=None):
    if not trial and not branch:
        return work.state()
    if not trial or not branch:
        raise ValueError("choose both a trial and its branch")
    record = Journal(work.root).get(trial)
    row = record["candidates"].get(branch, {})
    if row.get("status") != "done" or not row.get("state_path"):
        raise ValueError("the branch has no completed placement")
    return json.loads(Path(row["state_path"]).read_text())


def stitch(work, state, destination):
    with closing(sqlite3.connect("")) as owners:
        assembly = LivePlacement.assemble(work, state, owners)
        return write_def(work, assembly, destination, owners)


def write_def(work, assembly, destination, owners):
    source = work.root / "seed.def"
    if not source.exists():
        from chunk_flow import current_manifest

        if (
            current_manifest(work.manifest.parent).read_bytes()
            != work.manifest.read_bytes()
        ):
            raise ValueError("this historical plan has no saved DEF")
        source = work.manifest.parent / "chunk-plan.def"
    if not source.is_file():
        raise ValueError(
            "export the project with synth.place.snapshots=true to save chunk-plan.def"
        )
    # Native snapshot DEF writes one component per line. Keep all non-placement clauses and fixed objects verbatim.
    pattern = re.compile(r"(\+ (?:PLACED|FIXED|COVER) )\(\s*-?\d+\s+-?\d+\s*\)\s+(\w+)")
    rows = {}
    inside = False
    count = 0
    dst = Path(destination).resolve()
    if dst == source.resolve():
        raise ValueError("choose a separate output DEF")
    with tempfile.NamedTemporaryFile(mode="w", dir=dst.parent, delete=False) as temp:
        try:
            with source.open() as inp:
                for line in inp:
                    if line.startswith("ROW "):
                        t = line.split()
                        rows.setdefault(int(t[4]), []).append(
                            (int(t[3]), int(t[3]) + int(t[7]) * int(t[11]), t[5])
                        )
                    elif line.startswith("COMPONENTS "):
                        inside = True
                    elif line.startswith("END COMPONENTS"):
                        inside = False
                    elif inside and line.startswith("- "):
                        name = line.split(maxsplit=3)[1]
                        pose = owners.execute(
                            "SELECT x,y,macro,orientation FROM owners WHERE name=?",
                            (name,),
                        ).fetchone()
                        if pose is None and "+ UNPLACED" in line:
                            raise ValueError(f"unowned, unplaced DEF component {name}")
                        if pose:
                            owners.execute("DELETE FROM owners WHERE name=?", (name,))
                            x, y, macro, orientation = pose
                            m = pattern.search(line)
                            if not m:
                                m = re.search(r"\+ UNPLACED", line)
                                if not m:
                                    raise ValueError(
                                        f"unrecognized placement for {name}"
                                    )
                            orient = orientation or (m[2] if m.lastindex else "N")
                            status = m[1] if m.lastindex else "+ PLACED "
                            if not macro:
                                orient = next(
                                    (o for l, r, o in rows.get(y, ()) if l <= x < r),
                                    None,
                                )
                                if not orient:
                                    raise ValueError(
                                        f"{name} has no site row at its legal coordinates"
                                    )
                            line = (
                                line[: m.start()]
                                + f"{status}( {x} {y} ) {orient}"
                                + line[m.end() :]
                            )
                            count += 1
                    temp.write(line)
            if owners.execute("SELECT 1 FROM owners LIMIT 1").fetchone():
                raise ValueError(
                    "worker result contains instances missing from the exported DEF"
                )
        except BaseException:
            os.unlink(temp.name)
            raise
    os.replace(temp.name, dst)
    return {
        "revision": assembly["revision"],
        "components_updated": count,
        "def": str(dst),
    }


def tcl_word(s):
    return (
        '"'
        + str(s)
        .replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("$", "\\$")
        .replace("[", "\\[")
        + '"'
    )


def validate(
    work,
    engine,
    route=None,
    capacity=0.75,
    timing_setup=None,
    segments=False,
    trial=None,
    branch=None,
    *,
    state=None,
):
    from chunk_flow import PROJECT, atomic_json

    if state is None:
        state = placement_state(work, trial, branch)
    ref = work.root / "validation" / revision(state)
    ref.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(dir=ref))
    atomic_json(out / "request.json", dict(trial=trial, branch=branch, route=route))
    result = stitch(work, state, out / "placed.def")
    project = json.loads((work.manifest.parent / PROJECT).read_text())
    args = project["args"]
    settings = dict(
        args[i + 1].split("=", 1) for i, a in enumerate(args[:-1]) if a == "--set"
    )

    def paths(key):
        return [
            tcl_word(Path(project["cwd"]) / x)
            for x in settings.get(key, "").split(",")
            if x
        ]

    if not paths("synth.sdc") or not paths("synth.liberty"):
        raise ValueError("validation requires synthesis SDC and Liberty inputs")
    script = ["set_thread_count 3"]
    script += ["read_lef " + p for p in paths("synth.lef")]
    script += [
        "read_liberty " + p
        for p in paths("synth.liberty") + paths("synth.macro_liberty")
    ]
    script += [
        "read_def " + tcl_word(result["def"]),
        "check_placement",
        "puts CHUNK_LEGAL",
        "set_cmd_units -time ps -capacitance fF -resistance kOhm -power mW -distance um",
    ]
    if timing_setup:
        script += ["source " + tcl_word(Path(timing_setup).resolve())]
    script += ["read_sdc " + p for p in paths("synth.sdc")]
    script += ["source " + p for p in paths("synth.wire_rc")]
    for mode in ("placement", "global_routing") if route else ("placement",):
        if mode == "global_routing":
            if not 0 < capacity <= 1:
                raise ValueError("retained routing capacity must be in (0,1]")
            layers = tcl_word("-".join(route))
            script += [
                "set_routing_layers -signal " + layers + " -clock " + layers,
                f"set_global_routing_layer_adjustment {layers} {1 - capacity}",
                "global_route -verbose -allow_congestion -critical_nets_percentage 0 -congestion_iterations 3",
            ]
            if segments:
                script += [
                    "write_global_route_segments " + tcl_word(out / "segments.txt")
                ]
        script += [
            "estimate_parasitics -" + mode,
            "report_checks -path_delay max -group_path_count 1",
            f'puts "CHUNK_TIMING {mode} [sta::worst_slack -max] [sta::total_negative_slack -max]"',
        ]
    script += ["exit"]
    tcl = out / "validate.tcl"
    tcl.write_text("\n".join(script) + "\n")
    exe = shutil.which(str(engine))
    if not exe:
        raise ValueError(f"cannot locate independent engine {engine}")
    with (out / "openroad.log").open("w") as log:
        proc = subprocess.run(
            [exe, "-exit", "-no_init", str(tcl)],
            stdout=log,
            stderr=subprocess.STDOUT,
            check=False,
        )
    log = out / "openroad.log"
    data = log.read_text()
    if proc.returncode or "CHUNK_LEGAL" not in data:
        raise ValueError(f"OpenROAD validation failed, see {log}")
    result.update(
        legal=True,
        log=str(log),
        timing={
            m: {
                "wns_ps": min(0.0, float(w)) if abs(float(w)) < 1e25 else None,
                "tns_ps": float(t) if math.isfinite(float(t)) else None,
            }
            for m, w, t in re.findall(
                r"^CHUNK_TIMING (\w+) (\S+) (\S+)", data, re.MULTILINE
            )
        },
    )
    if segments and route:
        from chunk_calibrate import compare_segments

        result["segments"] = str(out / "segments.txt")
        maps = work.analysis("routing", {}, state=state)
        if maps["floorplan"]:
            result["spatial_calibration"] = compare_segments(
                out / "segments.txt",
                maps["floorplan"],
                [r["grid"] for r in maps["chunks"].values()],
                boundary=maps["boundaries"],
            )
    if route:

        def last(pattern):
            found = re.findall(pattern, data, re.MULTILINE)
            if not found:
                raise ValueError(f"missing global-route measurement in {log}")
            return found[-1]

        result.update(
            routed_nets=int(last(r"Routed nets: (\d+)")),
            routed_um=float(last(r"Total wirelength: ([\d.]+) um")),
            global_overflow=int(last(r"^Total\s+.*?/\s*\d+\s*/\s*(\d+)\s*$")),
            retained_capacity=capacity,
        )
    atomic_json(out / "result.json", result)
    route_file = ref / "global-routing.json"
    latest = ref / "latest.json"
    if route:
        atomic_json(route_file, result)
    elif latest.is_file() and not route_file.is_file():
        prior = json.loads(latest.read_text())
        if "routed_um" in prior:
            atomic_json(route_file, prior)
    atomic_json(latest, result)
    return result


def verify_wns(work, trial, branch, engine, timing_setup=None, keep=False):
    """Validate the same experiment's checkpoint, control, and edited legal poses."""
    journal = Journal(work.root)
    record = journal.get(trial)
    if branch in (None, "control") or branch not in record["candidates"]:
        raise ValueError("choose a completed edited branch")
    frames = {
        "checkpoint": journal.saved_main(record["checkpoint"]),
        "control": placement_state(work, trial, "control"),
        "candidate": placement_state(work, trial, branch),
    }
    values, logs = {}, {}
    for name, state in frames.items():
        result = validate(work, engine, timing_setup=timing_setup, state=state)
        slack = result["timing"].get("placement", {}).get("wns_ps")
        if slack is None:
            raise ValueError(
                "independent placement timing did not return a constrained worst slack"
            )
        values[name], logs[name] = slack, result["log"]
    improves = values["candidate"] > max(values["checkpoint"], values["control"])
    retained = bool(keep and improves)
    if retained:
        publish_trial(
            work,
            journal,
            trial,
            branch,
            record["checkpoint"],
            record["epoch"],
            record["engine"],
            frames["candidate"],
        )
    message = (
        f"Independent placement-RC WNS {values['candidate']:.3f} ps, "
        f"control {values['control']:.3f} ps, checkpoint {values['checkpoint']:.3f} ps"
    )
    journal.decide(trial, branch, "kept" if retained else "finding", message)
    return {
        "trial": trial,
        "branch": branch,
        "wns_ps": values,
        "delta_control_ps": values["candidate"] - values["control"],
        "improves_both": improves,
        "kept": retained,
        "logs": logs,
    }
