#!/usr/bin/env python3
"""Hermetic end-to-end checks for the observation-only controller replay."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        config = root / "settings.json"
        config.write_text(json.dumps({"budget": 800}))
        trace = root / "trace.jsonl"
        # Old diagnostics contain fixed-SDC timing, not the rebased window sum.
        # Those fields must not be silently repurposed for stability decisions.
        trace.write_text("".join(json.dumps({
            "iteration": at, "wns_ns": -1, "tns_us": -100,
            "hpwl_um": 10, "opaque_route_overflow": .1,
        }) + "\n" for at in range(0, 801, 10)))
        output = root / "replay.jsonl"
        subprocess.run([binary, config, trace, output], check=True)
        rows = [json.loads(line) for line in output.read_text().splitlines()]
        assert rows[-1]["finished"] and rows[-1]["observation_only"]
        assert all(row["missing_metrics"] == ["period", "window_tns"]
                   for row in rows if row["kind"] == "sample")
        assert not any(row.get("gate") in {"rudy", "repair"} and row["reason"] == "stable"
                       for row in rows if row["kind"] == "event")
        assert subprocess.run([binary, config, trace, output], capture_output=True).returncode != 0

        # A short trace must remain incomplete: do not fabricate later samples
        # or advance to the controller's budget after the observed run stopped.
        trace.write_text(json.dumps({"iteration": 0, "period_ns": 1,
                                     "window_tns_ns": -.1, "hpwl_um": 10,
                                     "opaque_route_overflow": .1}) + "\n")
        short_output = root / "short.jsonl"
        subprocess.run([binary, config, trace, short_output], check=True)
        summary = json.loads(short_output.read_text().splitlines()[-1])
        assert not summary["finished"] and summary["observed_until"] == 0

        # A trace continuing past a budget between sample points must stop at
        # that exact budget, without waiting for an observation at the deadline.
        config.write_text(json.dumps({"budget": 805}))
        trace.write_text("".join(json.dumps({"iteration": at}) + "\n"
                                 for at in range(0, 811, 10)))
        bounded_output = root / "bounded.jsonl"
        subprocess.run([binary, config, trace, bounded_output], check=True)
        bounded = [json.loads(line) for line in bounded_output.read_text().splitlines()]
        assert bounded[-1]["finished"] and bounded[-1]["controller_until"] == 805
        assert bounded[-1]["observed_until"] == 800
        assert any(row.get("gate") == "finish" and row["iteration"] == 805
                   and row["incomplete"] for row in bounded if row["kind"] == "event")

        # Typos in settings cannot silently fall back to defaults.
        config.write_text(json.dumps({"budegt": 800}))
        assert subprocess.run([binary, config, trace, root / "bad.jsonl"], capture_output=True).returncode != 0


if __name__ == "__main__":
    main()
