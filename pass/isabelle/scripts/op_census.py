#!/usr/bin/env python3
"""Static gate for a pass.isabelle certificate, run before any long proof build.

Three checks, all seconds-cheap, against a generated <Top>_Lgraph_Cert.thy:

  1. COVERAGE  every (op, arity) pair in the certificate is handled by a bridge
               lemma in formal/translation_correctness/Translation_OpBridge.thy.
               An uncovered pair means the per-node proof for those nodes cannot
               be discharged -- better to learn that now than 20 minutes into a
               build.

  2. SEXT      every Op_Sext has amount == out_width.  The certificate and the
               fast model use sign conventions that differ by one bit
               (cert keeps n bits, signed_take_bit n keeps n+1), and they agree
               only because the result is truncated to the output width.  A
               widening Sext would make the two models genuinely disagree, so
               sext_bridge_eq_width carries that hypothesis and this check is
               what stops a design from silently needing the case that is not
               proven.

  3. WIDTHS    report the operand-width invariant.  Every certificate dep is
               materialized at an explicit width; the fast model independently
               casts the same operand.  If those disagree the two models
               evaluate different values and no bridge can close the node.  All
               four emitter bugs in pass/isabelle/BRIDGE_BUGS.md violated it.

Exit 0 iff checks 1 and 2 pass.

Keep BRIDGES in sync with Translation_OpBridge.thy -- this file mirrors it, it
does not read it.
"""

import argparse
import collections
import re
import sys

# (op, arity) -> bridge lemma that discharges it.  Mirrors
# formal/translation_correctness/Translation_OpBridge.thy.
BRIDGES = {
    ("Op_Const", 0): "const_bridge",
    ("Op_And", 2): "and2_op_bridge",
    ("Op_Not", 1): "not1_op_bridge",
    ("Op_Xor", 2): "xor2_op_bridge",
    ("Op_Ror", 1): "ror1_bridge",
    ("Op_MuxBool", 3): "muxbool_bridge",
    ("Op_MuxN", 3): "muxn2_bridge",
    ("Op_ULT", 2): "ult_bridge",
    ("Op_UGT", 2): "ugt_bridge",
    ("Op_SLT", 2): "slt_bridge",
    ("Op_SGT", 2): "sgt_bridge",
    ("Op_EQ", 2): "eq2_bridge",
    ("Op_SRA", 2): "sra_bridge",
    ("Op_SHL", 2): "shl_bridge",
    ("Op_Sext", 2): "sext_bridge_eq_width",
    ("Op_GetMask", 2): "getmask_bridge",
}

# Ops whose bridge is an n-ary fold, so any arity is covered.
NARY = {"Op_Or": "orn_bridge", "Op_Sum": "sum2_bridge/sub_bridge"}

NODE_RE = re.compile(
    r"nid = (\d+), op = (Op_\w+)(?: \(?(-?\d+)\)?)?, width = (\d+), deps = \[([^\]]*)\]"
)


def parse(path):
    """Return (nodes, consts). nodes: list of (nid, op, arg, width, deps)."""
    text = open(path).read()
    nodes, consts = [], {}
    for m in NODE_RE.finditer(text):
        nid, op, arg, width, deps = m.groups()
        deps = [d.strip() for d in deps.split(",") if d.strip()]
        nodes.append((nid, op, arg, int(width), deps))
        if op == "Op_Const":
            consts[nid] = (int(arg), int(width))
    return nodes, consts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cert", help="a generated <Top>_Lgraph_Cert.thy")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    nodes, consts = parse(args.cert)
    if not nodes:
        print(f"FAIL: no node_cert entries parsed from {args.cert}", file=sys.stderr)
        return 2

    hist = collections.Counter((op, len(deps)) for _, op, _, _, deps in nodes)

    uncovered, covered_n = [], 0
    for (op, arity), count in sorted(hist.items(), key=lambda kv: -kv[1]):
        if (op, arity) in BRIDGES:
            how, ok = BRIDGES[(op, arity)], True
        elif op in NARY:
            how, ok = NARY[op] + " (n-ary)", True
        else:
            how, ok = "-- NO BRIDGE --", False
        covered_n += count if ok else 0
        if not ok:
            uncovered.append((op, arity, count))
        if not args.quiet:
            print(f"  {op:<12s} arity {arity:<3d} {count:>5d}   {how}")

    total = len(nodes)
    print(f"\ncoverage: {covered_n}/{total} nodes "
          f"({100.0 * covered_n / total:.1f}%), "
          f"{len(hist) - len(uncovered)}/{len(hist)} (op, arity) pairs")

    # -- check 2: Sext amount must equal the output width --------------------
    bad_sext = []
    for nid, op, _, width, deps in nodes:
        if op != "Op_Sext" or len(deps) != 2:
            continue
        amt = consts.get(deps[1])
        if amt is None:
            bad_sext.append((nid, width, "non-constant amount"))
        elif amt[0] != width:
            bad_sext.append((nid, width, f"amount {amt[0]}"))

    ok = True
    if uncovered:
        ok = False
        print("\nFAIL: no bridge lemma for:", file=sys.stderr)
        for op, arity, count in uncovered:
            print(f"  {op} arity {arity}  ({count} nodes)", file=sys.stderr)
    if bad_sext:
        ok = False
        print(f"\nFAIL: {len(bad_sext)} Op_Sext node(s) with amount != out_width.",
              file=sys.stderr)
        print("  sext_bridge_eq_width does not apply; the certificate and the fast",
              file=sys.stderr)
        print("  model genuinely disagree for a widening Sext.  See",
              file=sys.stderr)
        print("  pass/isabelle/BRIDGE_BUGS.md.", file=sys.stderr)
        for nid, width, why in bad_sext[:10]:
            print(f"    nid {nid}: out_w={width}, {why}", file=sys.stderr)

    if ok:
        print("\nPASS: every (op, arity) has a bridge; every Sext has amount == out_width")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
