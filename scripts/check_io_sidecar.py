#!/usr/bin/env python3
"""Validate an emitted `<base>_io.json` sidecar with a real JSON parser.

The C++ test's `json_string_field` is a field/escape decoder: it finds one
`"key": "..."` by substring and decodes that string. It proves a byte round
trips and would not notice a structural error anywhere else. This is the
whole-document gate.

ADMISSION IS THE POINT, so it is written to REJECT rather than to pass. The
first version accepted a negative `runtime_input_arity`, negative widths, a
boolean where an ordinal belongs, outputs with no name or width, clocks with no
name, and a document with a duplicated `top` key -- and crashed with an
AttributeError on a port that was not an object. A validator that only notices
the problems its author thought of is a spell-checker.

Two traps worth naming:

  * `isinstance(True, int)` is True in Python. A JSON boolean is NOT an integer
    for this schema, so integers are checked with `type(x) is int`.
  * `json.load` keeps the LAST of duplicated keys silently, so a document can
    say two different things and parse clean. `object_pairs_hook` rejects that.

WHAT `runtime_input_arity` MEANS HERE. `Direct.checkRuntime` compares
`i.size < inputArity D`, so the runtime rule is a LOWER BOUND: an input array
longer than the arity is accepted. (Only the edge vector is exact.) The metadata
helper, though, emits the CANONICAL value -- one past the highest ordinal any
source references -- so this validates that exact number rather than merely a
sufficient one. A sidecar advertising a larger arity would not be rejected by
Lean; it would just make callers build longer arrays than necessary, and it
would not be what this emitter produced.

Usage: check_io_sidecar.py <io.json> [<io.json> ...]
Exit 0 iff every file is valid.
"""

import json
import sys


def _no_duplicate_keys(pairs):
    seen = {}
    for k, v in pairs:
        if k in seen:
            raise ValueError(f"duplicate key {k!r}: the document says two different things")
        seen[k] = v
    return seen


def _is_int(x) -> bool:
    # A JSON bool is not an integer for this schema, and isinstance(True, int)
    # would say otherwise.
    return type(x) is int


def _field(obj, where: str, name: str, kind: str, bad: list) -> object:
    """Fetch `name` from `obj`, checking presence and type. None on failure."""
    if name not in obj:
        bad.append(f"{where}: missing `{name}`")
        return None
    v = obj[name]
    ok = {"str": lambda x: isinstance(x, str),
          "int": _is_int,
          "bool": lambda x: isinstance(x, bool)}[kind](v)
    if not ok:
        bad.append(f"{where}.{name} is {v!r} ({type(v).__name__}), expected {kind}")
        return None
    return v


def _check_port_list(doc, key, where_kind, bad) -> list:
    raw = doc.get(key)
    if not isinstance(raw, list):
        bad.append(f"`{key}` is {type(raw).__name__ if key in doc else 'missing'}, expected a list")
        return []
    ports = []
    for i, p in enumerate(raw):
        where = f"{key}[{i}]"
        if not isinstance(p, dict):
            bad.append(f"{where} is {type(p).__name__}, expected an object")
            continue
        name = _field(p, where, "name", "str", bad)
        ordinal = _field(p, where, "ordinal", "int", bad)
        width = _field(p, where, "width", "int", bad)
        used = _field(p, where, "used_by_model", "bool", bad) if where_kind == "input" else True
        if name is not None and not name:
            bad.append(f"{where}.name is empty")
        if width is not None and width <= 0:
            bad.append(f"{where}.width is {width}; a bit-vector width must be positive")
        if ordinal is not None:
            if ordinal < 0:
                bad.append(f"{where}.ordinal is {ordinal}; ordinals index an array")
            elif ordinal != i:
                bad.append(f"{where}.ordinal is {ordinal}; the ordinal IS the array index, "
                           f"so it must equal the position {i}")
        ports.append({"ordinal": ordinal, "used": used})
    return ports


def check(path: str) -> list:
    bad: list = []
    try:
        with open(path, encoding="utf-8") as fh:
            doc = json.load(fh, object_pairs_hook=_no_duplicate_keys)
    except OSError as e:
        return [f"cannot read: {e}"]
    except (json.JSONDecodeError, ValueError) as e:
        return [f"malformed document: {e}"]
    if not isinstance(doc, dict):
        return [f"top level is {type(doc).__name__}, expected an object"]

    if doc.get("schema_version") != 1 or not _is_int(doc.get("schema_version")):
        bad.append(f"schema_version is {doc.get('schema_version')!r}, expected the integer 1")
    if doc.get("kind") != "lean_design_io":
        bad.append(f"kind is {doc.get('kind')!r}, expected 'lean_design_io' -- a consumer "
                   f"identifies the sidecar by this, not by filename")
    top = _field(doc, "<doc>", "top", "str", bad)
    if top is not None and not top:
        bad.append("`top` is empty")

    inputs = _check_port_list(doc, "inputs", "input", bad)
    _check_port_list(doc, "outputs", "output", bad)

    arity = _field(doc, "<doc>", "runtime_input_arity", "int", bad)
    if arity is not None:
        if arity < 0:
            bad.append(f"runtime_input_arity is {arity}; it is a length")
        elif arity > len(inputs):
            bad.append(f"runtime_input_arity {arity} exceeds the {len(inputs)} declared input(s)")
        # The CANONICAL value: one past the highest referenced ordinal, 0 when
        # nothing is referenced. Checking only "reaches the used ordinals" would
        # admit any larger number, which this emitter never produces.
        used = [p["ordinal"] for p in inputs if p["used"] and p["ordinal"] is not None]
        want = max(used) + 1 if used else 0
        if arity != want:
            bad.append(f"runtime_input_arity is {arity}, expected {want} "
                       f"(one past the highest used ordinal{'' if used else ', none used'})")

    clocks = doc.get("clocks")
    if not isinstance(clocks, list):
        bad.append(f"`clocks` is {type(clocks).__name__ if 'clocks' in doc else 'missing'}, expected a list")
    elif not clocks:
        bad.append("`clocks` is empty; every certificate declares at least one domain")
    else:
        for i, c in enumerate(clocks):
            where = f"clocks[{i}]"
            if not isinstance(c, dict):
                bad.append(f"{where} is {type(c).__name__}, expected an object")
                continue
            name = _field(c, where, "name", "str", bad)
            if name is not None and not name:
                bad.append(f"{where}.name is empty")
            _field(c, where, "implicit", "bool", bad)
            ordinal = _field(c, where, "ordinal", "int", bad)
            if ordinal is not None and ordinal != i:
                bad.append(f"{where}.ordinal is {ordinal}; it indexes ClockEdges, so it must "
                           f"equal the position {i}")
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
