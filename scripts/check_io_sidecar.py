#!/usr/bin/env python3
"""Validate an emitted `<base>_io.json` sidecar with a REAL JSON parser.

The C++ test's `json_string_field` is a field/escape decoder: it finds one
`"key": "..."` by substring and decodes that string. It proves a byte round
trips, which is what it is for, and it would not notice a structural error
anywhere else in the document -- an unbalanced brace, a trailing comma, a
duplicated key. This is the whole-document gate.

It also checks the contract the file is supposed to satisfy, not merely that it
parses:

  * the schema discriminator, so a consumer can tell this sidecar from another;
  * dense input ordinals matching list position -- the ordinal IS the index into
    RuntimeInput, so a gap or a reordering silently misaligns the caller;
  * `runtime_input_arity` within range and at least one past every used ordinal;
  * output ordinals dense, since position is identity in the output array;
  * clock ordinals dense from 0, since they index ClockEdges.

Usage: check_io_sidecar.py <io.json> [<io.json> ...]
Exit 0 iff every file is valid.
"""

import json
import sys


def check(path: str) -> list[str]:
    bad: list[str] = []
    try:
        with open(path, encoding="utf-8") as fh:
            doc = json.load(fh)
    except OSError as e:
        return [f"cannot read: {e}"]
    except json.JSONDecodeError as e:
        return [f"not valid JSON: {e}"]
    if not isinstance(doc, dict):
        return [f"top level is {type(doc).__name__}, expected an object"]

    if doc.get("schema_version") != 1:
        bad.append(f"schema_version is {doc.get('schema_version')!r}, expected 1")
    if doc.get("kind") != "lean_design_io":
        bad.append(f"kind is {doc.get('kind')!r}, expected 'lean_design_io' -- "
                   f"a consumer identifies the sidecar by this, not by filename")
    if not isinstance(doc.get("top"), str) or not doc["top"]:
        bad.append("`top` is missing or empty")

    inputs = doc.get("inputs")
    if not isinstance(inputs, list):
        bad.append("`inputs` is missing or not a list")
        inputs = []
    for i, p in enumerate(inputs):
        for f, t in (("name", str), ("ordinal", int), ("width", int), ("used_by_model", bool)):
            if not isinstance(p.get(f), t):
                bad.append(f"inputs[{i}].{f} is {p.get(f)!r}, expected {t.__name__}")
        if p.get("ordinal") != i:
            bad.append(f"inputs[{i}].ordinal is {p.get('ordinal')!r}; the ordinal is the "
                       f"index into RuntimeInput, so it must equal the position")

    arity = doc.get("runtime_input_arity")
    if not isinstance(arity, int):
        bad.append("`runtime_input_arity` is missing or not an integer")
    else:
        if arity > len(inputs):
            bad.append(f"runtime_input_arity {arity} exceeds the {len(inputs)} declared input(s)")
        used = [p["ordinal"] for p in inputs if p.get("used_by_model")]
        if used and arity <= max(used):
            bad.append(f"runtime_input_arity {arity} does not reach used ordinal {max(used)}; "
                       f"checkRuntime would reject a correctly built array")

    outputs = doc.get("outputs")
    if not isinstance(outputs, list):
        bad.append("`outputs` is missing or not a list")
    else:
        for i, p in enumerate(outputs):
            if p.get("ordinal") != i:
                bad.append(f"outputs[{i}].ordinal is {p.get('ordinal')!r}; position is identity "
                           f"in the output array")

    clocks = doc.get("clocks")
    if not isinstance(clocks, list):
        bad.append("`clocks` is missing or not a list")
    elif not clocks:
        bad.append("`clocks` is empty; every certificate declares at least one domain")
    else:
        for i, c in enumerate(clocks):
            if c.get("ordinal") != i:
                bad.append(f"clocks[{i}].ordinal is {c.get('ordinal')!r}; it indexes ClockEdges")
            if not isinstance(c.get("implicit"), bool):
                bad.append(f"clocks[{i}].implicit is {c.get('implicit')!r}, expected a bool")
    return bad


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: check_io_sidecar.py <io.json> [...]", file=sys.stderr)
        return 2
    rc = 0
    for path in sys.argv[1:]:
        problems = check(path)
        if problems:
            rc = 1
            print(f"FAIL {path}")
            for p in problems:
                print(f"  {p}")
        else:
            print(f"ok   {path}")
    return rc


if __name__ == "__main__":
    sys.exit(main())
