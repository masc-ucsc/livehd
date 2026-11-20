# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Prepare portable same-build requests and conditionally publish completed home placements."""

import contextlib
import copy
import fcntl
import hashlib
import json
import os
import resource
import shutil
import subprocess
import sys
import tempfile
import time
import uuid
from concurrent.futures import ThreadPoolExecutor
from functools import lru_cache
from pathlib import Path

from chunk_resources import memory_group, monitor
from chunk_score import contribution, refresh_neighbors


def digest(value):
    return hashlib.sha256(
        json.dumps(value, sort_keys=True, separators=(",", ":")).encode()
    ).hexdigest()


def file_digest(path):
    with Path(path).open("rb") as stream:
        checksum = hashlib.sha256()
        for block in iter(lambda: stream.read(1 << 20), b""):
            checksum.update(block)
        return checksum.hexdigest()


@lru_cache(maxsize=8)
def engine_digest(path, identity):
    return file_digest(path)


def engine(path):
    info = os.stat(path)
    return engine_digest(
        path, (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns)
    )


def dependencies(state, home):
    return digest(
        [
            state["results"].get(home),
            state["contacts"].get(home),
            state.get("holds", {}).get(home),
            state.get("interface_revision", {}).get(home, 0),
        ]
    )


def issue(
    work,
    home,
    stage,
    folder,
    *,
    iterations=None,
    density=None,
    poses=(),
    seed=None,
    freeze_macros=None,
    live_frames=False,
):
    if home not in work.jobs:
        raise ValueError(f"no exported packet for {home}")
    folder = Path(folder).resolve()
    folder.mkdir(parents=True, exist_ok=False)
    with work.writer() as (state, _):
        job_path, _ = work.prepare_job(
            state,
            home,
            stage,
            folder,
            iterations=iterations,
            density=density,
            poses=poses,
            seed=seed,
            freeze_macros=freeze_macros,
            live_frames=live_frames,
        )
        base = dependencies(state, home)
    records = json.loads(job_path.read_text())
    job = records[0]["jobs"][0]
    source = folder / job["input"]
    # This directory is transferable. Copy rather than hard-link an editable client bundle into the immutable packet store.
    shutil.copyfile(source, folder / "packet.bin")
    job["input"] = "packet.bin"
    job_path.write_text(json.dumps(records))
    ticket = {
        "version": 1,
        "manifest": work.manifest_digest,
        "engine": engine(work.exe),
        "home": home,
        "stage": stage,
        "base": base,
        "packet": file_digest(folder / "packet.bin"),
        "job": file_digest(job_path),
    }
    key = digest(ticket)
    (folder / "ticket.json").write_text(json.dumps(ticket))
    (work.root / "issued").mkdir(exist_ok=True)
    # Identical concurrent issues share a content key. Readers see a complete ticket after the rename.
    from chunk_flow import atomic_json

    atomic_json(work.root / "issued" / key, ticket)
    return key


@contextlib.contextmanager
def published_attempt(root, home, stage, attempt):
    # A lock on this descriptor expires even when the coordinating process crashes.
    directory = root / "running"
    directory.mkdir(exist_ok=True)
    with tempfile.NamedTemporaryFile(mode="w", dir=directory) as lease:
        fcntl.flock(lease, fcntl.LOCK_EX)
        json.dump({"home": home, "stage": stage, "directory": str(attempt)}, lease)
        lease.flush()
        yield


def active_jobs(root):
    jobs = {}
    for path in (root / "running").glob("*"):
        try:
            with path.open() as lease:
                try:
                    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
                except BlockingIOError:
                    data = json.load(lease)
                    jobs[data["home"]] = data
        except (FileNotFoundError, json.JSONDecodeError):
            continue
    return jobs


def execute(exe, folder, *, publish=None, cgroup_root=None):
    folder = Path(folder).resolve()
    ticket = json.loads((folder / "ticket.json").read_text())
    if ticket["version"] != 1 or ticket["engine"] != engine(exe):
        raise ValueError("chunk worker build does not match the request")
    if (
        file_digest(folder / "job.json") != ticket["job"]
        or file_digest(folder / "packet.bin") != ticket["packet"]
    ):
        raise ValueError("chunk request checksum mismatch")
    attempt = Path(tempfile.mkdtemp(dir=folder, prefix="attempt-"))
    started = time.time()
    start = time.monotonic()
    policy = json.loads((folder / "job.json").read_text())[0]
    budget = policy["budget_bytes"]
    with (
        memory_group(cgroup_root, budget) as launcher,
        publish(attempt) if publish else contextlib.nullcontext(),
        (attempt / "worker.log").open("w") as log,
        subprocess.Popen(
            [
                *launcher,
                exe,
                "place-chunks",
                folder / "job.json",
                "--out",
                attempt / "result",
            ],
            stdout=log,
            stderr=log,
            start_new_session=True,
        ) as proc,
        monitor(proc, budget if policy["require_fit"] else 0) as usage,
    ):
        proc.wait()
    metadata = attempt / "result/metadata.json"
    if not metadata.is_file():
        raise ValueError(
            f"worker returned {proc.returncode} without feedback, see {attempt}/worker.log"
        )
    report = next(
        r
        for r in json.loads(metadata.read_text())
        if r["class"] == "livehd.placement.chunk_feedback"
    )
    if proc.returncode or report["status"] not in ("placed", "fixed_only"):
        raise ValueError(
            report.get("reason", f"worker failed, see {attempt}/worker.log")
        )
    files = ["metadata.json", "0/feedback.json"]
    for name in ("positions.json", "preview.json", "handoff.bin", "gp.json"):
        if (attempt / "result/0" / name).is_file():
            files.append("0/" + name)
    receipt = {
        "ticket": digest(ticket),
        "attempt": attempt.name,
        "worker_pid": proc.pid,
        "started": started,
        "finished": time.time(),
        "seconds": time.monotonic() - start,
        "worker_tree_bytes": usage["peak_tree_bytes"],
        "sizes": {key: (attempt / "result" / key).stat().st_size for key in files},
        "files": {key: file_digest(attempt / "result" / key) for key in files},
    }
    from chunk_flow import atomic_json

    atomic_json(folder / "receipt.json", receipt)
    return receipt


def collect(work, folder):
    from chunk_flow import one

    folder = Path(folder).resolve()
    receipt = json.loads((folder / "receipt.json").read_text())
    ticket = json.loads((folder / "ticket.json").read_text())
    key = digest(ticket)
    issued = work.root / "issued" / key
    if (
        receipt["ticket"] != key
        or not issued.is_file()
        or json.loads(issued.read_text()) != ticket
    ):
        raise ValueError("unknown placement request for this workspace")
    if (
        engine(work.exe) != ticket["engine"]
        or work.manifest_digest != ticket["manifest"]
    ):
        raise ValueError("placement plan or engine changed during dispatch")
    home, stage = ticket["home"], ticket["stage"]
    # Import into coordinator-owned storage before validation. Workers can remove their directories afterwards.
    dest = Path(tempfile.mkdtemp(prefix=stage + "-", dir=work.root))
    try:
        attempt = receipt["attempt"]
        if Path(attempt).name != attempt or not attempt.startswith("attempt-"):
            raise ValueError("invalid attempt directory")
        allowed = {
            "metadata.json",
            "0/feedback.json",
            "0/positions.json",
            "0/preview.json",
            "0/handoff.bin",
            "0/gp.json",
        }
        if (
            not {"metadata.json", "0/feedback.json"} <= receipt["files"].keys()
            or set(receipt["files"]) - allowed
        ):
            raise ValueError("unexpected chunk output")
        out = dest / "result"
        (out / "0").mkdir(parents=True)
        for filename, checksum in receipt["files"].items():
            shutil.copyfile(folder / attempt / "result" / filename, out / filename)
            if file_digest(out / filename) != checksum:
                raise ValueError(f"corrupt worker output {filename}")
        job_path = folder / "job.json"
        if file_digest(job_path) != ticket["job"]:
            raise ValueError("job changed during dispatch")
        job = json.loads(job_path.read_text())[0]
        report = one(
            json.loads((out / "metadata.json").read_text()),
            "chunk_feedback",
            work.design,
        )
        feedback = one(
            json.loads((out / "0/feedback.json").read_text()),
            "chunk_feedback",
            work.design,
        )
        if (
            report["id"] != home
            or report.get("stage") != stage
            or feedback.get("stage") != stage
            or report["plan_revision"] != job["plan_revision"]
            or report["status"] not in ("placed", "fixed_only")
            or (job["require_fit"] and report.get("over_budget"))
            or feedback.get("id") != home
            or feedback.get("handoff_checksum") != report.get("handoff_checksum")
        ):
            raise ValueError("worker returned incompatible feedback")
        keys = (
            "iterations",
            "hpwl_dbu",
            "estimated_wns_ps",
            "estimated_worst_slack_ps",
            "timed_nets",
            "peak_rss_bytes",
            "worker_seconds",
            "over_budget",
            "solver_movable_nodes",
            "gp_moved",
            "gp_mean_displacement_dbu",
            "gp_max_displacement_dbu",
        )
        row = {k: report[k] for k in keys if k in report}
        row.update(
            status="done",
            directory=dest.name,
            report=str((out / "0/feedback.json").relative_to(work.root)),
            dispatch=key,
            worker_tree_bytes=receipt.get("worker_tree_bytes"),
            started=receipt.get("started"),
            finished=receipt.get("finished"),
            worker_pid=receipt.get("worker_pid"),
            transfer_bytes=sum(receipt["sizes"].values()),
            dispatch_seconds=receipt["seconds"],
        )
        for field, name in [
            ("preview", "preview.json"),
            ("positions", "positions.json"),
            ("handoff", "handoff.bin"),
        ]:
            if (out / "0" / name).is_file():
                row[field] = str((out / "0" / name).relative_to(work.root))
        if (
            report["status"] == "placed"
            and not {"positions", "preview", "handoff"} <= row.keys()
        ):
            raise ValueError("worker result lacks a required placement artifact")
        if report.get("handoff_checksum"):
            if "handoff" not in row:
                raise ValueError("missing worker handoff")
            row["handoff_checksum"] = report["handoff_checksum"]
        if "preview" in row:
            frame = json.loads((work.root / row["preview"]).read_text())[0]
            row.update(
                {
                    k: frame[k]
                    for k in ("peak_density", "target_density", "overflow_fraction")
                    if k in frame
                }
            )
        with work.writer() as (state, _):
            stages = state["results"].get(home, {})
            if (
                stages.get(stage, {}).get("dispatch") == key
                and stages[stage]["status"] == "done"
            ):
                shutil.rmtree(dest)
                return work.rows(state, [home])[0]
            if dependencies(state, home) != ticket["base"]:
                raise ValueError(f"{home} or its interface changed since dispatch")
            parent = copy.deepcopy(state)
            _, before = contribution(work, state, home)
            stages = state["results"].setdefault(home, {})
            if stage == "macro" and "final" in stages:
                stages["final"].update(status="stale", reason="new macro handoff")
            stages[stage] = row
            refresh_neighbors(work, state, home, before, feedback.get("score"))
            from chunk_journal import Journal
            from chunk_live import revision

            Journal(work.root).checkpoint(revision(state), parent)
            work.save(state)
            return work.rows(state, [home])[0]
    except Exception:
        shutil.rmtree(dest, ignore_errors=True)
        raise


def wave(
    work,
    stage,
    ids,
    workers,
    *,
    home_options=None,
    admit=None,
    cgroup_root=None,
    **options,
):
    if workers < 1:
        raise ValueError("workers must be positive")
    state = work.state()
    from chunk_journal import Journal
    from chunk_live import revision
    from chunk_score import Exterior

    journal = Journal(work.root)
    journal.snapshot(revision(state), Exterior(work, state).measure(), state)
    homes = (
        ids
        if ids is not None
        else [
            h
            for h in work.jobs
            if state["results"].get(h, {}).get(stage, {}).get("status") != "done"
        ]
    )

    def solve(home):
        from contextlib import nullcontext

        reservation = (
            admit(work.jobs[home]["estimated_bytes"]) if admit else nullcontext()
        )
        with (
            reservation,
            tempfile.TemporaryDirectory(dir=work.root, prefix="transfer-") as scratch,
        ):
            path = Path(scratch) / "request"
            issue(
                work, home, stage, path, **options, **(home_options or {}).get(home, {})
            )
            execute(
                work.exe,
                path,
                cgroup_root=cgroup_root,
                publish=lambda attempt: published_attempt(
                    work.root, home, stage, attempt
                ),
            )
            return collect(work, path)

    start = time.monotonic()
    with ThreadPoolExecutor(max_workers=workers) as pool:
        rows = list(pool.map(solve, homes))
    state = work.state()
    journal.snapshot(revision(state), Exterior(work, state).measure(), state)
    if rows:
        from chunk_flow import atomic_json

        events = sorted(
            (row[stage][key], delta)
            for row in rows
            for key, delta in (("started", 1), ("finished", -1))
            if row[stage].get(key)
        )
        active = peak = 0
        for _, delta in events:
            active += delta
            peak = max(peak, active)
        directory = work.root / "waves"
        directory.mkdir(exist_ok=True)
        usage = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
        atomic_json(
            directory / (uuid.uuid4().hex + ".json"),
            {
                "stage": stage,
                "completed": time.time(),
                "seconds": time.monotonic() - start,
                "peak_parallel_workers": peak,
                "coordinator_peak_bytes": usage
                * (1 if sys.platform == "darwin" else 1024),
                "homes": {
                    row["id"]: {
                        k: row[stage].get(k)
                        for k in (
                            "dispatch",
                            "worker_tree_bytes",
                            "peak_rss_bytes",
                            "worker_seconds",
                        )
                    }
                    for row in rows
                },
            },
        )
    return rows
