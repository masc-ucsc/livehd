#!/usr/bin/env python3
"""Power-on soundness of lgcheck's induction and bounded stages.

equiv_simple and equiv_induct have no base case: they prove that every $equiv
holds once the state they relate has been equal, never that it starts equal.
A stage that uses them (lgcheck 1, 1n, 1r, 1p, 1m, 1c, 4) is therefore only a
proof when the power-on state agrees, per bit, under one rule: a gold `init`
bit that is x (an absent init is all x) is a don't-care the gate's power-on
value refines, as in the bounded miter's -ignore_gold_x; a defined gold bit
must equal the gate's.

  power_on.py check PAIRS.json
      PAIRS.json is the stage's equiv module as its pairing left it, dumped
      with only the init-bearing nets, their aliases, the $equiv cells and the
      memories. Exit 1 when a paired state bit (same-name, renamed or
      structurally merged) breaks the rule; 3 when every pair keeps it but
      some state with a defined power-on value is on no pair, so its first
      cycles are covered by no proof and the base case must decide; 0 when the
      stage's proof stands as is.

  power_on.py bmc-init MODULES.json
      MODULES.json holds gold and gate before the bounded miter. Print gate
      output names when defined gate initialization refines unspecified gold
      initialization; those outputs must be defined in a bounded counterexample.

  power_on.py inject BASE.il OUT.il
      BASE.il holds `lgcheck_base` (equiv_miter of the equiv module the stage
      proved, which drops every wire attribute) and the init-bearing wires of
      `equiv`. OUT.il
      is `lgcheck_base` with each wire's power-on value restored, ready for a
      bounded `sat` from power-on.
"""

from __future__ import annotations

import json
import sys


# `check`'s "the base case decides" exit code; 2 is python's own usage error.
BASE_CASE = 3


def bit_compatible(gold: str, gate: str) -> bool:
    """One state bit: a gold x is a don't-care, a defined gold bit must match."""
    return gold == "x" or gold == gate


def compatible(gold: str | None, gate: str | None) -> bool:
    """Two whole init strings (MSB first; None = no init, all x)."""
    if gold is None or set(gold) <= {"x"}:
        return True
    if gate is None or len(gold) != len(gate):
        return False
    return all(bit_compatible(g, t) for g, t in zip(gold, gate))


def bmc_init(path: str) -> int:
    """Preserve a defined implementation's refinement of unspecified gold init.

    Print gate output names when zero-filling the gold's unspecified state
    would manufacture a different power-on state. BMC then keeps that state
    undefined and searches only for differences with defined gate outputs;
    an X-pessimistic gate expression is not a concrete counterexample.
    """
    try:
        with open(path) as f:
            modules = json.load(f)["modules"]
        gold, gate = modules["gold"], modules["gate"]
    except (OSError, ValueError, KeyError):
        return 1

    def defined_init(module):
        values = [
            net.get("attributes", {}).get("init", "")
            for net in module.get("netnames", {}).values()
        ]
        values += [
            cell.get("parameters", {}).get("INIT", "")
            for cell in module.get("cells", {}).values()
            if str(cell.get("type", "")).startswith("$mem")
        ]
        # Before memory_collect, initialization lives in $meminit DATA/EN
        # connections rather than a $mem_v2 INIT parameter.
        for cell in module.get("cells", {}).values():
            if cell.get("type") not in ("$meminit", "$meminit_v2"):
                continue
            connections = cell.get("connections", {})
            enable = connections.get("EN", ["1"])
            if enable:
                values.append("".join(
                    bit for index, bit in enumerate(connections.get("DATA", []))
                    if bit in ("0", "1") and enable[index % len(enable)] == "1"
                ))
        return any(c in "01" for value in values for c in str(value))

    if not defined_init(gold) and defined_init(gate):
        for name, port in gate.get("ports", {}).items():
            if port.get("direction") == "output":
                print("\\gate_" + name)
    return 0


def check(path: str) -> int:
    try:
        with open(path) as f:
            modules = json.load(f)["modules"]
    except (OSError, ValueError, KeyError):
        return 1  # no dump = no proof
    module = modules.get("equiv", {})  # absent: nothing was selected
    init = {"gold": {}, "gate": {}}  # bit -> power-on value, per side
    named = {"gold": set(), "gate": set()}  # bits some net of that side carries
    for name, net in module.get("netnames", {}).items():
        side = name.rsplit("_", 1)[-1]
        if side not in init:
            continue
        named[side].update(bit for bit in net["bits"] if isinstance(bit, int))
        value = net.get("attributes", {}).get("init")
        if value is None:
            continue
        value = str(value).lower()  # MSB first, while `bits` is LSB first
        for i, bit in enumerate(net["bits"][: len(value)]):
            if isinstance(bit, int):
                init[side][bit] = value[len(value) - 1 - i]
    pairs = {(bit, bit) for bit in named["gold"] & named["gate"]}  # one cell for both sides (equiv_struct)
    memory_init = False
    for cell in module.get("cells", {}).values():
        if cell.get("type") == "$equiv":
            pairs.update(zip(cell["connections"]["A"], cell["connections"]["B"]))
        elif str(cell.get("type", "")).startswith("$mem"):
            memory_init |= any(c in "01" for c in str(cell.get("parameters", {}).get("INIT", "")))
    for gold, gate in pairs:
        if not bit_compatible(init["gold"].get(gold, "x"), init["gate"].get(gate, "x")):
            print(f"INFO: {path}: a paired state starts at a different power-on value; that proof is not accepted")
            return 1
    paired = {"gold": {a for a, _ in pairs}, "gate": {b for _, b in pairs}}
    for side in init:
        if memory_init or any(v != "x" and bit not in paired[side] for bit, v in init[side].items()):
            return BASE_CASE
    return 0


def rtlil_name(line: str) -> str:
    return line.split()[-1]


def inject(path: str, out_path: str) -> int:
    try:
        with open(path) as f:
            lines = f.read().splitlines()
    except OSError:
        return 1
    inits: dict[str, str] = {}
    module = ""
    pending: list[str] = []
    for line in lines:
        text = line.strip()
        if text.startswith("module "):
            module = rtlil_name(text)
        elif text == "end":
            module = ""
        elif module == "\\equiv" and text.startswith("attribute \\init "):
            pending.append(text.split(maxsplit=2)[2])
        elif module == "\\equiv" and text.startswith("wire "):
            if pending:
                inits[rtlil_name(text)] = pending[-1]
            pending = []
        elif not text.startswith("attribute "):
            pending = []
    out: list[str] = []
    module = ""
    has_init = False
    found = False
    for line in lines:
        text = line.strip()
        if text.startswith("module "):
            module = rtlil_name(text)
            found |= module == "\\lgcheck_base"
        if module == "\\lgcheck_base" or (not module and not text.startswith("attribute ")):
            if text.startswith("wire ") and not has_init and rtlil_name(text) in inits:
                indent = line[: len(line) - len(line.lstrip())]
                out.append(f"{indent}attribute \\init {inits[rtlil_name(text)]}")
            has_init = text.startswith("attribute \\init ") or (has_init and text.startswith("attribute "))
            out.append(line)
        if text == "end":
            module = ""
    if not found:
        return 1
    with open(out_path, "w") as f:
        f.write("\n".join(out) + "\n")
    return 0


def main(argv: list[str]) -> int:
    if len(argv) == 3 and argv[1] == "check":
        return check(argv[2])
    if len(argv) == 3 and argv[1] == "bmc-init":
        return bmc_init(argv[2])
    if len(argv) == 4 and argv[1] == "inject":
        return inject(argv[2], argv[3])
    print(__doc__, file=sys.stderr)
    return 64


if __name__ == "__main__":
    sys.exit(main(sys.argv))
