# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Release routed neighbors of failed pins and give those pins first access on the next route."""

import argparse
from bisect import bisect_left, bisect_right
import csv
import json
from pathlib import Path
import time

from rf_chunks import CONN, POSE, ROUTE, WIRE, feedback, records, route, section


def localized_seed(frame, failed, radius, output, all_pins=False, findings=(), pin_neighbors=False):
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
    centers = exact + [((int(r["xl"]) + int(r["xh"])) // 2, (int(r["yl"]) + int(r["yh"])) // 2) for r in findings]

    current = ""
    # Copy through the signal-net header verbatim, retaining the last pose,
    # clock tree, special nets and every placement or routing exclusion.
    with frame.open() as src, output.open("w") as dst:
        for line in src:
            dst.write(line)
            current = section(line, current)
            if current in ("COMPONENTS", "PINS") and line.startswith("- "):
                name = line.split(None, 2)[1].replace(r"\/", "/")
                if name in owners:
                    m = POSE.search(line)
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
            m = WIRE.fullmatch(path)
            if not m:
                raise ValueError(f"unknown rf_route geometry: {path}")
            x, y = int(m[1]), int(m[2])
            x2 = int(m[4]) if m[4] not in (None, "*") else x
            y2 = int(m[5]) if m[5] not in (None, "*") else y
            for i in range(bisect_left(xs, min(x, x2) - radius), bisect_right(xs, max(x, x2) + radius)):
                yy = ys[xs[i]]
                j = bisect_left(yy, min(y, y2) - radius)
                if j < len(yy) and yy[j] <= max(y, y2) + radius:
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
            if line.startswith("END NETS"):
                dst.write(line)
                break
        for line in src:
            dst.write(line)
    return released, priority


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--router", type=Path, default=Path(__file__).with_name("rf_route"))
    p.add_argument("--radius", type=int, default=540, help="access window radius in DEF units")
    p.add_argument("--all-pins", action="store_true", help="release access at every endpoint of the failed nets")
    p.add_argument(
        "--targets", type=Path, help="repair listed open nets and their released neighbors in this round"
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
        "--pin-neighbors", action="store_true", help="route every net of a blocked cell in the priority pass"
    )
    p.add_argument("--seconds", type=float, default=120, help="CPU budget per routing pass")
    p.add_argument("--out", required=True, type=Path)
    p.add_argument("previous", type=Path, help="directory containing routed.def and feedback.tsv")
    p.add_argument("lefs", nargs="+", type=Path)
    args = p.parse_args()
    if args.radius < 1 or not args.seconds > 0:
        p.error("radius and CPU budget must be positive")
    folder = args.out
    folder.mkdir(parents=True, exist_ok=False)
    start = time.monotonic()
    with (args.previous / "feedback.tsv").open() as src:
        failed = [r for r in csv.DictReader(src, delimiter="\t") if r["status"] != "routed"]
    previous = failed
    if args.targets:
        targets = set(args.targets.read_text().splitlines())
        unknown = targets - {r["net"] for r in previous}
        if not targets or unknown:
            p.error(f"expected open net names from the previous report; unknown names: {sorted(unknown)[:5]}")
        failed = [r for r in previous if r["net"] in targets]
    findings = []
    report = args.previous / "findings.tsv"
    if report.is_file():
        with report.open() as src:
            findings = [
                r
                for r in csv.DictReader(src, delimiter="\t")
                if r["kind"] not in ("no-wires", "cell-off-site", "cell-overlap")
                and (not args.targets or targets.intersection(r["nets"].partition(" (")[0].split()))
            ]
    if not failed and not findings:
        p.error("previous report has no signal routing failures or geometry findings")
    seed = folder / "seed.def"
    prior = {}
    for report in args.protect_failures:
        with report.open() as src:
            for row in csv.DictReader(src, delimiter="\t"):
                if row["status"] != "routed":
                    prior[row["net"], row["fail_pin"]] = row
    released, priority = localized_seed(
        args.previous / "routed.def",
        [*prior.values(), *failed],
        args.radius,
        seed,
        args.all_pins,
        findings,
        args.pin_neighbors,
    )
    (folder / "released.json").write_text(json.dumps(released))
    first = folder / "priority"
    only = folder / "first.txt"
    selected = []
    if args.targets:
        repair_list = folder / "repair.txt"
        cohort = priority.union(name.replace(r"\/", "/") for name in released)
        repair_list.write_text("".join(name + "\n" for name in sorted(cohort)))
        selected = ["--only", repair_list]
    only.write_text("".join(n + "\n" for n in sorted(priority)))
    opts = [args.router, *ROUTE, "--time-limit", str(args.seconds), "--final-passes", "3"]
    route(
        [
            *opts,
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
                dst, fieldnames=fields, delimiter="\t", lineterminator="\n", extrasaction="ignore"
            )
            for net in sorted(bare - reported):
                writer.writerow(old_rows.get(net, {"net": net, "status": "unattempted", "why": "outside-cohort"}))
    result = dict(
        feedback(folder),
        previous_unrouted=len(previous),
        targeted_unrouted=len(failed),
        prior_failure_nets=len({name for name, _ in prior}),
        previous_geometry_findings=len(findings),
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
