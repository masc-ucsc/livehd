#!/usr/bin/env python3
"""Static pre-proof gate over a generated <Top>_Lgraph_Cert.v.

Runs in seconds, before a long `rocq c`, and answers three questions:

  1. ARITY.  Does every emitted (op, arity) pair actually hit a real arm of
     `denote_op`?  Every arm in LGraphModel.v is TOTAL -- an unexpected arity
     falls through to `mk_bv w 0` rather than getting stuck.  That totality is
     what keeps Rocq's reducer from building an enormous partially-evaluated
     term, but it also means a wrong arity is SILENT: the certificate typechecks
     and evaluates, it just denotes zero.  This is the check that catches it.

  2. ID SCOPE.  Are all certificate ids spelled `%N`?  Rocq's `nat` is unary, so
     a LiveHD node id of 2000000000 as a `nat` literal builds a term with two
     billion successors the moment anything reduces it.

  3. SHAPE.  Which ops have a `simpleOpCertWfBool` rule (and so can carry the
     strong per-chunk shape lemma under `--set formal.rocq.cert_wf=chunked`),
     and which fall back.

Exit 0 iff checks 1 and 2 pass.  Check 3 is reported, never fatal.

Keep ARITY and SIMPLE_OPS in sync with formal/rocq/theories/Translation/LGraphModel.v
(`denote_op` and `simpleOpCertWfBool`).

Usage:
    pass/rocq/scripts/op_census.py <Top>_Lgraph_Cert.v [more_certs.v ...]
"""

import re
import sys
from collections import Counter, defaultdict

# op -> accepted arity predicate, mirroring denote_op's match arms.
# `None` means "any arity" (the arm consumes a list, not a fixed pattern).
ARITY = {
    "Op_Const": None,
    "Op_Sum": None,
    "Op_Sub": {2},
    "Op_Mult": None,
    "Op_Div": {2},
    "Op_UDiv": {2},
    "Op_SDiv": {2},
    "Op_And": None,
    "Op_Or": None,
    "Op_Xor": None,
    "Op_Ror": None,
    "Op_Not": {1},
    "Op_LT": {2},
    "Op_GT": {2},
    "Op_ULT": {2},
    "Op_UGT": {2},
    "Op_SLT": {2},
    "Op_SGT": {2},
    "Op_EQ": None,
    "Op_SHL": None,
    "Op_SRA": {2},
    "Op_MuxBool": {3},
    "Op_MuxN": None,
    "Op_Sext": {2},
    "Op_GetMask": {2},
    "Op_SetMask": {3},
}

# Ops that simpleOpCertWfBool gives a real shape rule (everything else returns
# false there, so a chunk containing one needs cert_wf_fallback).
SIMPLE_OPS = {
    "Op_Const", "Op_Sum", "Op_And", "Op_Or", "Op_Xor", "Op_Ror", "Op_Not",
    "Op_EQ", "Op_ULT", "Op_UGT", "Op_SLT", "Op_SGT", "Op_GetMask",
    "Op_MuxBool", "Op_MuxN", "Op_SHL", "Op_SRA", "Op_Sext",
}

NODE_RE = re.compile(
    r"\{\|\s*nc_nid\s*:=\s*(?P<nid>\d+)%N\s*;"
    r"\s*nc_op\s*:=\s*(?P<op>\(?\s*Op_[A-Za-z]+)(?P<oparg>[^;]*);"
    r"\s*nc_width\s*:=\s*(?P<width>\d+)\s*;"
    r"\s*nc_deps\s*:=\s*\[(?P<deps>[^\]]*)\]%(?P<scope>[A-Za-z]+)\s*\|\}"
)

# A dep list that is NOT %N-scoped (the bug this gate exists to catch).
BAD_SCOPE_RE = re.compile(r"nc_deps\s*:=\s*\[[^\]]*\]%(?!N\b)([A-Za-z]+)")
BARE_LIST_RE = re.compile(r"nc_deps\s*:=\s*\[[^\]]*\](?!%)")


def census(path):
    text = open(path, encoding="utf-8").read()

    nodes = []
    for m in NODE_RE.finditer(text):
        op = m.group("op").lstrip("( ").strip()
        deps = [d for d in (x.strip() for x in m.group("deps").split(";")) if d]
        nodes.append((int(m.group("nid")), op, int(m.group("width")), len(deps)))

    problems = []

    # --- 2. id scope --------------------------------------------------------
    for m in BAD_SCOPE_RE.finditer(text):
        problems.append(f"dep list scoped %{m.group(1)}, must be %N (Rocq nat is unary)")
    for _ in BARE_LIST_RE.finditer(text):
        problems.append("dep list with no scope delimiter, must be %N")
    if "gc_topo" in text and not re.search(r"gc_topo\s*:=\s*\[[^\]]*\]%N", text):
        problems.append("gc_topo is not %N-scoped")
    if "gc_sources" in text and not re.search(r"gc_sources\s*:=\s*\[[^\]]*\]%N", text):
        problems.append("gc_sources is not %N-scoped")

    # --- 1. arity -----------------------------------------------------------
    pairs = Counter((op, arity) for _, op, _, arity in nodes)
    unknown_ops = sorted({op for _, op, _, _ in nodes if op not in ARITY})
    for op in unknown_ops:
        problems.append(f"unknown operator {op} (not an LGraphOp constructor)")

    bad_arity = defaultdict(list)
    for nid, op, _, arity in nodes:
        allowed = ARITY.get(op, None)
        if allowed is not None and arity not in allowed:
            bad_arity[(op, arity)].append(nid)
    for (op, arity), nids in sorted(bad_arity.items()):
        problems.append(
            f"{op} with {arity} dep(s) at node(s) {nids[:5]}"
            f"{'...' if len(nids) > 5 else ''}: denote_op has no arm for this "
            f"arity, so it SILENTLY denotes 0 (expected {sorted(ARITY[op])})"
        )

    # --- report -------------------------------------------------------------
    print(f"== {path}")
    print(f"   {len(nodes)} node certificates")
    if not nodes:
        print("   (no certificate entries -- memory stub, or emit_cert=false?)")

    print("   (op, arity) census:")
    for (op, arity), n in sorted(pairs.items(), key=lambda kv: (-kv[1], kv[0])):
        flag = "" if op in SIMPLE_OPS else "   [no simpleOpCertWfBool shape rule]"
        print(f"     {op:<12} arity {arity:<2} x{n}{flag}")

    widths = Counter(w for _, _, w, _ in nodes)
    print("   width histogram (top 10):")
    for w, n in sorted(widths.items(), key=lambda kv: (-kv[1], kv[0]))[:10]:
        print(f"     {w:>5} bits  x{n}")

    non_simple = sorted({op for _, op, _, _ in nodes if op not in SIMPLE_OPS})
    if non_simple:
        print(f"   cert_wf=chunked will need cert_wf_fallback for: {', '.join(non_simple)}")
    else:
        print("   cert_wf=chunked: every op has a shape rule, no fallback needed")

    # The all-ones Get_mask zero-extend idiom, historically a bug magnet.
    allones = len(re.findall(r"mk_bv\s+\d+\s+\(\(-1\)\)", text))
    print(f"   all-ones (-1) certificate sources: {allones}")

    for p in problems:
        print(f"   ERROR: {p}")
    return len(problems)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    bad = 0
    for path in argv[1:]:
        bad += census(path)
    if bad:
        print(f"\nFAIL: {bad} problem(s) found")
        return 1
    print("\nOK")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
