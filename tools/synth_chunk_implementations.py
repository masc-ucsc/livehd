#!/usr/bin/env python3
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""List or synthesize physical chunk implementations from a joined LiveHD metadata file."""

import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def jobs(records, design):
    plans = [
        r
        for r in records
        if r.get("class") == "livehd.placement.chunk_plan" and r.get("design") == design
    ]
    if len(plans) != 1:
        raise ValueError("expected one saved chunk plan for " + design)
    copies = defaultdict(list)
    for c in plans[0]["chunks"]:
        copies[c["partition"], c["implementation"]].append(c["id"])
    bindings = {}
    for r in records:
        if r.get("class") == "livehd.synth.chunk_source" and r.get("design") == design:
            key = r["partition"], r.get("implementation")
            if key in bindings:
                raise ValueError("duplicate chunk source: " + str(key))
            if (
                not r.get("top")
                or not isinstance(r.get("sources"), list)
                or not r["sources"]
            ):
                raise ValueError("chunk source needs top and sources")
            bindings[key] = r
    return [
        (p, i, sorted(ids), bindings.get((p, i), bindings.get((p, None))))
        for (p, i), ids in sorted(copies.items())
    ]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("metadata", type=Path)
    ap.add_argument("--design", required=True)
    ap.add_argument("--partition")
    ap.add_argument("--implementation")
    ap.add_argument(
        "--run",
        action="store_true",
        help="execute lhd synth for the selected groups; otherwise list them",
    )
    ap.add_argument("--lhd", default="lhd")
    ap.add_argument("--workdir", type=Path, default=Path("chunk-synthesis"))
    a = ap.parse_args()
    bundle = json.loads(a.metadata.read_text())
    tasks = [
        j
        for j in jobs(bundle, a.design)
        if (not a.partition or j[0] == a.partition)
        and (not a.implementation or j[1] == a.implementation)
    ]
    if not tasks:
        ap.error("selection has no implementations")
    output = []
    for partition, impl, copies, binding in tasks:
        # Labels may contain hierarchy punctuation. Use a stable filesystem-safe
        # workdir scoped to this design, partition and *physical implementation*.
        key = hashlib.sha256(
            json.dumps([a.design, partition, impl]).encode()
        ).hexdigest()
        folder = (a.workdir / key).resolve()
        record = dict(
            partition=partition,
            implementation=impl,
            instances=copies,
            workdir=str(folder),
        )
        output.append(record)
        if binding:
            src = [
                (a.metadata.resolve().parent / f).resolve() for f in binding["sources"]
            ]
            command = [
                a.lhd,
                "synth",
                *map(str, src),
                "--top",
                binding["top"],
                "--workdir",
                str(folder),
            ]
            for k, v in sorted(binding.get("settings", {}).items()):
                command.extend(["--set", k + "=" + str(v)])
            record["argv"] = command
            if a.run and not all(f.is_file() for f in src):
                ap.error("missing RTL for " + partition + "/" + impl)
        elif a.run:
            ap.error("no livehd.synth.chunk_source for " + partition + "/" + impl)
    # Validate every selected binding before launching the first job. Run one at
    # a time; each workdir can be edited, inspected or rerun independently.
    if a.run:
        for record in output:
            subprocess.run(record["argv"], check=True, stdout=sys.stderr)
    print(
        json.dumps(
            [
                {
                    "class": "livehd.synth.chunk_jobs",
                    "id": "implementations",
                    "design": a.design,
                    "jobs": output,
                }
            ],
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
