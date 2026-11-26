# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Route independent tile interiors, then route the remaining nets on the assembled LiveHD DEF."""

import argparse
import concurrent.futures
from contextlib import contextmanager
import csv
import fcntl
import hashlib
from http.server import BaseHTTPRequestHandler
import json
import os
from pathlib import Path
import re
import socket
from socketserver import UnixStreamServer
import stat
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


def intersection_area(a, b):
    return max(0, min(a[2], b[2]) - max(a[0], b[0])) * max(0, min(a[3], b[3]) - max(a[1], b[1]))


def partition(frame, nx, ny, plan_path=None):
    cells, ports = {}, {}
    labels = []
    jobs = []
    boxes = []
    current = ""
    cross = []
    with frame.open() as src:
        for line in src:
            current = section(line, current)
            if line.startswith("DESIGN "):
                design = line.split()[1]
            if line.startswith("DIEAREA "):
                (x0, y0), (x1, y1) = [tuple(map(int, p)) for p in re.findall(POINT, line)]
                if plan_path:
                    plans = [
                        r
                        for r in json.loads(plan_path.read_text())
                        if r.get("class") == "livehd.placement.chunk_plan" and r.get("design") == design
                    ]
                    if len(plans) != 1:
                        raise ValueError(f"expected one placement chunk plan for {design}")
                    occupied = []
                    for home in plans[0]["chunks"]:
                        shape = home.get("region_dbu") or [home["box_dbu"]]
                        for rect in shape:
                            l, b, r, t = rect
                            if not (x0 <= l < r <= x1 and y0 <= b < t <= y1):
                                raise ValueError("chunk rectangle outside routing frame")
                            if any(intersection_area(rect, other) for other in occupied):
                                raise ValueError("independent routing workers require disjoint placement regions")
                            occupied.append(rect)
                        labels.append(home["id"])
                        boxes.append(shape)
                else:
                    xs = [x0 + (x1 - x0) * i // nx for i in range(nx + 1)]
                    ys = [y0 + (y1 - y0) * j // ny for j in range(ny + 1)]
                    boxes = [[(xs[i], ys[j], xs[i + 1], ys[j + 1])] for j in range(ny) for i in range(nx)]
                    labels = list(range(len(boxes)))
                jobs = [[] for _ in boxes]
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
                    if plan_path:
                        owner = next(
                            (
                                i
                                for i, shape in enumerate(boxes)
                                if any(l <= x < r and b <= y < t for l, b, r, t in shape)
                            ),
                            None,
                        )
                        owners.add(owner)
                    else:
                        i = min(nx - 1, max(0, ((x - x0 + 1) * nx - 1) // (x1 - x0)))
                        j = min(ny - 1, max(0, ((y - y0 + 1) * ny - 1) // (y1 - y0)))
                        owners.add(j * nx + i)
                    if len(owners) > 1:
                        break
                if len(owners) == 1 and None not in owners and "+ USE CLOCK" not in line:
                    jobs[owners.pop()].append(name)
                else:
                    cross.append(name)
    if not boxes:
        raise ValueError("missing rectangular DIEAREA")
    return boxes, jobs, cross, labels


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
    for path in paths:
        m = WIRE.fullmatch(path)
        if not m:
            raise ValueError(f"unknown rf_route geometry: {path}")
        x, y = int(m[1]), int(m[2])
        x2 = int(m[4]) if m[4] not in (None, "*") else x
        y2 = int(m[5]) if m[5] not in (None, "*") else y
        rect = (min(x, x2) - 128, min(y, y2) - 128, max(x, x2) + 128, max(y, y2) + 128)
        if sum(intersection_area(rect, slab) for slab in box) < (rect[2] - rect[0]) * (rect[3] - rect[1]):
            return False
    return bool(paths)


@contextmanager
def socket_directory_lock(path):
    # Serialize probing, binding and removal by coordinators sharing this directory.
    directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
    try:
        fcntl.flock(directory, fcntl.LOCK_EX)
        yield
    finally:
        os.close(directory)


def check_socket_path(path, *, remove_stale=False):
    try:
        entry = path.lstat()
    except FileNotFoundError:
        return
    if not stat.S_ISSOCK(entry.st_mode):
        raise ValueError(f"agent socket path already exists and is not a socket: {path}")
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as probe:
        probe.settimeout(1)
        try:
            probe.connect(str(path))
        except ConnectionRefusedError:
            if remove_stale and os.path.samestat(entry, path.lstat()):
                path.unlink()
            return
    raise ValueError(f"a resident router already serves {path}")


def socket_commands(process, path):
    """Use the same local POST /agent protocol as tools/lhd_gui.py."""
    ready = process.stdout.readline()
    if not ready:
        raise RuntimeError("router stopped before agent startup")
    state = json.loads(ready)
    finished = False

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_POST(self):
            nonlocal state, finished
            try:
                if self.path != "/agent":
                    self.send_error(404)
                    return
                size = int(self.headers.get("Content-Length", 0))
                if not 0 < size <= 65536:
                    raise ValueError("expected one JSON command")
                req = json.loads(self.rfile.read(size))
                op = req["cmd"]
                if op == "status":
                    reply = dict(ok=True, **state)
                else:
                    if op == "retry":
                        command = "retry {} {} {} {}".format(
                            int(req["budget"]), int(req["passes"]), int(req["ripup"]), float(req["seconds"])
                        )
                    elif op == "release":
                        name = req["net"]
                        if not isinstance(name, str) or not name or any(c.isspace() for c in name):
                            raise ValueError("expected one net name")
                        command = "release " + name
                    elif op == "finish":
                        command = op
                    else:
                        raise ValueError("expected status, release, retry or finish")
                    print(command, file=process.stdin, flush=True)
                    finished = op == "finish"
                    if finished:
                        reply = {"ok": True}
                    else:
                        result = json.loads(process.stdout.readline())
                        if "error" not in result:
                            state = result
                        reply = dict(ok="error" not in result, **result)
            except (KeyError, ValueError, TypeError) as exc:
                reply = {"ok": False, "error": str(exc)}
            data = json.dumps(reply).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

    # Bind without unlinking an existing socket. Make it private before
    # listening and remove only our own socket at shutdown.
    with UnixStreamServer(str(path), Handler, bind_and_activate=False) as server:
        with socket_directory_lock(path):
            check_socket_path(path, remove_stale=True)
            server.server_bind()
            owned = path.lstat()
            path.chmod(0o600)
            server.server_activate()
        try:
            server.timeout = 1
            while not finished:
                if process.poll() is not None:
                    raise RuntimeError(f"resident router exited with status {process.returncode}")
                server.handle_request()
        finally:
            with socket_directory_lock(path):
                try:
                    if os.path.samestat(owned, path.lstat()):
                        path.unlink()
                except FileNotFoundError:
                    pass


def worker_options(parser):
    drivers = parser.add_mutually_exclusive_group()
    drivers.add_argument("--lhd", type=Path, help="LiveHD executable for independent routing workers")
    drivers.add_argument("--router", type=Path, help=argparse.SUPPRESS)


def worker_command(args):
    if args.router:
        return [args.router]
    binary = args.lhd
    if binary is None:
        # The compatibility launchers have //lhd in their Bazel runfiles.
        from python.runfiles import Runfiles

        binary = Path(Runfiles.Create().Rlocation("_main/lhd/lhd"))
    return [binary.resolve(), "--route-worker"]


def route(args, folder, agent=False, socket_path=None):
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "command.json").write_text(json.dumps([str(x) for x in args]))
    with (folder / "stats.json").open("w") as out, (folder / "stderr.log").open("w") as err:
        if agent:
            with subprocess.Popen(
                args, stdin=subprocess.PIPE if socket_path else None, stdout=subprocess.PIPE, stderr=err, text=True
            ) as process:
                if socket_path:
                    try:
                        socket_commands(process, socket_path)
                    finally:
                        process.stdin.close()
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
                    *args.worker,
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


def feedback(folder, homes=()):
    with (folder / "feedback.tsv").open() as src:
        failures = [dict(row) for row in csv.DictReader(src, delimiter="\t") if row["status"] != "routed"]
    stats = json.loads((folder / "stats.json").read_text())
    by_home = {}
    if homes:
        for row in failures:
            owner = "unlocated"
            if row.get("fail_x_dbu") and row.get("fail_y_dbu"):
                x, y = int(row["fail_x_dbu"]), int(row["fail_y_dbu"])
                owner = next(
                    (str(name) for name, slabs in homes if any(l <= x < r and b <= y < t for l, b, r, t in slabs)),
                    "exterior",
                )
            counts = by_home.setdefault(owner, {})
            why = row["why"] or row["status"]
            counts[why] = counts.get(why, 0) + 1
    return {
        "unrouted": len(failures),
        "failures_by_home": by_home,
        "violations": {k: v for k, v in stats.items() if k.startswith("verify_") and v},
        "first_failures": failures[:20],
        "route_cpu_s": stats["route_cpu_s"],
    }


def die_with_parent():
    """Stop when the lhd that started this coordinator is killed, so that no worker is left running."""
    try:
        import ctypes
        import signal

        parent = os.getppid()
        ctypes.CDLL(None).prctl(1, signal.SIGTERM)  # PR_SET_PDEATHSIG
        if os.getppid() != parent:
            raise SystemExit(1)
    except (OSError, AttributeError):
        pass


def main():
    die_with_parent()
    p = argparse.ArgumentParser(description=__doc__)
    worker_options(p)
    homes = p.add_mutually_exclusive_group()
    homes.add_argument(
        "--plan", type=Path, help="use the rectangular or stepped regions of a LiveHD fp_chunks.json"
    )
    homes.add_argument(
        "--grid",
        type=int,
        nargs=2,
        metavar=("NX", "NY"),
        help="override the two- or four-home ASAP7 starting grid",
    )
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
    p.add_argument("--reuse-if-compatible", type=Path, help=argparse.SUPPRESS)
    p.add_argument("--out", required=True, type=Path)
    p.add_argument("frame", type=Path)
    p.add_argument("lefs", nargs="+", type=Path)
    p.add_argument(
        "--agent",
        action="store_true",
        help="keep the assembled router resident for status, release and retry commands",
    )
    p.add_argument(
        "--agent-socket", type=Path, help="serve the resident router over a local Unix socket for tools/lhd_gui.py"
    )
    p.add_argument("--timing", type=Path, help="LiveHD pin slack TSV from this placement")
    p.add_argument("--crit-frac", type=float, default=0.0)
    p.add_argument("--pd-alpha", type=float, default=0.3)
    args = p.parse_args()
    args.worker = worker_command(args)
    if args.grid is None:
        args.grid = (2, 2)
        if not args.plan:
            with args.frame.open() as frame:
                for line in frame:
                    if line.startswith("COMPONENTS "):
                        if int(line.split()[1]) <= 150_000 or args.workers < 4:
                            args.grid = (2, 1)
                        break
    args.agent = args.agent or args.agent_socket is not None
    if args.agent_socket is not None:
        # Find out now, not after every worker has run, that the socket cannot be served.
        if len(os.fsencode(str(args.agent_socket))) > 107:
            p.error(f"agent socket path is longer than a Unix socket address allows: {args.agent_socket}")
        try:
            with socket_directory_lock(args.agent_socket):
                check_socket_path(args.agent_socket)
        except (OSError, ValueError) as exc:
            p.error(f"cannot use agent socket {args.agent_socket}: {exc}")
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
    boxes, assignments, cross, labels = partition(args.frame, *args.grid, args.plan)
    if any(i < 0 or i >= len(boxes) for i in args.release):
        p.error("released tile outside grid")

    def digest(file):
        with file.open("rb") as stream:
            h = hashlib.sha256()
            for block in iter(lambda: stream.read(1 << 20), b""):
                h.update(block)
            return h.hexdigest()

    stamp = dict(
        worker_identity=(digest(args.router) if args.router else subprocess.check_output(
            [*args.worker, "--identity"], text=True).strip()),
        inputs=[digest(f) for f in (args.frame, *args.lefs, Path(__file__))],
        timing=digest(args.timing) if args.timing else None,
        placement_plan=digest(args.plan) if args.plan else None,
        alpha=args.pd_alpha,
        critical_fraction=args.crit_frac,
        grid=list(args.grid),
        worker_seconds=args.worker_seconds,
        trunks_first=args.trunks_first,
        trunk_seconds=args.finish_seconds if args.trunks_first else None,
    )
    if not args.reuse and args.reuse_if_compatible:
        prior = args.reuse_if_compatible
        if (prior / "inputs.json").is_file() and json.loads((prior / "inputs.json").read_text()) == stamp:
            args.reuse = prior
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
                    *args.worker,
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
        grid=None if args.plan else args.grid,
        homes=labels,
        regions_dbu=boxes,
        crossing_or_clock=len(cross),
        assigned=[len(x) for x in assignments],
        retained=len(wires),
        reused_workers=len(boxes) if args.reuse else 0,
        worker_wall_s=time.monotonic() - start,
    )
    (args.out / "assembly.json").write_text(json.dumps(prefix, indent=2))
    route(
        [
            *args.worker,
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
        socket_path=args.agent_socket,
    )
    result = dict(prefix, **feedback(work, list(zip(labels, boxes))), wall_s=time.monotonic() - start)
    (args.out / "feedback.json").write_text(json.dumps(result, indent=2))
    print(json.dumps(result))


if __name__ == "__main__":
    main()
