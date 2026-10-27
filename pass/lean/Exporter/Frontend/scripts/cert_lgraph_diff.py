#!/usr/bin/env python3
"""Independent LGraph -> certificate differential gate.

## Why this exists

A wrong constant width shipped and passed BOTH existing gates:

  * step 5 (fast model == certificate) passed because the two share
    `pin_width()` in pass_lean.cpp -- they truncated the value IDENTICALLY, so
    the equality it proves held over the same wrong number;
  * the LEC gate passed because it compares the RTL against the LGraph, and
    never looks at the emitted certificate at all.

The specific value was `0x6000000` on an unsized const pin, modeled as 1 bit
(i.e. as 0), which silently changed what a 58-bit ROM output meant.

Any gate built out of pass.lean's own extractor would have missed it for the
same reason. This one reads the SAME post-cprop LGraph through a DIFFERENT
extractor -- `inou.cgen.verilog`, whose `const_to_verilog` / `dpin_width`
resolve widths independently (cgen_verilog.cpp:61, lgyosys_tolg.cpp:477) -- and
compares the constants the two produce.

## What it checks

  1. INVARIANT (no second extractor needed): every non-negative
     `SourceDesc.const w v` in the certificate satisfies `v < 2^w`. A value that
     does not fit its declared width is precisely the lie graph/node_util.hpp:323
     asserts against in debug builds.
  2. DIFFERENTIAL: every certificate constant value appears among the constants
     `cgen_verilog` renders from the same graph. A value the certificate holds
     but the Verilog never mentions means one of the two extractors invented or
     dropped it.

Usage:
  cert_lgraph_diff.py <Top>_Lgraph.lean [--verilog out.v] [--quiet]

Exit 0 iff every check passes. With no --verilog only check 1 runs, and that is
reported so a partial run is never mistaken for a full one.
"""

import re
import sys


def cert_consts(path):
    """[(width, value)] over SourceDesc.const in the emitted DesignCert."""
    txt = open(path, encoding="utf-8", errors="replace").read()
    out = []
    for m in re.finditer(r"SourceDesc\.const\s+(\d+)\s+\(\(?(-?)Int\.ofNat\s+(\d+)\)?\)", txt):
        out.append((int(m.group(1)), -int(m.group(3)) if m.group(2) else int(m.group(3))))
    for m in re.finditer(r"SourceDesc\.const\s+(\d+)\s+\(0\)", txt):
        out.append((int(m.group(1)), 0))
    return out


def rom_tables(path):
    """[(aw, dw, [entries])] over SourceDesc.memConst."""
    txt = open(path, encoding="utf-8", errors="replace").read()
    out = []
    for m in re.finditer(r"SourceDesc\.memConst\s+(\d+)\s+(\d+)\s+#\[([^\]]*)\]", txt):
        body = m.group(3).strip()
        vals = [int(x) for x in body.split(",")] if body else []
        out.append((int(m.group(1)), int(m.group(2)), vals))
    return out


def verilog_consts(path):
    """{value} over sized literals N'h.. / N'd.. / N'b.. in generated Verilog."""
    txt = open(path, encoding="utf-8", errors="replace").read()
    vals = set()
    for m in re.finditer(r"(\d+)'s?([hdb])([0-9a-fA-F_xzXZ?]+)", txt):
        base, digits = m.group(2), m.group(3).replace("_", "")
        if re.search(r"[xz?]", digits, re.I):
            continue
        try:
            vals.add(int(digits, {"h": 16, "d": 10, "b": 2}[base]))
        except ValueError:
            pass
    return vals


def main():
    if len(sys.argv) < 2:
        print("usage: cert_lgraph_diff.py <Top>_Lgraph.lean [--verilog out.v] [--quiet]", file=sys.stderr)
        return 2
    cert = sys.argv[1]
    vpath = None
    if "--verilog" in sys.argv:
        vpath = sys.argv[sys.argv.index("--verilog") + 1]
    quiet = "--quiet" in sys.argv
    fails = []

    # -- check 1: the value fits its declared width ---------------------------
    consts = cert_consts(cert)
    for w, v in consts:
        if v >= 0 and v >= (1 << w):
            fails.append(f"const width {w} cannot hold value {v} (needs {v.bit_length()} bits)")
    if not quiet:
        print(f"consts checked: {len(consts)}")

    # ROM entries must fit the table's data width too.
    for aw, dw, vals in rom_tables(cert):
        for i, v in enumerate(vals):
            if v < 0 or v >= (1 << dw):
                fails.append(f"ROM entry {i} = {v} does not fit dw={dw}")
        if not quiet:
            print(f"ROM table: aw={aw} dw={dw} entries={len(vals)}")

    # -- check 2: differential against an independent extractor ---------------
    if vpath:
        vv = verilog_consts(vpath)
        missing = sorted({v for _, v in consts if v > 1 and v not in vv})
        if not quiet:
            print(f"verilog consts: {len(vv)}; cert consts absent from it: {len(missing)}")
        # Only flag WIDE values: small ones are folded into expressions by cgen
        # and legitimately never appear as literals.
        for v in missing:
            if v.bit_length() > 8:
                fails.append(f"cert holds constant {v} ({v.bit_length()} bits) that cgen_verilog never emits")
    elif not quiet:
        print("NOTE: no --verilog given; the differential half did NOT run")

    for f in fails:
        print(f"FAIL: {f}")
    if not fails and not quiet:
        print("PASS: cert/LGraph constant differential")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
