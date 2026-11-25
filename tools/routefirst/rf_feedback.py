# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Release routed neighbors of failed pins and give those pins first access on the next route."""

import argparse
from bisect import bisect_left, bisect_right
import csv
import json
from pathlib import Path
import time

from rf_chunks import CONN, POSE, ROUTE, WIRE, feedback, records, route, section


def localized_seed(frame, failed, radius, output, all_pins=False):
    owners = {row["fail_pin"].rsplit("/", 1)[0] for row in failed if row["fail_pin"]}
    owners.update(row["fail_pin"] for row in failed if row["fail_pin"])
    if all_pins:
        missing = {row["net"] for row in failed}
        for name, head, _ in records(frame):
            if name.replace(r"\/", "/") in missing:
                for inst, pin in CONN.findall(head.partition(" + ")[0]):
                    owners.add((pin if inst == "PIN" else inst).replace(r"\/", "/"))
    centers = []
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
            if paths and any(touches(path) for path in paths):
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
    return released


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--router", type=Path, default=Path(__file__).with_name("rf_route"))
    p.add_argument("--radius", type=int, default=540, help="access window radius in DEF units")
    p.add_argument("--all-pins", action="store_true", help="release access at every endpoint of the failed nets")
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
    if not failed:
        p.error("previous routing report has no failed nets")
    seed = folder / "seed.def"
    released = localized_seed(args.previous / "routed.def", failed, args.radius, seed, args.all_pins)
    (folder / "released.json").write_text(json.dumps(released))
    first = folder / "priority"
    only = folder / "first.txt"
    only.write_text("".join(r["net"] + "\n" for r in failed))
    opts = [args.router, *ROUTE, "--time-limit", str(args.seconds), "--final-passes", "3"]
    route([*opts, "--only", only, seed, first / "routed.def", *args.lefs], first)
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
    result = dict(
        feedback(folder),
        previous_unrouted=len(failed),
        released=len(released),
        radius=args.radius,
        all_pins=args.all_pins,
        wall_s=time.monotonic() - start,
    )
    (folder / "feedback.json").write_text(json.dumps(result, indent=2))
    print(json.dumps(result))


if __name__ == "__main__":
    main()
