#!/usr/bin/env python3
"""Compatibility entry point for an optional, unversioned local benchmark report.

Benchmark-specific rendering and results live under repros/, outside the source
repository. This launcher never writes generated results into docs/.
"""
from pathlib import Path
import runpy


def main():
    reporter = Path(__file__).resolve().parents[1] / "repros/placement-results/render_partial_route.py"
    if not reporter.is_file():
        raise SystemExit("No local benchmark reporter; keep evaluation artifacts under repros/.")
    runpy.run_path(str(reporter), run_name="__main__")


if __name__ == "__main__":
    main()
