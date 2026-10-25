#!/usr/bin/env python3
"""Derive slang-clean copies of the two CORE-ET files that use an identifier
before declaring it.

WHY THIS EXISTS.  SystemVerilog requires a declaration to precede its use.  Two
CORE-ET files violate that; Verilator (the repo's DV flow) tolerates it, slang
does not.  Between them they block 44 of the 122 minion modules from the LEC /
Lean pipeline.  These are ORIGINAL-REPO-CLASS defects and should be fixed in
core-et; until then we compile a derived copy, exactly as
scripts/run_cva6_alu_lean.sh derives alu_concrete.sv from upstream alu.sv.

WHAT THE PATCH IS.  A pure MOVE of declaration lines to just above their first
use.  Nothing is added, removed or reworded -- asserted below by comparing the
multiset of lines before and after.  A silent no-op here would produce a
degenerate model that still compiles, so every step hard-fails instead.

  1. hw/ip/minion/vpu/rtl/vpu_defs_pkg.sv
       TXFMA_EXP_FRAC_OFFSET   used ~:875, declared ~:892
  2. hw/ip/tech_generic/prim_mul_div/rtl/intpipe_mul_div_ctl.sv
       start_mul_2p / start_div_2p   used ~:114/:123, declared ~:139/:140
  3. hw/ip/shirecache/rtl/shirecache_pkg.sv  (B05, ET-Link->AXI)
       TagDataSize    used :65, declared :74
       L3ShireIdSize  used :105, declared :143, and with it L3Shires (:142),
                      which L3ShireIdSize's own initializer reads -- moved
                      SECOND so it lands above its new first use
  4. hw/ip/shirecache/rtl/shirecache_mesh_master_axi_port.sv  (B05)
       r_fifo_pop / b_fifo_pop  (one `logic` line)  used :224/:245, declared :278
     and, separately (EXPLICIT_IMPORTS), `import etlink_pkg::PaSize;`: the
     module wildcard-imports both etlink_pkg and shirecache_pkg, and
     shirecache_pkg::PaSize is declared `= etlink_pkg::PaSize` (checked).
"""
import os, re, sys, hashlib

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
# The output directory is overridable so a caller can derive into FRESH staging
# and compare, rather than writing straight over the directory a sweep consumes.
# Checking an embedded source-hash header is not enough on its own: a derived
# body can be edited with that header left intact.  Regenerating and comparing
# byte for byte is what actually shows the consumed files are this generator's
# output -- which is exactly what `IR_Semantics/scripts/coreet_preflight.sh` does, and it
# cannot do it without this seam.  Taken from pinned D4
# f6ad1fe29f8f15aeda045115864fd81a34c33aef; master's requirement that
# COREET_ROOT be set EXPLICITLY is kept, because the pinned copy defaulted it
# to a private checkout path.
OUTDIR = os.environ.get(
    "COREET_PATCHED_OUTDIR",
    os.path.join(ROOT, "generated", "core-et", "patched_src"))

# relative path -> list of identifiers whose declaration must move up
PATCHES = {
    "hw/ip/minion/vpu/rtl/vpu_defs_pkg.sv": ["TXFMA_EXP_FRAC_OFFSET"],
    "hw/ip/tech_generic/prim_mul_div/rtl/intpipe_mul_div_ctl.sv":
        ["start_mul_2p", "start_div_2p"],
    # ORDER MATTERS: L3ShireIdSize moves first, then L3Shires follows it up.
    "hw/ip/shirecache/rtl/shirecache_pkg.sv":
        ["TagDataSize", "L3ShireIdSize", "L3Shires"],
    "hw/ip/shirecache/rtl/shirecache_mesh_master_axi_port.sv": ["r_fifo_pop"],
}

# Files whose ANSI `output` ports carry NO data type and are then driven from an
# always block.  An untyped ANSI output defaults to a NET (wire), and a net may
# not be assigned procedurally -- slang rejects it, Verilator does not.  Adding
# `logic` is the lowRISC-standard spelling and is legal for BOTH continuous and
# procedural drivers, so it is safe to apply to every untyped output in the file
# rather than only the ones that happen to be procedurally driven today.
UNTYPED_OUTPUT_FILES = [
    "hw/ip/minion/vpu/rtl/txfmactl_top.sv",
    "hw/ip/minion/vpu/rtl/txfmaexp_top.sv",
]

# Files where two WILDCARD-imported packages both declare a name the module
# uses -- slang: "multiple imports found for identifier".  The adapter inserts
# ONE explicit `import <pkg>::<name>;` right after that package's wildcard
# import, which SystemVerilog resolves ahead of any wildcard.  It is CHECKED,
# not assumed: every other wildcard-imported package declaring the name must
# define it as `<pkg>::<name>` itself (an alias), so the choice changes no value.
#   rel -> [(package, name)]
EXPLICIT_IMPORTS = {
    # B05: shirecache_pkg::PaSize is declared `= etlink_pkg::PaSize`
    "hw/ip/shirecache/rtl/shirecache_mesh_master_axi_port.sv": [("etlink_pkg", "PaSize")],
}

OUTPUT_RE = re.compile(r"^(\s*output\s+)(?!(?:logic|wire|reg|bit|var)\b)(\S.*)$")


def decl_index(lines, ident):
    """Index of the line DECLARING `ident` (localparam/logic/wire/reg ... ident =|;)."""
    pat = re.compile(
        r"^\s*(localparam|parameter|logic|wire|reg|bit)\b[^;=]*\b" + re.escape(ident) + r"\s*(=|;|,)")
    hits = [i for i, l in enumerate(lines) if pat.match(l)]
    if len(hits) != 1:
        raise SystemExit(f"FATAL: expected exactly 1 declaration of {ident}, found {len(hits)}")
    return hits[0]


def first_use_index(lines, ident, decl_i):
    """Index of the first line REFERENCING `ident` that is not its declaration."""
    word = re.compile(r"\b" + re.escape(ident) + r"\b")
    for i, l in enumerate(lines):
        if i == decl_i:
            continue
        s = l.split("//")[0]
        if word.search(s):
            return i
    raise SystemExit(f"FATAL: {ident} is never used -- the patch premise is wrong")


def statement_start(lines, use_i):
    """Index of the line that BEGINS the statement containing `use_i`.

    Inserting a declaration directly above the first *use* is wrong when that use
    sits inside a multi-line construct: in intpipe_mul_div_ctl.sv the first use of
    `start_mul_2p` is `.q_hi_o (start_mul_2p)` INSIDE a module instantiation port
    list, and a `logic ...;` dropped there is a syntax error ("expected ')'").
    Walk back to the first line whose preceding code line terminates a statement.
    """
    def prev_code(i):
        j = i - 1
        while j >= 0:
            t = lines[j].split("//")[0].strip()
            if t:
                return t
            j -= 1
        return ""

    j = use_i
    while j > 0:
        pt = prev_code(j)
        if pt == "" or pt.endswith((";", "begin", "end")):
            return j
        j -= 1
    return use_i


def patch_file(rel, idents, coreet_root):
    src = os.path.join(coreet_root, rel)
    if not os.path.isfile(src):
        raise SystemExit(f"FATAL: missing source {src}")
    with open(src) as f:
        original = f.readlines()
    lines = list(original)

    moved = []
    for ident in idents:
        d = decl_index(lines, ident)
        u = first_use_index(lines, ident, d)
        if u > d:
            continue                      # already ordered; nothing owed
        decl = lines.pop(d)
        u = first_use_index(lines, ident, -1)   # recompute after the pop
        ins = statement_start(lines, u)
        lines.insert(ins, decl)
        moved.append(f"{ident}: decl {d+1} -> line {ins+1} (statement holding use {u+1})")

    if not moved:
        raise SystemExit(f"FATAL: {rel} needed no move -- upstream changed; "
                         f"re-check IR_Semantics/scripts/coreet_patch_srcs.py")

    # The patch must be a PURE REORDER.  If this ever fails, the transformation
    # changed content and the derived file can no longer stand in for the source.
    if sorted(original) != sorted(lines):
        raise SystemExit(f"FATAL: {rel} patch is not a pure reorder -- refusing to emit")

    for ident in idents:
        d = decl_index(lines, ident)
        u = first_use_index(lines, ident, d)
        if d > u:
            raise SystemExit(f"FATAL: {rel}: {ident} still declared after first use")

    out = os.path.join(OUTDIR, os.path.basename(rel))
    os.makedirs(OUTDIR, exist_ok=True)
    body = "".join(lines)
    hdr = ("// DERIVED by IR_Semantics/scripts/coreet_patch_srcs.py -- do not edit.\n"
           f"// source: {rel}\n"
           f"// sha256(source): {hashlib.sha256(''.join(original).encode()).hexdigest()[:16]}\n"
           "// change: pure reorder, declaration moved above first use --\n"
           f"//         {'; '.join(moved)}\n"
           # No continuation line may BEGIN with the word "verilator": verilator
           # reads the first token of a comment as a pragma, so a wrapped line
           # starting "Verilator does not..." is rejected as BADVLTPRAGMA and
           # the derived file will not elaborate under CORE-ET's own DV flow,
           # which is Verilator-based. Keep the word mid-line.
           "// reason: SystemVerilog requires declaration before use. slang enforces\n"
           "//         this; the Verilator-based flow does not, so it is an\n"
           "//         original-repo-class defect.\n")
    with open(out, "w") as f:
        f.write(hdr + body)
    print(f"{rel}\n  -> {out}\n     {'; '.join(moved)}")
    return src, out


def type_outputs(rel, coreet_root):
    """Give every untyped ANSI `output` port an explicit `logic` type."""
    src = os.path.join(coreet_root, rel)
    if not os.path.isfile(src):
        raise SystemExit(f"FATAL: missing source {src}")
    with open(src) as f:
        original = f.readlines()

    lines, n = [], 0
    for l in original:
        m = OUTPUT_RE.match(l.rstrip("\n"))
        if m and not l.lstrip().startswith("//"):
            lines.append(f"{m.group(1)}logic {m.group(2)}\n")
            n += 1
        else:
            lines.append(l)

    if n == 0:
        raise SystemExit(f"FATAL: {rel} has no untyped output to type -- upstream "
                         f"changed; re-check IR_Semantics/scripts/coreet_patch_srcs.py")
    # The ONLY difference may be an inserted `logic` token on `output` lines.
    if len(lines) != len(original):
        raise SystemExit(f"FATAL: {rel} line count changed -- refusing to emit")
    for a, b in zip(original, lines):
        if a != b and a.replace("output ", "output logic ", 1).split() != b.split():
            raise SystemExit(f"FATAL: {rel} patch changed more than the type token:\n"
                             f"  before: {a.rstrip()}\n  after : {b.rstrip()}")

    out = os.path.join(OUTDIR, os.path.basename(rel))
    os.makedirs(OUTDIR, exist_ok=True)
    hdr = ("// DERIVED by IR_Semantics/scripts/coreet_patch_srcs.py -- do not edit.\n"
           f"// source: {rel}\n"
           f"// sha256(source): {hashlib.sha256(''.join(original).encode()).hexdigest()[:16]}\n"
           f"// change: added an explicit `logic` type to {n} untyped ANSI output port(s).\n"
           "// reason: an untyped ANSI output defaults to a NET, and a net cannot be\n"
           "//         assigned procedurally.  slang enforces this, Verilator does not.\n")
    with open(out, "w") as f:
        f.write(hdr + "".join(lines))
    print(f"{rel}\n  -> {out}\n     typed {n} untyped output port(s)")
    return src, out


def find_pkg_file(coreet_root, pkg):
    hits = []
    for d, _, fs in os.walk(os.path.join(coreet_root, "hw")):
        for f in fs:
            if f == pkg + ".sv":
                hits.append(os.path.join(d, f))
    if len(hits) != 1:
        raise SystemExit(f"FATAL: expected exactly 1 file for package {pkg}, found {len(hits)}")
    return hits[0]


def explicit_imports(rel, pairs, coreet_root, derived):
    """Insert checked explicit imports; compose with an earlier derived copy."""
    src = os.path.join(coreet_root, rel)
    base = derived.get(os.path.realpath(src), src)
    with open(base) as f:
        original = f.readlines()
    lines = list(original)
    wild = re.findall(r"(?m)^\s*import\s+(\w+)::\*;", "".join(original))
    done = []
    for pkg, name in pairs:
        if pkg not in wild:
            raise SystemExit(f"FATAL: {rel} has no `import {pkg}::*;` to qualify")
        # the alias check: every OTHER wildcard package declaring `name`
        decl = re.compile(r"(?m)^\s*localparam\b[^;=]*\b" + re.escape(name) + r"\s*=\s*([^;]+);")
        others = []
        for q in wild:
            if q == pkg:
                continue
            m = decl.search(open(find_pkg_file(coreet_root, q)).read())
            if m:
                rhs = m.group(1).split("//")[0].strip()
                if rhs != f"{pkg}::{name}":
                    raise SystemExit(f"FATAL: {q}::{name} = {rhs}, not an alias of {pkg}::{name}; "
                                     f"an explicit import would choose a different value")
                others.append(q)
        if not others:
            raise SystemExit(f"FATAL: {rel}: no other wildcard package declares {name}; nothing to resolve")
        i = next(k for k, l in enumerate(lines) if re.match(r"^\s*import\s+" + pkg + r"::\*;", l))
        indent = re.match(r"^(\s*)", lines[i]).group(1)
        lines.insert(i + 1, f"{indent}import {pkg}::{name};\n")
        done.append(f"{pkg}::{name} (aliased by {', '.join(others)})")
    # ONLY inserted import lines may differ
    extra = list(lines)
    for l in original:
        extra.remove(l)
    if not all(re.match(r"^\s*import \w+::\w+;\s*$", l) for l in extra):
        raise SystemExit(f"FATAL: {rel} explicit-import adapter changed more than import lines")
    out = os.path.join(OUTDIR, os.path.basename(rel))
    os.makedirs(OUTDIR, exist_ok=True)
    hdr = ("// DERIVED by IR_Semantics/scripts/coreet_patch_srcs.py -- do not edit.\n"
           f"// change: explicit import(s) {'; '.join(done)}\n"
           "// reason: two wildcard-imported packages declare the name; slang\n"
           "//         refuses the ambiguity.  The packages alias one value (checked).\n")
    with open(out, "w") as f:
        f.write(hdr + "".join(lines))
    print(f"{rel}\n  -> {out}\n     {'; '.join(done)}")
    return src, out


def main():
    coreet_root = os.environ.get("COREET_ROOT")
    if not coreet_root:
        raise SystemExit("set COREET_ROOT to the read-only CORE-ET checkout")
    mapping = {}
    for rel, idents in PATCHES.items():
        src, out = patch_file(rel, idents, coreet_root)
        mapping[os.path.realpath(src)] = out
    for rel in UNTYPED_OUTPUT_FILES:
        src, out = type_outputs(rel, coreet_root)
        mapping[os.path.realpath(src)] = out
    for rel, pairs in EXPLICIT_IMPORTS.items():
        src, out = explicit_imports(rel, pairs, coreet_root, mapping)
        mapping[os.path.realpath(src)] = out
    mf = os.path.join(OUTDIR, "map.tsv")
    with open(mf, "w") as f:
        for k, v in mapping.items():
            f.write(f"{k}\t{v}\n")
    print(f"\nsubstitution map -> {mf}")


if __name__ == "__main__":
    main()
