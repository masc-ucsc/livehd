#!/usr/bin/env python3
"""Compare two lean_export_graph binaries on copies of saved normalized graphs.

The manifest is a JSON list of {group, top, graph_dir}; paths are relative to
--graph-root. All graph copies, outputs, and logs stay below --output.
Exit 1 means an accepted design regressed or its certificate changed. Timeouts
and refusal pairs are reported explicitly and never counted as accepted tests.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def run(binary, graph, top, output, timeout):
    output.mkdir(parents=True, exist_ok=True)
    try:
        proc = subprocess.run([str(binary), str(graph), top, str(output)],
                              capture_output=True, text=True, timeout=timeout)
        code, log = proc.returncode, proc.stdout + proc.stderr
    except subprocess.TimeoutExpired:
        code, log = 124, f"timed out after {timeout} seconds\n"
    (output / "run.log").write_text(log)
    files = sorted(output.glob("*_Lgraph.lean"))
    # A stale output must not make a refused or timed-out invocation pass.
    text = files[0].read_bytes() if code == 0 and len(files) == 1 else None
    if code == 0 and text is None:
        code = 125
    return code, text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("before", "after", "manifest", "graph-root", "output"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--timeout", type=float, default=180)
    args = parser.parse_args()
    entries = json.loads(args.manifest.read_text())
    results = []
    for entry in entries:
        group, top = entry["group"], entry["top"]
        root = args.output.resolve() / group / top
        graph = root / "graph"
        if not graph.exists():
            shutil.copytree(args.graph_root / entry["graph_dir"], graph)
        before, old = run(args.before.resolve(), graph, top, root / "before", args.timeout)
        after, new = run(args.after.resolve(), graph, top, root / "after", args.timeout)
        row = dict(group=group, top=top, before=before, after=after,
                   identical=old is not None and old == new,
                   sha256=hashlib.sha256(old).hexdigest() if old is not None else None)
        if new is not None:
            row["sources"] = new.count(b"SourceDesc.")
            row["nodes"] = new.count(b"origin :=")
            row["flops"] = new.count(b"resetActiveLow :=")
            row["memories"] = new.count(b"nextImg :=")
        results.append(row)
        print(json.dumps(row), flush=True)
        (args.output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    return int(any(r["before"] == 0 and not r["identical"] for r in results))


if __name__ == "__main__":
    raise SystemExit(main())
