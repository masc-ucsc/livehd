# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Matched local placement experiments with a frozen exterior and whole-design acceptance."""

import copy
import json
import tempfile
import time
import uuid
from itertools import product
from pathlib import Path

import chunk_dispatch
import chunk_interfaces
from chunk_journal import Journal
from chunk_live import engine_identity, publish_trial, revision
from chunk_score import Exterior, contribution, refresh_neighbors


def absolute_state(work, state):
    result = copy.deepcopy(state)
    for stages in result["results"].values():
        for row in stages.values():
            for key in (
                "report",
                "previous_report",
                "directory",
                "positions",
                "handoff",
                "preview",
            ):
                if row.get(key):
                    row[key] = str(work.root / row[key])
    return result


def objective_cost(score, objective):
    if objective == "hpwl":
        return score["hpwl_um"]
    if objective != "wns":
        raise ValueError("objective must be hpwl or wns")
    if score["estimated_wns_ps"] is None:
        raise ValueError("the placement has no timing estimate")
    return -score["estimated_wns_ps"]


def finish_candidate(work, receipt, item, branch, path):
    score = Exterior(work, branch).measure()
    control = receipt["candidates"]["control"].get("assembled", score)
    delta = {
        key: score[key] - control[key]
        for key in ("hpwl_um", "estimated_wns_ps")
        if score[key] is not None and control[key] is not None
    }
    artifacts = {
        home: branch["results"][home]["final"] for home in receipt["affected_homes"]
    }
    item.update(artifacts[receipt["home"]])
    item.update(
        status="done",
        assembled=score,
        delta_assembled=delta,
        state_path=str(path),
        frame_kind="legal",
        artifacts=artifacts,
    )


def combine(work, base, receipt):
    homes = receipt["affected_homes"]
    states = {
        k: json.loads(Path(row["state_path"]).read_text())
        for k, row in receipt["candidates"].items()
        if row["status"] == "done"
    }
    if len(states) < 2 or len(homes) < 2:
        return
    exterior = Exterior(work, base, homes)
    parts = {
        (key, h): contribution(work, state, h)[1]
        for key, state in states.items()
        for h in homes
    }

    def score(keys):
        return objective_cost(
            exterior.measure([parts[key, h] for key, h in zip(keys, homes)], "final"),
            receipt["objective"],
        )

    # Enumerate small combinations exactly. For larger searches improve one home at a time.
    if len(states) ** len(homes) <= 512:
        best = min(product(states, repeat=len(homes)), key=score)
    else:
        best = [
            min(
                states,
                key=lambda k: objective_cost(
                    receipt["candidates"][k]["assembled"], receipt["objective"]
                ),
            )
        ] * len(homes)
        for i in range(len(homes)):
            best = min((best[:i] + [key] + best[i + 1 :] for key in states), key=score)
    if len(set(best)) < 2:
        return
    merged = copy.deepcopy(base)
    for home, label in zip(homes, best):
        merged["results"][home] = states[label]["results"][home]
        refresh_neighbors(
            work, merged, home, contribution(work, base, home)[1], parts[label, home]
        )
    path = Path(tempfile.mkdtemp(dir=work.root, prefix="combined-")) / "state.json"
    path.write_text(json.dumps(merged))
    item = receipt["candidates"]["combined"] = {"choices": dict(zip(homes, best))}
    finish_candidate(work, receipt, item, merged, path)


def restore_best(work, objective):
    """Recall a complete archived state for this plan, protecting any concurrent edit."""
    journal = Journal(work.root)
    checkpoint = work.state()
    epoch, engine = journal.epoch(), engine_identity(work.exe)
    current = Exterior(work, checkpoint).measure()
    best = journal.best(current["coverage"], objective)
    if not best or objective_cost(best, objective) >= objective_cost(
        current, objective
    ):
        return {"restored": False, "metrics": current}
    trial, branch = best["trial"], best["branch"]
    if trial == "main":
        after = journal.saved_main(branch)
    else:
        from chunk_validate import placement_state

        after = placement_state(work, trial, branch)
    measured = Exterior(work, after).measure()
    if measured["coverage"] != current["coverage"] or objective_cost(
        measured, objective
    ) != objective_cost(best, objective):
        raise ValueError(
            "archived physical placement no longer matches its recorded score"
        )
    publish_trial(
        work, journal, trial, branch, revision(checkpoint), epoch, engine, after
    )
    journal.snapshot(revision(after), measured, after)
    journal.decide(
        trial,
        branch,
        "restored",
        f"Restored the best complete archived placement by {objective}",
    )
    return {
        "restored": True,
        "trial": trial,
        "branch": branch,
        "prior_revision": revision(checkpoint),
        "revision": revision(after),
        "metrics": measured,
    }


def explore(
    work,
    kind,
    workers,
    iterations,
    final_iterations,
    homes=None,
    pitch=None,
    nets=None,
    density=0.4,
    keep=False,
    objective="hpwl",
    admit=None,
    ident=None,
    base_state=None,
    spread=None,
    bus_order=False,
    hypothesis=None,
):
    if min(iterations, final_iterations, workers) < 1 or not 0 < density <= 1:
        raise ValueError("invalid experiment effort or density")
    if hypothesis is not None and (
        not isinstance(hypothesis, str) or not hypothesis.strip()
    ):
        raise ValueError("supply a nonempty experiment hypothesis")
    state = base_state if base_state is not None else work.state()
    base = absolute_state(work, state)
    journal = Journal(work.root)
    start_score = Exterior(work, state).measure()
    ids = work.select(homes or [])
    phase = "macro" if kind == "macro" else "final"
    receipt = {
        "kind": kind,
        "objective": objective,
        "requested_iterations": iterations,
        "final_iterations": final_iterations,
        "id": ident or uuid.uuid4().hex[:12],
        "started": time.time(),
        "home": ids[0],
        "selection": [],
        "stage": "final",
        "hypothesis": hypothesis
        or {
            "macro": "Seed the highest-gain macros toward incident pin intervals and compare after final placement",
            "interfaces": "Allocate shared boundary slots and re-place the incident homes",
            "density": "Compare the requested density with an unchanged equally advanced placement",
        }[kind],
        "checkpoint": revision(state),
        "epoch": journal.epoch(),
        "engine": engine_identity(work.exe),
        "candidates": {},
        "checkpoint_assembled": start_score,
    }
    contract = None
    if kind == "interfaces":
        contract = chunk_interfaces.proposal(
            work, state, pitch, nets, spread=spread, bus_order=bus_order
        )
        receipt["interface_policy"] = {
            "spread": contract["spread"],
            "bus_order": contract["bus_order"],
            "unassigned": len(contract["conflicts"]),
            "edges": contract["edges"],
        }
        if not contract["feasible"]:
            message = f"{len(contract['conflicts'])} interface signals exceed the requested pitch"
            receipt["candidates"] = {
                "interfaces": {
                    "status": "failed",
                    "error": message,
                    "edges": contract["edges"],
                }
            }
            journal.save(receipt)
            journal.decide(receipt["id"], "interfaces", "rejected", message)
            raise ValueError(message)
        ids = list(contract["updates"])
        if not ids:
            raise ValueError("the proposed contact contract is already current")
    receipt["affected_homes"] = ids
    receipt["home"] = ids[0]
    journal.snapshot(revision(state), start_score, state)
    labels = ["control", "top8", "top32"] if kind == "macro" else ["control", kind]
    receipt["candidates"] = {label: {"status": "queued"} for label in labels}
    journal.save(receipt)
    for label in labels:
        item = receipt["candidates"][label]
        item["status"] = "running"
        journal.save(receipt)
        fork = copy.copy(work)
        fork.root = Path(tempfile.mkdtemp(dir=work.root, prefix="search-"))
        fork.path = fork.root / "state.json"
        item["directory"] = str(fork.root)
        journal.save(receipt)
        branch = copy.deepcopy(base)
        if contract and label != "control":
            chunk_interfaces.install(branch, contract)
        fork.save(branch)
        try:
            options = {}
            for h in ids:
                rows = base["results"].get(h, {})
                source = rows.get(phase, {})
                if source.get("status") != "done":
                    source = rows.get("macro", {})
                if source.get("status") != "done" or not source.get("handoff"):
                    item.update(
                        status="failed",
                        error=f"{h} needs a current macro or final handoff",
                    )
                    journal.save(receipt)
                    raise ValueError(item["error"])
                opts = {
                    "seed": {
                        "path": source["handoff"],
                        "checksum": source["handoff_checksum"],
                    },
                    "freeze_macros": phase == "final"
                    and rows.get("macro", {}).get("status") == "done",
                }
                if label.startswith("top"):
                    feedback = json.loads(Path(source["report"]).read_text())[0]
                    if "macro_suggestions" not in feedback:
                        raise ValueError(
                            "refresh macro placement to generate connectivity suggestions"
                        )
                    holds = base.get("holds", {}).get(h, {})
                    poses = [
                        [n, *v, v[0] is not None, v[1] is not None]
                        for n, v in holds.items()
                    ]
                    for m in feedback["macro_suggestions"][: int(label[3:])]:
                        if m["name"] not in holds:
                            poses.append([m["name"], m["x"], m["y"], False, False])
                    opts["poses"] = poses
                if label == "density":
                    opts["density"] = density
                options[h] = opts
            chunk_dispatch.wave(
                fork,
                phase,
                ids,
                workers,
                home_options=options,
                iterations=iterations,
                live_frames=True,
                admit=admit,
            )
            if phase == "macro":
                chunk_dispatch.wave(
                    fork,
                    "final",
                    ids,
                    workers,
                    iterations=final_iterations,
                    live_frames=True,
                    admit=admit,
                )
            saved = absolute_state(fork, fork.state())
            fork.save(saved)
            finish_candidate(work, receipt, item, saved, fork.path)
        except (ValueError, OSError, KeyError) as exc:
            item.update(status="failed", error=str(exc))
        journal.save(receipt)
        if label == "control" and item["status"] == "failed":
            raise ValueError(item["error"])
    if kind != "interfaces":
        combine(work, base, receipt)
    control = receipt["candidates"]["control"]["assembled"]
    done = [
        (objective_cost(row["assembled"], objective), label, row)
        for label, row in receipt["candidates"].items()
        if row["status"] == "done" and row["assembled"]["hpwl_um"] is not None
    ]
    if not done:
        raise ValueError("no complete assembled HPWL available")
    score, label, best = min(done, key=lambda v: v[0])
    improves_control = score < objective_cost(control, objective)
    improves_parent = start_score["coverage"] != best["assembled"][
        "coverage"
    ] or score < objective_cost(start_score, objective)
    receipt["winner"] = label
    receipt["kept"] = False
    if keep and improves_control and improves_parent:
        after = json.loads(Path(best["state_path"]).read_text())
        publish_trial(
            work,
            journal,
            receipt["id"],
            label,
            receipt["checkpoint"],
            receipt["epoch"],
            receipt["engine"],
            after,
        )
        receipt["kept"] = True
    journal.decide(
        receipt["id"],
        label,
        "kept" if receipt["kept"] else "finding",
        f"Best {objective} candidate {label} compared with the matched control. Assembled changes {best['delta_assembled']}",
    )
    receipt["finished"] = time.time()
    journal.save(receipt)
    return receipt
