#!/usr/bin/env python3
"""Structural sanity of an emitted `<Top>_Lgraph.lean` certificate.

REPLACES a `nodes > 0` check that was UNSOUNDLY STRICT. Two real CORE-ET
modules -- `null_vpu` and `minion_dcache_texsend` -- are legitimate ZERO-NODE
designs: every source is a constant, `nodes := #[]`, and each output references
a valid source slot. They are exactly what their names say (a null VPU stub,
and a block whose outputs are tied off), and the old gate refused them for
having no combinational nodes, which is not a defect.

What actually has to hold is that every REFERENCE resolves. Slots are dense:
sources occupy `0 .. nSources-1` and node `i` occupies `nSources + i`, so every
output slot, flop `din`/`enable`/`resetPin`, node `deps` entry and memory
`nextImg` must be `< nSources + nNodes`.

This is a cheap pre-check, not a substitute for `checkDesign`; it exists to
turn a malformed emission into a named failure in milliseconds rather than a
Lean error minutes later.

Usage: cert_shape_check.py <Top>_Lgraph.lean
"""
import re
import sys

REFS = (
    # (section, regex over that section, label)
    ("outputs", re.compile(r"slot\s*:=\s*(\d+)"), "output slot"),
    ("flops", re.compile(r"din\s*:=\s*(\d+)"), "flop din"),
    ("flops", re.compile(r"enable\s*:=\s*some\s+(\d+)"), "flop enable"),
    ("flops", re.compile(r"resetPin\s*:=\s*some\s+(\d+)"), "flop resetPin"),
    ("memories", re.compile(r"nextImg\s*:=\s*(\d+)"), "memory nextImg"),
)
# `{` may precede the first field on the same line, so allow it. If this regex
# stops matching the emitted layout the parser sees EMPTY sections, which would
# surface as bogus out-of-range failures (or, worse, as a pass) -- so a parse
# that finds no sections at all is a loud error, never an empty design.
SECTION = re.compile(r"^\s*\{?\s*(sources|nodes|outputs|flops|memories|clocks)\s*:=", re.M)


def sections(text):
    """Split the DesignCert body into its named field blocks."""
    out, marks = {}, list(SECTION.finditer(text))
    for i, m in enumerate(marks):
        end = marks[i + 1].start() if i + 1 < len(marks) else len(text)
        out[m.group(1)] = text[m.start():end]
    return out


def main():
    if len(sys.argv) != 2:
        print("usage: cert_shape_check.py <cert.lean>", file=sys.stderr)
        return 2
    try:
        text = open(sys.argv[1], encoding="utf-8", errors="replace").read()
    except OSError as e:
        print(f"FAIL: cannot read certificate: {e}")
        return 1
    sec = sections(text)
    missing = [k for k in ("sources", "nodes", "outputs") if k not in sec]
    if missing:
        print(f"FAIL: could not parse the certificate: no {', '.join(missing)} section(s) found")
        print("      (this is a PARSER failure, not a shape failure -- the emitted layout "
              "may have changed)")
        return 1
    n_src = len(re.findall(r"SourceDesc\.", sec.get("sources", "")))
    n_nod = len(re.findall(r"origin\s*:=", sec.get("nodes", "")))
    total = n_src + n_nod
    n_out = len(re.findall(r"slot\s*:=", sec.get("outputs", "")))
    print(f"sources={n_src} nodes={n_nod} outputs={n_out} slots={total}")

    bad = []
    if total == 0:
        bad.append("the certificate has no slots at all (no sources and no nodes)")
    if n_out == 0 and not re.search(r"flops\s*:=\s*#\[\s*\{", sec.get("flops", "")):
        bad.append("the certificate has neither outputs nor flops: it computes nothing observable")

    for name, rx, label in REFS:
        for m in rx.finditer(sec.get(name, "")):
            v = int(m.group(1))
            if v >= total:
                bad.append(f"{label} {v} is out of range (only {total} slot(s))")

    # node deps: `deps := #[a, b, ...]`
    for m in re.finditer(r"deps\s*:=\s*#\[([^\]]*)\]", sec.get("nodes", "")):
        for d in re.findall(r"\d+", m.group(1)):
            if int(d) >= total:
                bad.append(f"node dep {d} is out of range (only {total} slot(s))")

    if bad:
        for b in dict.fromkeys(bad):
            print(f"FAIL: {b}")
        return 1
    # A zero-node design is FINE as long as its references resolve.
    if n_nod == 0:
        print("note: zero-node design (outputs resolve directly to sources); refs valid")
    print("ok: all references resolve")
    return 0


if __name__ == "__main__":
    sys.exit(main())
