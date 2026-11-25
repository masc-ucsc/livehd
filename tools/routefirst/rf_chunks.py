# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Route independent tile interiors, then route the remaining nets on the assembled LiveHD DEF."""

import argparse
import concurrent.futures
import csv
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile
import time


# LiveHD writes each unrouted net and its connections on one line. rf_route
# appends one path per line. Refuse other producers rather than guessing.
POINT = r"\(\s*(-?\d+)\s+(-?\d+)\s*\)"
POSE = re.compile(r"\+ (?:FIXED|PLACED|COVER) " + POINT)
CONN = re.compile(r"\( (\S+) (\S+) \)")
WIRE = re.compile(
    r"(?:\+ ROUTED|NEW) M[1-7] \( (-?\d+) (-?\d+)( 0)? \) (?:VIA[1-6][2-7]|\( (\*|-?\d+) (\*|-?\d+) 0 \))"
)
ROUTE = [
    "--hweight",
    "1.05",
    "--via-cost",
    "120",
    "--pd-alpha",
    "0.3",
    "--budget",
    "300000",
    "--ripup",
    "3",
    "--rip-width",
    "6",
]


def section(line, current):
    if line.startswith("END "):
        return ""
    for name in ("NETS", "COMPONENTS", "PINS"):
        if line.startswith(name + " "):
            return name
    return current


def partition(frame, nx, ny):
    cells, ports = {}, {}
    jobs = [[] for _ in range(nx * ny)]
    boxes = []
    current = ""
    cross = []
    with frame.open() as src:
        for line in src:
            current = section(line, current)
            if line.startswith("DIEAREA "):
                (x0, y0), (x1, y1) = [tuple(map(int, p)) for p in re.findall(POINT, line)]
                xs = [x0 + (x1 - x0) * i // nx for i in range(nx + 1)]
                ys = [y0 + (y1 - y0) * j // ny for j in range(ny + 1)]
                boxes = [(xs[i], ys[j], xs[i + 1], ys[j + 1]) for j in range(ny) for i in range(nx)]
            if not line.startswith("- ") or not current:
                continue
            name = line.split(None, 2)[1]
            if current in ("COMPONENTS", "PINS"):
                pose = POSE.search(line)
                if pose is None:
                    raise ValueError(f"{current} record without a pose: {name}")
                (cells if current == "COMPONENTS" else ports)[name] = tuple(map(int, pose.groups()))
            elif current == "NETS":
                if not line.rstrip().endswith(";") or re.search(r"\+ (ROUTED|FIXED|COVER|SHIELD) ", line):
                    raise ValueError(f"expected an unrouted LiveHD signal net: {name}")
                owners = set()
                pins = CONN.findall(line.partition(" + ")[0])
                if len(pins) < 2:
                    continue
                for inst, pin in pins:
                    x, y = ports[pin] if inst == "PIN" else cells[inst]
                    # Cells are assigned by origin. The worker must also prove
                    # that all its emitted paths lie inside this same tile.
                    i = min(nx - 1, max(0, ((x - x0 + 1) * nx - 1) // (x1 - x0)))
                    j = min(ny - 1, max(0, ((y - y0 + 1) * ny - 1) // (y1 - y0)))
                    owners.add(j * nx + i)
                    if len(owners) > 1:
                        break
                if len(owners) == 1 and "+ USE CLOCK" not in line:
                    jobs[owners.pop()].append(name)
                else:
                    cross.append(name)
    if not boxes:
        raise ValueError("missing rectangular DIEAREA")
    return boxes, jobs, cross


def records(frame):
    with frame.open() as src:
        for line in src:
            if line.startswith("NETS "):
                break
        else:
            raise ValueError("missing NETS")
        for line in src:
            if line.startswith("END NETS"):
                return
            if not line.startswith("- "):
                raise ValueError("unsupported signal net record")
            head = line
            paths = []
            while not line.rstrip().endswith(";"):
                line = next(src)
                paths.append(line.strip().removesuffix(";").strip())
            yield head.split(None, 2)[1], head, paths
        raise ValueError("unterminated NETS")


def contained(paths, box):
    # rf_route emits ASAP7 default-width M2-M7 wires and VIA12..67. A
    # 128-DBU seam reserve covers their enclosures and the calibrated spacing.
    xl, yl, xh, yh = box
    for path in paths:
        m = WIRE.fullmatch(path)
        if not m:
            raise ValueError(f"unknown rf_route geometry: {path}")
        x, y = int(m[1]), int(m[2])
        x2 = int(m[4]) if m[4] not in (None, "*") else x
        y2 = int(m[5]) if m[5] not in (None, "*") else y
        if not (
            xl + 128 <= min(x, x2) <= max(x, x2) <= xh - 128 and yl + 128 <= min(y, y2) <= max(y, y2) <= yh - 128
        ):
            return False
    return bool(paths)


def route(args, folder, agent=False):
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "command.json").write_text(json.dumps([str(x) for x in args]))
    with (folder / "stats.json").open("w") as out, (folder / "stderr.log").open("w") as err:
        if agent:
            with subprocess.Popen(args, stdout=subprocess.PIPE, stderr=err, text=True) as process:
                for line in process.stdout:
                    if line.strip() == "{":
                        out.write(line)
                        out.write(process.stdout.read())
                        break
                    print(line, end="", flush=True)
                if process.wait():
                    raise subprocess.CalledProcessError(process.returncode, args)
        else:
            subprocess.run(args, stdout=out, stderr=err, check=True)
    return json.loads((folder / "stats.json").read_text())


def worker(args, frame, box, names, folder):
    folder.mkdir()
    requested = set(names)
    accepted = {}
    if names:
        only = folder / "nets.txt"
        only.write_text("".join(x.replace(r"\/", "/") + "\n" for x in names))
        with tempfile.TemporaryDirectory(dir=folder) as tmp:
            out = Path(tmp) / "worker.def"
            route(
                [
                    args.router,
                    *args.costs,
                    "--only",
                    only,
                    "--final-passes",
                    "0",
                    "--time-limit",
                    str(args.worker_seconds),
                    "--report",
                    folder / "feedback.tsv",
                    frame,
                    out,
                    *args.lefs,
                ],
                folder,
            )
            for name, _, paths in records(out):
                if name in requested and contained(paths, box):
                    accepted[name] = paths
    (folder / "wires.json").write_text(json.dumps(accepted))
    return accepted


def assemble(frame, dest, wires):
    seen = 0
    current = ""
    with frame.open() as src, dest.open("w") as out:
        for line in src:
            current = section(line, current)
            if current == "NETS" and line.startswith("- "):
                name = line.split(None, 2)[1]
                if name in wires:
                    body = line.rstrip()
                    if not body.endswith(";"):
                        raise ValueError(f"cannot insert wires into {name}")
                    line = body[:-1] + "\n      " + "\n      ".join(wires[name]) + " ;\n"
                    seen += 1
            out.write(line)
    if seen != len(wires):
        raise ValueError("worker wires do not match source net records")


def feedback(folder):
    with (folder / "feedback.tsv").open() as src:
        failures = [dict(row) for row in csv.DictReader(src, delimiter="\t") if row["status"] != "routed"]
    stats = json.loads((folder / "stats.json").read_text())
    return {
        "unrouted": len(failures),
        "violations": {k: v for k, v in stats.items() if k.startswith("verify_") and v},
        "first_failures": failures[:20],
        "route_cpu_s": stats["route_cpu_s"],
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--router", type=Path, default=Path(__file__).with_name("rf_route"))
    p.add_argument("--grid", type=int, nargs=2, default=(2, 2), metavar=("NX", "NY"))
    p.add_argument("--workers", type=int, default=4)
    p.add_argument("--worker-seconds", type=float, default=120)
    p.add_argument("--finish-seconds", type=float, default=600)
    p.add_argument(
        "--trunks-first", action="store_true", help="route crossing and clock nets before the interior workers"
    )
    p.add_argument(
        "--release",
        type=int,
        nargs="*",
        default=[],
        help="give these tile interiors to the coordinator after feedback",
    )
    p.add_argument("--reuse", type=Path, help="reuse the verified worker inputs and wires of this trial")
    p.add_argument("--out", required=True, type=Path)
    p.add_argument("frame", type=Path)
    p.add_argument("lefs", nargs="+", type=Path)
    p.add_argument(
        "--agent",
        action="store_true",
        help="keep the assembled router resident for status, release and retry commands",
    )
    p.add_argument("--timing", type=Path, help="LiveHD pin slack TSV from this placement")
    p.add_argument("--crit-frac", type=float, default=0.0)
    p.add_argument("--pd-alpha", type=float, default=0.3)
    args = p.parse_args()
    if not 0 <= args.pd_alpha <= 1 or not 0 <= args.crit_frac <= 1 or (args.crit_frac and not args.timing):
        p.error("pd-alpha and crit-frac must be in [0,1], with timing supplied for critical-net priority")
    args.costs = list(ROUTE)
    args.costs[args.costs.index("--pd-alpha") + 1] = str(args.pd_alpha)
    if args.timing:
        args.costs.extend(["--timing", args.timing, "--crit-frac", str(args.crit_frac)])
    if min(*args.grid, args.workers) < 1 or not (args.worker_seconds > 0 and args.finish_seconds > 0):
        p.error("grid, workers and CPU budgets must be positive")
    args.out.mkdir(parents=True, exist_ok=False)
    start = time.monotonic()
    boxes, assignments, cross = partition(args.frame, *args.grid)
    if any(i < 0 or i >= len(boxes) for i in args.release):
        p.error("released tile outside grid")

    def digest(file):
        with file.open("rb") as stream:
            h = hashlib.sha256()
            for block in iter(lambda: stream.read(1 << 20), b""):
                h.update(block)
            return h.hexdigest()

    stamp = dict(
        inputs=[digest(f) for f in (args.router, args.frame, *args.lefs, Path(__file__))],
        timing=digest(args.timing) if args.timing else None,
        alpha=args.pd_alpha,
        critical_fraction=args.crit_frac,
        grid=list(args.grid),
        worker_seconds=args.worker_seconds,
        trunks_first=args.trunks_first,
        trunk_seconds=args.finish_seconds if args.trunks_first else None,
    )
    if args.reuse and json.loads((args.reuse / "inputs.json").read_text()) != stamp:
        raise ValueError("stale worker result: inputs, grid or worker budget changed")
    (args.out / "inputs.json").write_text(json.dumps(stamp))
    frame = args.frame
    if args.trunks_first:
        trunk = args.out / "trunks"
        if args.reuse:
            trunk.symlink_to((args.reuse / "trunks").resolve(), target_is_directory=True)
        else:
            trunk.mkdir()
            only = trunk / "nets.txt"
            only.write_text("".join(x.replace(r"\/", "/") + "\n" for x in cross))
            route(
                [
                    args.router,
                    *args.costs,
                    "--final-passes",
                    "1",
                    "--only",
                    only,
                    "--time-limit",
                    str(args.finish_seconds),
                    "--report",
                    trunk / "feedback.tsv",
                    frame,
                    trunk / "routed.def",
                    *args.lefs,
                ],
                trunk,
            )
        frame = trunk / "routed.def"
    wires = {}

    def one(i):
        folder = args.out / f"worker-{i}"
        if args.reuse:
            folder.symlink_to((args.reuse / f"worker-{i}").resolve(), target_is_directory=True)
            found = json.loads((folder / "wires.json").read_text())
        else:
            found = worker(args, frame, boxes[i], assignments[i], folder)
        return {} if i in args.release else found

    with concurrent.futures.ThreadPoolExecutor(args.workers) as pool:
        for part in pool.map(one, range(len(boxes))):
            if wires.keys() & part.keys():
                raise ValueError("overlapping net assignments")
            wires.update(part)
    seed = args.out / "assembly.def"
    assemble(frame, seed, wires)
    work = args.out / "finish"
    prefix = dict(
        grid=args.grid,
        crossing_or_clock=len(cross),
        assigned=[len(x) for x in assignments],
        retained=len(wires),
        worker_wall_s=time.monotonic() - start,
    )
    (args.out / "assembly.json").write_text(json.dumps(prefix, indent=2))
    route(
        [
            args.router,
            *args.costs,
            "--final-passes",
            "3",
            "--shift",
            "3",
            "--shift-reach",
            "6",
            "--chain",
            "4",
            "--flop-reach",
            "2",
            "--tile-move",
            "--reserve",
            "--time-limit",
            str(args.finish_seconds),
            *(["--agent"] if args.agent else []),
            "--verify-report",
            work / "findings.tsv",
            "--report",
            work / "feedback.tsv",
            seed,
            work / "routed.def",
            *args.lefs,
        ],
        work,
        agent=args.agent,
    )
    result = dict(prefix, **feedback(work), wall_s=time.monotonic() - start)
    (args.out / "feedback.json").write_text(json.dumps(result, indent=2))
    print(json.dumps(result))


if __name__ == "__main__":
    main()
