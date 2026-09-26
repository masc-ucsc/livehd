#!/usr/bin/env python3
"""Decide whether lgcheck's clock-blind engines may prove a gold/gate pair.

equiv_simple/equiv_induct/`sat -tempinduct` treat every flip-flop as one
transition per solver step, whatever clock and edge drives it. That abstraction
is exact only when every state element of BOTH designs is clocked by the same
edge of the same primary input: posedge-vs-negedge registers, registers moved
to another clock, a gated clock, a mixed-edge pipeline or a latch are all
indistinguishable from their single-clock twins under it, so a clock-blind
engine PROVES real clock/edge differences.

Input is the Yosys JSON of the flattened `gold` and `gate` modules, reduced to
their state cells plus the buffer/inverter cells that can carry a clock. Each
clocked state element gets a signature: its clock bit traced back through
buffers and inverters to a top input bit, with the edge polarity folded through
every inversion (so a negedge RTL register and a posedge netlist flop on an
inverted clock agree). Anything else -- a derived/gated clock, an undriven or
constant clock, a latch enable, an asynchronous-write memory -- is a signature
of its own that no clock-blind engine can model.

Prints `single <description>` when the union of signatures over BOTH designs
has at most one element and it is a primary-input clock edge (or Yosys'
global-clock $ff), which is when the clock-blind engines are sound. Otherwise
prints `multi <description>`: only the clk2fflogic-aware engines may decide.
The verdict word carries `+async` when either design has a register with an
asynchronous set/reset/load (`single+async ...`).
"""

import argparse
import json
import pathlib
import sys

# Cells that may carry a clock unchanged (A -> Y) or inverted.
BUF_CELLS = {"$buf", "$pos", "$_BUF_"}
NOT_CELLS = {"$not", "$_NOT_"}

# Coarse edge-triggered registers: CLK + CLK_POLARITY.
COARSE_FF = {
    "$dff", "$dffe", "$adff", "$adffe", "$sdff", "$sdffe", "$sdffce",
    "$dffsr", "$dffsre", "$aldff", "$aldffe",
}
COARSE_LATCH = {"$dlatch", "$adlatch", "$dlatchsr"}
# Fine-grained edge-triggered registers, the clock polarity is the first
# letter after the type: $_DFF_P_, $_DFFE_NP_, $_SDFFCE_PN0P_, ...
FINE_FF_PREFIX = ("$_DFF_", "$_DFFE_", "$_DFFSR_", "$_DFFSRE_", "$_SDFF_",
                  "$_SDFFE_", "$_SDFFCE_", "$_ALDFF_", "$_ALDFFE_")
FINE_LATCH_PREFIX = ("$_DLATCH_", "$_DLATCHSR_")
# Registers with an asynchronous set/reset/load. The SAT-based engines refuse
# these cells (sound), but stage 4's `techmap -map +/adff2dff.v` turns an
# asynchronous reset into a SYNCHRONOUS one, which would prove an async-reset
# register equal to its sync-reset twin.
COARSE_ASYNC = {"$adff", "$adffe", "$aldff", "$aldffe", "$dffsr", "$dffsre"}


def is_async_ff(ctype: str) -> bool:
    if ctype in COARSE_ASYNC or ctype.startswith(("$_DFFSR", "$_ALDFF")):
        return True
    parts = ctype.split("_")
    if len(parts) < 3:
        return False
    # $_DFF_PN0_ / $_DFFE_PN0P_: the reset polarity/value letters follow the
    # clock (and enable) polarity; $_DFF_P_ / $_DFFE_PP_ have none.
    return (ctype.startswith("$_DFF_") and len(parts[2]) == 3) or (
        ctype.startswith("$_DFFE_") and len(parts[2]) == 4
    )


def param_int(cell: dict, name: str, default: int = 0) -> int:
    value = cell.get("parameters", {}).get(name)
    if value is None:
        return default
    if isinstance(value, int):
        return value
    try:
        return int(str(value), 2)
    except ValueError:
        return default


def param_bits(cell: dict, name: str, width: int) -> list[int]:
    """LSB-first bits of a (packed) parameter."""
    value = param_int(cell, name)
    return [(value >> i) & 1 for i in range(width)]


class Module:
    def __init__(self, name: str, module: dict):
        self.name = name
        self.inputs: dict[int, tuple[str, int]] = {}
        for port, info in module.get("ports", {}).items():
            if info.get("direction") != "input":
                continue
            for idx, bit in enumerate(info.get("bits", [])):
                if isinstance(bit, int):
                    self.inputs[bit] = (port, idx)
        # bit -> (inverting?, source bit) for buffer/inverter cells.
        self.alias: dict[int, tuple[bool, object]] = {}
        self.cells = module.get("cells", {})
        for cell in self.cells.values():
            ctype = cell.get("type", "")
            if ctype not in BUF_CELLS and ctype not in NOT_CELLS:
                continue
            conn = cell.get("connections", {})
            a, y = conn.get("A", []), conn.get("Y", [])
            if len(a) != len(y):
                continue  # extension: leave the extra bits untraced
            for abit, ybit in zip(a, y):
                if isinstance(ybit, int):
                    self.alias[ybit] = (ctype in NOT_CELLS, abit)

    def root(self, bit, polarity: int) -> tuple:
        """Trace a clock/enable bit to its root; polarity 1 = active high."""
        seen = set()
        while True:
            if not isinstance(bit, int):
                return ("const", str(bit))
            if bit in self.inputs:
                port, idx = self.inputs[bit]
                return ("input", port, idx, polarity)
            if bit in seen or bit not in self.alias:
                return ("derived", f"{self.name}:net{bit}")
            seen.add(bit)
            inverting, bit = self.alias[bit]
            if inverting:
                polarity ^= 1

    def has_async_ff(self) -> bool:
        return any(is_async_ff(cell.get("type", "")) for cell in self.cells.values())

    def signatures(self) -> list[tuple]:
        sigs: list[tuple] = []
        for cname, cell in self.cells.items():
            ctype = cell.get("type", "")
            conn = cell.get("connections", {})
            where = f"{self.name}:{cname}"
            if ctype in COARSE_FF:
                clk = conn.get("CLK", ["x"])
                sigs.append(("edge",) + self.root(clk[0], param_int(cell, "CLK_POLARITY", 1)))
            elif ctype in ("$ff", "$_FF_"):
                sigs.append(("global",))
            elif ctype in COARSE_LATCH:
                en = conn.get("EN", ["x"])
                sigs.append(("latch", where) + self.root(en[0], param_int(cell, "EN_POLARITY", 1)))
            elif ctype.startswith(FINE_FF_PREFIX):
                pol = 1 if ctype.split("_")[2][:1] == "P" else 0
                sigs.append(("edge",) + self.root(conn.get("C", ["x"])[0], pol))
            elif ctype.startswith(FINE_LATCH_PREFIX):
                pol = 1 if ctype.split("_")[2][:1] == "P" else 0
                sigs.append(("latch", where) + self.root(conn.get("E", ["x"])[0], pol))
            elif ctype.startswith("$_SR_") or ctype == "$sr":
                sigs.append(("async", where))
            elif ctype in ("$memwr", "$memwr_v2", "$memrd", "$memrd_v2"):
                if param_int(cell, "CLK_ENABLE"):
                    sigs.append(("edge",) + self.root(conn.get("CLK", ["x"])[0], param_int(cell, "CLK_POLARITY", 1)))
                elif ctype.startswith("$memwr"):
                    sigs.append(("async", where))
            elif ctype in ("$mem", "$mem_v2"):
                for kind in ("RD", "WR"):
                    ports = param_int(cell, f"{kind}_PORTS")
                    enable = param_bits(cell, f"{kind}_CLK_ENABLE", ports)
                    polarity = param_bits(cell, f"{kind}_CLK_POLARITY", ports)
                    clocks = conn.get(f"{kind}_CLK", [])
                    for i in range(ports):
                        if enable[i]:
                            clk = clocks[i] if i < len(clocks) else "x"
                            sigs.append(("edge",) + self.root(clk, polarity[i]))
                        elif kind == "WR":
                            sigs.append(("async", where))
        return sigs


def describe(sig: tuple) -> str:
    if sig[0] == "edge" and sig[1] == "input":
        _, _, port, idx, pol = sig
        return f"{'posedge' if pol else 'negedge'} {port}[{idx}]"
    if sig[0] == "edge":
        return f"edge on {' '.join(str(s) for s in sig[1:])}"
    return " ".join(str(s) for s in sig)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--json", type=pathlib.Path, required=True)
    parser.add_argument("--gold", default="gold")
    parser.add_argument("--gate", default="gate")
    args = parser.parse_args()
    try:
        modules = json.loads(args.json.read_text(encoding="utf-8"))["modules"]
    except (OSError, ValueError, KeyError) as error:
        print(f"clock_domains: {error}", file=sys.stderr)
        return 1
    per_side: dict[str, set[tuple]] = {}
    has_async = False
    for side in (args.gold, args.gate):
        if side not in modules:
            print(f"clock_domains: module {side!r} not found", file=sys.stderr)
            return 1
        module = Module(side, modules[side])
        per_side[side] = set(module.signatures())
        has_async = has_async or module.has_async_ff()
    union = per_side[args.gold] | per_side[args.gate]
    single = len(union) <= 1 and all(
        sig == ("global",) or (sig[0] == "edge" and sig[1] == "input") for sig in union
    )
    def listing(sigs: set[tuple]) -> str:
        names = sorted(describe(s) for s in sigs)
        if len(names) > 4:
            names = names[:4] + [f"... {len(names) - 4} more"]
        return ", ".join(names) or "no state"

    detail = "; ".join(f"{side}: {listing(sigs)}" for side, sigs in per_side.items())
    print(("single" if single else "multi") + ("+async " if has_async else " ") + detail)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
