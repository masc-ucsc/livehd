#!/usr/bin/env python3
"""Check a configured channel IR budget against matched OpenROAD voltage CSVs.

This checks recorded IR evidence, not connectivity or DRC. The analysis contexts
in the manifest must describe the actual runs; the caller owns their provenance.
"""

import argparse
import csv
from decimal import Decimal, InvalidOperation
import hashlib
import json
from pathlib import Path
import re


class OutputConflict(Exception):
    """An output path aliases input evidence and must never be written."""


def aliases(left, right):
    return left.resolve() == right.resolve() or (
        left.exists() and right.exists() and left.samefile(right))


def finite(value):
    try:
        number = Decimal(str(value))
    except InvalidOperation as error:
        raise ValueError(f"invalid numeric value: {value}") from error
    if not number.is_finite():
        raise ValueError(f"nonfinite numeric value: {value}")
    return number


def voltages(path, rail):
    result = {}
    columns = ["Instance", "Terminal", "Layer", "X location", "Y location", "Voltage"]
    allowed = {"VDD": {"VDD", "VDDPE", "VDDCE"}, "VSS": {"VSS", "VSSE"}}[rail]
    with Path(path).open(newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != columns:
            raise ValueError(f"unexpected OpenROAD voltage CSV header: {path}")
        for row in reader:
            if None in row or any(not row.get(key) for key in columns):
                raise ValueError(f"incomplete voltage row: {path}")
            if row["Terminal"] not in allowed:
                raise ValueError(f"wrong supply terminal in {rail} CSV: {path}")
            key = (row["Instance"], row["Terminal"], row["Layer"],
                   finite(row["X location"]), finite(row["Y location"]))
            if key in result:
                raise ValueError(f"duplicate voltage terminal: {key}")
            result[key] = finite(row["Voltage"])
    if not result:
        raise ValueError(f"empty voltage CSV: {path}")
    return result


def compare_rail(reference, candidate, rail, nominal, limit):
    if reference.keys() != candidate.keys():
        raise ValueError(f"{rail} terminal identities or placement differ")
    if not reference:
        raise ValueError(f"empty {rail} comparison")
    # VDD degradation lowers voltage; VSS degradation raises it.
    sign = Decimal(1) if rail == "VDD" else Decimal(-1)
    ref_drop = max(sign * (nominal - value) for value in reference.values())
    new_drop = max(sign * (nominal - value) for value in candidate.values())
    terminal_delta = max(sign * (reference[key] - candidate[key]) for key in reference)
    extra = max(Decimal(0), terminal_delta, new_drop - ref_drop)
    return {"terminals": len(reference), "reference_worst_drop_v": str(ref_drop),
            "candidate_worst_drop_v": str(new_drop),
            "worst_drop_increase_v": str(max(Decimal(0), new_drop - ref_drop)),
            "max_terminal_drop_increase_v": str(max(Decimal(0), terminal_delta)),
            "within_limit": extra <= limit}


def check(manifest, directory):
    directory = Path(directory)
    hashes = {}

    def artifact(name):
        path = (directory / name).resolve()
        with path.open("rb") as stream:
            hashes[str(path)] = hashlib.file_digest(stream, "sha256").hexdigest()
        return path

    arms = [manifest[key] for key in ("reference", "candidate")]
    contexts = [arm["analysis"] for arm in arms]
    required = {"liberty_sha256", "sdc_sha256", "corner", "source_model", "power_model", "voltages_v"}
    for context in contexts:
        if not required.issubset(context) or any(not context[key] for key in required):
            raise ValueError("incomplete recorded analysis context")
        for key in ("liberty_sha256", "sdc_sha256"):
            if not isinstance(context[key], str) or not re.fullmatch(r"[0-9a-f]{64}", context[key]):
                raise ValueError(f"invalid analysis fingerprint: {key}")
        if set(context["voltages_v"]) != {"VDD", "VSS"}:
            raise ValueError("both ASAP7 supply voltages must be recorded")
        for value in context["voltages_v"].values():
            finite(value)
    if contexts[0] != contexts[1]:
        raise ValueError("reference and candidate analysis contexts differ")
    reports = [json.loads(artifact(arm["power_grid_report"]).read_text()) for arm in arms]
    if any(report.get("profile") != "pg_asap7" for report in reports):
        raise ValueError("IR channel validator currently supports the ASAP7 profile")
    if not reports[0].get("reference_revision") or reports[0]["reference_revision"] != reports[1].get("reference_revision"):
        raise ValueError("power-grid reference revisions differ or are missing")
    if reports[0].get("channels") != [] or not reports[1].get("channels"):
        raise ValueError("comparison requires channels off versus channels on")
    limit = finite(reports[1].get("channel_max_ir_drop_v"))
    if limit < 0:
        raise ValueError("channel IR-drop limit must be nonnegative")
    rails = {}
    for rail in ("VDD", "VSS"):
        values = [voltages(artifact(arm["rails"][rail]), rail) for arm in arms]
        rails[rail] = compare_rail(*values, rail, finite(contexts[0]["voltages_v"][rail]), limit)
    for name, expected in hashes.items():
        with Path(name).open("rb") as stream:
            if hashlib.file_digest(stream, "sha256").hexdigest() != expected:
                raise ValueError(f"input changed during IR validation: {name}")
    return {"channel_ir_delta_within_limit": all(value["within_limit"] for value in rails.values()),
            "channel_max_ir_drop_v": str(limit), "rails": rails, "sha256": hashes,
            "analysis": contexts[0], "scope": "Matched recorded IR evidence only; connectivity, DRC and routing are separate gates"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if aliases(args.output, args.manifest):
        raise OutputConflict("output must not replace the manifest")
    manifest_hash = None
    try:
        data = args.manifest.read_bytes()
        manifest_hash = hashlib.sha256(data).hexdigest()
        manifest = json.loads(data)
        inputs = []
        for arm in (manifest["reference"], manifest["candidate"]):
            inputs.extend(args.manifest.parent / name for name in [arm["power_grid_report"], *arm["rails"].values()])
        if any(aliases(args.output, path) for path in inputs):
            raise OutputConflict("output must not replace an input artifact")
        result = check(manifest, args.manifest.parent)
    except (ValueError, KeyError, TypeError, OSError) as error:
        result = {"channel_ir_delta_within_limit": False, "error": str(error)}
    result["manifest_sha256"] = manifest_hash
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    return 0 if result["channel_ir_delta_within_limit"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
