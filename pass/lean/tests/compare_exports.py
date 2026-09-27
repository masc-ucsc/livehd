#!/usr/bin/env python3
"""Export saved graphs; record hashes, refusal diagnostics and semantic summaries.

Manifest: [{"group": "tiny", "top": "tiny_sum", "graph_dir": "/.../lgdb"}].
Copies graph databases into the project-local output directory before opening
them because the graph library may write metadata even during a read/export.
"""
import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess


def summarize(text):
    nodes = []
    for nid, op, width, deps in re.findall(
        r"\{ nid := (\d+), op := (.*?), width := (\d+), deps := \[([^\]]*)\] \}", text
    ):
        nodes.append([int(nid), op, int(width), [int(d) for d in deps.split(",") if d.strip()]])
    topo = re.search(r"topo := \[([^\]]*)\], sources := \[([^\]]*)\]", text)
    return {
        "nodes": nodes,
        "topo": [int(d) for d in topo[1].split(",") if d.strip()] if topo else [],
        "sources": [int(d) for d in topo[2].split(",") if d.strip()] if topo else [],
        "state_fields": re.findall(r"^  (st_\w+) : (.*)$", text, re.M),
        "sorry": "sorry" in text,
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("exporter", type=Path)
    p.add_argument("manifest", type=Path)
    p.add_argument("output", type=Path)
    p.add_argument("--mode", default="legacy")
    p.add_argument("--timeout", type=int, default=15)
    p.add_argument("--label", action="append", default=[])
    args = p.parse_args()
    entries = json.loads(args.manifest.read_text())
    args.output.mkdir(parents=True, exist_ok=True)

    def run(entry):
        work = args.output / entry["group"] / entry["top"]
        work.mkdir(parents=True, exist_ok=True)
        graph = work / "graph"
        if not graph.exists():
            shutil.copytree(entry["graph_dir"], graph)
        out = work / "export"
        try:
            proc = subprocess.run([str(args.exporter.resolve()), str(graph.resolve()), entry["top"],
                                   str(out.resolve()), args.mode, *args.label],
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=args.timeout)
            status, log = proc.returncode, proc.stdout.decode(errors="replace")
        except subprocess.TimeoutExpired:
            status, log = "timeout", "export exceeded time limit"
        (work / "export.log").write_text(log)
        files = list(out.glob("*.lean"))
        result = {"group": entry["group"], "top": entry["top"], "status": status}
        if status == 0 and len(files) == 1:
            text = files[0].read_text()
            result["sha256"] = hashlib.sha256(files[0].read_bytes()).hexdigest()
            summary = summarize(text)
            (work / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
            result["semantic_sha256"] = hashlib.sha256(json.dumps(summary, sort_keys=True).encode()).hexdigest()
            result["nodes"] = len(summary["nodes"])
            result["sources"] = len(summary["sources"])
        else:
            result["diagnostic"] = log[-3000:]
        return result

    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        results = list(pool.map(run, entries))
    (args.output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    for group in sorted({r["group"] for r in results}):
        rows = [r for r in results if r["group"] == group]
        print(group, "accepted", sum("sha256" in r for r in rows), "total", len(rows))


if __name__ == "__main__":
    main()
