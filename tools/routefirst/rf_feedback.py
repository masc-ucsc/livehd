# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Release routed neighbors of failed pins and give those pins first access on the next route."""

import argparse
from bisect import bisect_left, bisect_right
import csv
import heapq
import json
import os
import math
import re
import shutil
import subprocess
from pathlib import Path
import time
import sys

# A Bazel runfile can be a symlink into an otherwise sparse source tree.
# Import the coordinator beside the invoked script in the runfiles directory.
sys.path.insert(0, str(Path(__file__).absolute().parent))

from rf_chunks import (
    CONN,
    POSE,
    ROUTE,
    WIRE,
    die_with_parent,
    feedback,
    frame_steps,
    record_lines,
    records,
    route,
    routing_layers,
    section,
    wire_bounds,
    worker_command,
    worker_options,
)


def localized_seed(
    frame, failed, radius, output, all_pins=False, findings=(), pin_neighbors=False, pin_access_layers=()
):
    owners = {row["fail_pin"].rsplit("/", 1)[0] for row in failed if row["fail_pin"]}
    owners.update(row["fail_pin"] for row in failed if row["fail_pin"])
    blocked = owners.copy()
    priority = {row["net"] for row in failed}
    # Older reports have endpoint names only. New frames provide the pin
    # coordinate, which can be far from the origin of a rotated hard macro.
    exact = [
        (int(r["fail_x_dbu"]), int(r["fail_y_dbu"])) for r in failed if r.get("fail_x_dbu") and r.get("fail_y_dbu")
    ]
    if all_pins:
        missing = {row["net"] for row in failed}
        for name, head, _ in records(frame):
            if name.replace(r"\/", "/") in missing:
                for inst, pin in CONN.findall(head.partition(" + ")[0]):
                    owners.add((pin if inst == "PIN" else inst).replace(r"\/", "/"))
    issue_nets = {name for r in findings for name in r["nets"].partition(" (")[0].split() if name != "fixed"}
    # An open net is reported without a place (an all-zero box). Its net is released by name below; the origin is
    # not a release centre.
    centers = exact + [
        ((int(r["xl"]) + int(r["xh"])) // 2, (int(r["yl"]) + int(r["yh"])) // 2)
        for r in findings
        if any(int(r[k]) for k in ("xl", "yl", "xh", "yh"))
    ]

    current = ""
    blocked_cells = set()
    # Copy through the signal-net header verbatim, retaining the last pose,
    # clock tree, special nets and every placement or routing exclusion.
    with frame.open() as src, output.open("w") as dst:
        for line in src:
            dst.write(line)
            current = section(line, current)
            if current in ("COMPONENTS", "PINS") and line.lstrip().startswith("- "):
                name = line.split(None, 2)[1].replace(r"\/", "/")
                if name in owners:
                    if current == "COMPONENTS" and name in blocked:
                        blocked_cells.add(name)
                    lines = record_lines(src, line)
                    dst.writelines(lines[1:])
                    if m := POSE.search(" ".join(part.strip() for part in lines)):
                        centers.append((int(m[1]), int(m[2])))
            if current == "NETS":
                break
        ys = {}
        for x, y in centers:
            ys.setdefault(x, []).append(y)
        xs = sorted(ys)
        for group in ys.values():
            group.sort()

        def touches(path):
            if not xs:
                return False
            if pin_access_layers and path.partition(" (")[0].split()[-1] not in pin_access_layers:
                return False
            xl, yl, xh, yh = wire_bounds(path)
            for i in range(
                bisect_left(xs, xl - radius),
                bisect_right(xs, xh + radius),
            ):
                yy = ys[xs[i]]
                j = bisect_left(yy, yl - radius)
                if j < len(yy) and yy[j] <= yh + radius:
                    return True
            return False

        released = []
        for name, head, paths in records(frame):
            net = name.replace(r"\/", "/")
            neighbor = pin_neighbors and any(
                (pin if inst == "PIN" else inst).replace(r"\/", "/") in blocked
                for inst, pin in CONN.findall(head.partition(" + ")[0])
            )
            if neighbor or net in issue_nets:
                priority.add(net)
            if paths and (net in priority or any(touches(path) for path in paths)):
                dst.write(head.rstrip() + " ;\n")
                released.append(name)
            else:
                dst.write(head)
                if paths:
                    dst.write("      " + "\n      ".join(paths) + " ;\n")
        for line in src:
            if line.split()[:2] == ["END", "NETS"]:
                dst.write(line)
                break
        for line in src:
            dst.write(line)
    return released, priority, blocked_cells


def critical_routes(frame, timing, count):
    slacks, clocks = {}, set()
    with timing.open() as src:
        for line in src:
            if not line.strip() or line.startswith("#"):
                continue
            row = re.fullmatch(r'"(.+)"\s+(\S+)\s+([01])', line.strip())
            if not row or not math.isfinite(value := float(row[2])):
                raise ValueError(f"invalid LiveHD pin timing row in {timing}: {line.strip()}")
            pin = row[1].replace(r"\/", "/")
            slacks[pin] = min(value, slacks.get(pin, math.inf))
            if row[3] == "1":
                clocks.add(pin)

    def ranked():
        for name, head, paths in records(frame):
            if not paths or not paths[0].startswith("+ ROUTED ") or "+ USE CLOCK" in head:
                continue
            pins = [
                (b if a == "PIN" else a + ":" + b).replace(r"\/", "/")
                for a, b in CONN.findall(head.partition(" + ")[0])
            ]
            if len(pins) < 2 or clocks.intersection(pins):
                continue
            slack = min((slacks[p] for p in pins if p in slacks), default=math.inf)
            if not math.isfinite(slack):
                continue
            length = 0
            for path in paths:
                wire = WIRE.fullmatch(path)
                if not wire:
                    break
                if wire[4] and wire[4] != "*":
                    length += abs(int(wire[4]) - int(wire[1]))
                if wire[5] and wire[5] != "*":
                    length += abs(int(wire[5]) - int(wire[2]))
            else:
                yield slack, -length, name.replace(r"\/", "/")

    return heapq.nsmallest(count, ranked())


def timing_eco(args):
    frame = args.previous / "routed.def"
    ranked = critical_routes(frame, args.timing_eco, args.net_count)
    if not ranked:
        raise ValueError("no routed signal net matched the pin timing")
    folder = args.out
    folder.mkdir(parents=True, exist_ok=False)
    names = folder / "nets.txt"
    names.write_text("".join(n + "\n" for _, _, n in ranked))
    (folder / "rank.json").write_text(json.dumps({n: [s, -w] for s, w, n in ranked}, indent=2))
    seed = folder / "seed.def"
    localized_seed(frame, [{"net": n, "fail_pin": ""} for _, _, n in ranked], 0, seed)
    route(
        [
            *args.worker,
            "--only",
            names,
            "--timing",
            args.timing_eco,
            "--crit-frac",
            "1",
            "--order",
            "short",
            "--hweight",
            "1.1",
            "--budget",
            "900000",
            "--pd-alpha",
            str(args.pd_alpha),
            "--ripup",
            "4",
            "--final-passes",
            "3",
            "--time-limit",
            str(args.seconds),
            "--verify-report",
            folder / "findings.tsv",
            "--report",
            folder / "feedback.tsv",
            seed,
            folder / "routed.def",
            *args.lefs,
        ],
        folder,
    )
    result = feedback(folder)
    result.update(
        selected=len(ranked),
        unrouted=result["violations"].get("verify_nets_without_wires", 0),
    )
    (folder / "feedback.json").write_text(json.dumps(result, indent=2))
    print(json.dumps(result))


def main():
    die_with_parent()
    p = argparse.ArgumentParser(description=__doc__)
    worker_options(p)
    p.add_argument("--radius", type=int, help="access window radius in DEF units (default: ten sites of the frame)")
    p.add_argument(
        "--all-pins",
        action="store_true",
        help="release access at every endpoint of the failed nets",
    )
    p.add_argument(
        "--reserve-first",
        action="store_true",
        help="reserve compatible access pads for the blocked cells before routing the priority cohort",
    )
    p.add_argument(
        "--targets",
        type=Path,
        help="repair listed open nets and their released neighbors in this round",
    )
    p.add_argument(
        "--protect-failures",
        action="append",
        type=Path,
        default=[],
        metavar="TSV",
        help="route earlier failed nets from this same placement alongside current failures to break repair cycles",
    )
    p.add_argument(
        "--pin-neighbors",
        action="store_true",
        help="route every net of a blocked cell in the priority pass",
    )
    p.add_argument("--seconds", type=float, default=120, help="CPU budget per routing pass")
    p.add_argument("--out", required=True, type=Path)
    p.add_argument("previous", type=Path, help="directory containing routed.def and feedback.tsv")
    p.add_argument("lefs", nargs="+", type=Path)
    p.add_argument(
        "--timing-eco",
        type=Path,
        metavar="PIN_TIMING",
        help="reroute critical signals of a completed route using LiveHD pin timing",
    )
    p.add_argument("--net-count", type=int, default=12, help="maximum signals in a timing ECO")
    p.add_argument("--pd-alpha", type=float, default=0.6, help="timing ECO tree weight")
    p.add_argument(
        "--pin-access-layers",
        action="store_true",
        help="release tracks near failed pins only on the lowest three routing layers, including vias starting there",
    )
    args = p.parse_args()
    args.worker = worker_command(args)
    if args.timing_eco:
        if (
            args.targets
            or args.protect_failures
            or args.all_pins
            or args.pin_neighbors
            or args.pin_access_layers
            or args.reserve_first
        ):
            p.error("timing ECO and pin-access repair options select different cohorts")
        if not (args.net_count > 0 and args.seconds > 0 and math.isfinite(args.seconds) and 0 <= args.pd_alpha <= 1):
            p.error("expected positive count and CPU budget and a tree weight between 0 and 1")
        # A timing ECO freezes all other signals. Keep open-net repairs on the ordinary feedback path.
        stats = json.loads((args.previous / "stats.json").read_text())
        if "verify_nets_without_wires" not in stats or any(v for k, v in stats.items() if k.startswith("verify_")):
            p.error("timing ECO requires a verified connected input without native geometry findings")
        timing_eco(args)
        return
    args.pin_neighbors = args.pin_neighbors or args.reserve_first
    if args.radius is None:
        args.radius = 10 * frame_steps(args.previous / "routed.def")[0]
    if args.radius < 1 or not args.seconds > 0:
        p.error("radius and CPU budget must be positive")
    folder = args.out
    folder.mkdir(parents=True, exist_ok=False)
    start = time.monotonic()
    with (args.previous / "feedback.tsv").open() as src:
        failed = [r for r in csv.DictReader(src, delimiter="\t") if r["status"] != "routed"]
    report = args.previous / "findings.tsv"
    if not failed:
        # A worker reports only the nets in its cohort. Read back the whole DEF with
        # the supplied LEFs before deciding that this checkpoint needs no work.
        report = folder / "findings.tsv"
        readback = subprocess.check_output(
            [*args.worker, "--verify-report", report, args.previous / "routed.def", "-", *args.lefs], text=True
        )
        counts = {name: int(value) for name, value in (line.split() for line in readback.splitlines())}
    findings = []
    if report.is_file():
        known = {r["net"] for r in failed}
        with report.open() as src:
            for row in csv.DictReader(src, delimiter="\t"):
                if row["kind"] == "no-wires":
                    if row["nets"] not in known:
                        failed.append({"net": row["nets"], "fail_pin": "", "status": "unattempted", "why": "outside-cohort"})
                        known.add(row["nets"])
                elif row["kind"] not in ("cell-off-site", "cell-overlap"):
                    findings.append(row)
    previous = failed
    if args.targets:
        targets = set(args.targets.read_text().splitlines())
        unknown = targets - {r["net"] for r in previous}
        if not targets or unknown:
            p.error(f"expected open net names from the previous report; unknown names: {sorted(unknown)[:5]}")
        failed = [r for r in previous if r["net"] in targets]
        findings = [r for r in findings if targets.intersection(r["nets"].partition(" (")[0].split())]
    if not failed and not findings:
        for name in ("routed.def", "feedback.tsv"):
            try:
                os.link(args.previous / name, folder / name)
            except OSError:
                shutil.copyfile(args.previous / name, folder / name)
        (folder / "stats.json").write_text(json.dumps(dict(counts, route_cpu_s=0)))
        result = dict(feedback(folder), released=0, note="nothing to repair")
        (folder / "feedback.json").write_text(json.dumps(result, indent=2))
        print(json.dumps(result))
        return
    seed = folder / "seed.def"
    prior = {}
    for report in args.protect_failures:
        with report.open() as src:
            for row in csv.DictReader(src, delimiter="\t"):
                if row["status"] != "routed":
                    prior[row["net"], row["fail_pin"]] = row
    released, priority, blocked_cells = localized_seed(
        args.previous / "routed.def",
        [*prior.values(), *failed],
        args.radius,
        seed,
        args.all_pins,
        findings,
        args.pin_neighbors,
        # The pin layer and the two routing layers above it (M1 through M3 in ASAP7).
        tuple(routing_layers(args.lefs)[:3]) if args.pin_access_layers else (),
    )
    (folder / "released.json").write_text(json.dumps(released))
    first = folder / "priority"
    only = folder / "first.txt"
    selected = []
    reservations = []
    if args.reserve_first and blocked_cells:
        access_list = folder / "blocked-cells.txt"
        access_list.write_text("".join(name + "\n" for name in sorted(blocked_cells)))
        reservations = ["--reserve-cells", access_list]
    if args.targets:
        repair_list = folder / "repair.txt"
        cohort = priority.union(name.replace(r"\/", "/") for name in released)
        repair_list.write_text("".join(name + "\n" for name in sorted(cohort)))
        selected = ["--only", repair_list]
    only.write_text("".join(n + "\n" for n in sorted(priority)))
    opts = [
        *args.worker,
        *ROUTE,
        "--time-limit",
        str(args.seconds),
        "--final-passes",
        "3",
    ]
    route(
        [
            *opts,
            *reservations,
            *(["--reserve", "--shift", "3", "--chain", "4", "--flop-reach", "2"] if args.pin_neighbors else []),
            "--only",
            only,
            seed,
            first / "routed.def",
            *args.lefs,
        ],
        first,
    )
    route(
        [
            *opts,
            "--reserve",
            "--shift",
            "3",
            "--flop-reach",
            "2",
            "--chain",
            "4",
            "--tile-move",
            *selected,
            "--report",
            folder / "feedback.tsv",
            "--verify-report",
            folder / "findings.tsv",
            first / "routed.def",
            folder / "routed.def",
            *args.lefs,
        ],
        folder,
    )
    if args.targets:
        # The router reports only its mutable cohort. Unselected opens keep
        # their original failed-pin diagnostics. The full DEF verifier still
        # checks every net, including all frozen conductors and cell poses.
        with (folder / "feedback.tsv").open() as src:
            rows = csv.DictReader(src, delimiter="\t")
            fields = rows.fieldnames
            reported = {row["net"] for row in rows}
        with (folder / "findings.tsv").open() as src:
            bare = {r["nets"] for r in csv.DictReader(src, delimiter="\t") if r["kind"] == "no-wires"}
        old_rows = {r["net"]: r for r in previous}
        with (folder / "feedback.tsv").open("a") as dst:
            writer = csv.DictWriter(
                dst,
                fieldnames=fields,
                delimiter="\t",
                lineterminator="\n",
                extrasaction="ignore",
            )
            for net in sorted(bare - reported):
                writer.writerow(
                    old_rows.get(
                        net,
                        {"net": net, "status": "unattempted", "why": "outside-cohort"},
                    )
                )
    result = dict(
        feedback(folder),
        previous_unrouted=len(previous),
        targeted_unrouted=len(failed),
        prior_failure_nets=len({name for name, _ in prior}),
        previous_geometry_findings=len(findings),
        pin_access_layers=args.pin_access_layers,
        released=len(released),
        radius=args.radius,
        all_pins=args.all_pins,
        priority_nets=len(priority),
        wall_s=time.monotonic() - start,
    )
    (folder / "feedback.json").write_text(json.dumps(result, indent=2))
    print(json.dumps(result))


if __name__ == "__main__":
    main()
