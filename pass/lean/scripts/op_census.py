#!/usr/bin/env python3
"""Static op/width census for a generated `<Top>_Lgraph.lean`.

Answers, in seconds, the three questions that otherwise cost hours of discovery
before starting a step-5 bridge run on a new design:

  1. Which `(op, arity)` pairs does this design use, and does the step-5 bridge
     dispatch in `pass/lean/pass_lean.cpp` handle every one of them?  An
     unhandled pair becomes a `sorry -- TODO(step5)` placeholder, so catching it
     here replaces finding it after a multi-hour typecheck.
  2. How wide are the nodes?  Width is the CVA6 scaling axis (513/576 bits vs
     DINO's 127), and the GetMask `by decide` side condition is O(w^2) per node.
  3. How many GetMask nodes carry a constant ALL-ONES mask?  Those are the ones
     the closed-form `getmask_bridge_allones` fast path can discharge in O(1)
     instead of reducing `(List.range mw).filter ..` in the kernel.

Works on both cert-only files (`if n = <id> then ..` sourceEnv chain, `nodes_of_list`)
and bridge-enabled files (`BT.nd` trees).

Usage:  op_census.py <generated.lean> [--quiet]
Exit 0 iff every `(op, arity)` in the design is handled by the dispatch.
"""

import collections
import re
import sys

# ---------------------------------------------------------------------------
# The supported set.  This MUST mirror the dispatch chain in
# pass/lean/pass_lean.cpp (the `bridge_call = ..` if/else ladder, ~line 2113).
# Keep the two in sync: this gate is only as good as its fidelity to the emitter.
# ---------------------------------------------------------------------------

# `op` here is the certificate spelling minus the "LGraphOp." prefix, e.g.
# "Op_GetMask", "Op_Sum 2".  `arity` is len(deps).
def dispatch_status(op, arity, dep_widths, out_width=None):
    """Return (status, note).

    status is one of:
      "ok"        -- the emitter emits a real op bridge
      "unhandled" -- falls through to `sorry -- TODO(step5)`
      "trap"      -- the emitter CLAIMS support but the bridge lemma cannot
                     unify at this arity, so it emits a proof that fails to
                     typecheck (worse than a sorry: it is silent until the run)
    """
    if op.startswith("Op_GetMask"):
        # The emitter matches Op_GetMask by PREFIX and always emits
        # `getmask_bridge' _ _ (by decide)`, whose statement is
        # eval_op Op_GetMask b [bvenc X, bvenc M] -- exactly two deps.
        if arity != 2:
            return ("trap", "getmask_bridge' takes exactly 2 deps")
        return ("ok", "")
    if op == "Op_Sum 2" and arity == 2:
        return ("ok", "sum2_bridge")
    if op == "Op_Sum 1" and arity == 2:
        return ("ok", "sum1_bridge")
    if op == "Op_And" and arity == 2:
        return ("ok", "and_bridge")
    if op == "Op_And" and arity == 3:
        return ("ok", "and3_bridge (fold-free)")
    if op == "Op_Or" and arity == 2:
        # Binary Or does NOT take the n-ary bridge: orn_bv_bridge is correct at
        # arity 2 but its closer unfolds a List.foldl, which sent the kernel into
        # unbounded recursion on deep operand chains (Bug 9).
        return ("ok", "or_bridge (binary fast path)")
    if op == "Op_Or":
        return ("ok", "orn_bv_bridge (n-ary)")
    if op == "Op_Xor" and arity == 2:
        return ("ok", "xor_bridge")
    if op == "Op_Not" and arity == 1:
        return ("ok", "not_bridge")
    if op == "Op_SHL" and arity == 2:
        return ("ok", "shl_bridge")
    if op == "Op_Ror" and arity == 1:
        return ("ok", "ror1_bridge")
    if op == "Op_MuxBool" and arity == 3:
        return ("ok", "muxbool_bridge")
    if op == "Op_MuxN" and arity == 3:
        return ("ok", "muxn3_bridge")
    if op == "Op_SRA" and arity == 2:
        # A WIDENING SRA (out wider than operand) sign-extends and uses
        # sra_bridge_sext; sra_bridge itself requires w <= wa and would leave
        # `decide` proving a FALSE side condition. Report which arm applies so a
        # width regression here is visible statically.
        wa = dep_widths[0]
        if wa is not None and out_width is not None and out_width > wa:
            return ("ok", "sra_bridge_sext (widening, %d>%d)" % (out_width, wa))
        return ("ok", "sra_bridge (truncating)")
    if op in ("Op_EQ", "Op_ULT", "Op_UGT") and arity == 2:
        return ("ok", "eq/ult/ugt_bridge")
    if op == "Op_Sext" and arity == 2:
        return ("ok", "sext_bridge | sext_bridge_low")
    if op == "Op_SLT" and arity == 2:
        # slt_bridge is stated at a SINGLE width (a b : BitVec cw), so the
        # emitter only dispatches when both operand widths are equal.
        w0, w1 = dep_widths[0], dep_widths[1]
        if w0 is None or w1 is None:
            return ("unhandled", "SLT with undetermined dep widths (check by hand)")
        if w0 != w1:
            return ("unhandled", "SLT at unequal widths %d/%d (needs slt_bridge_max)" % (w0, w1))
        return ("ok", "slt_bridge")
    return ("unhandled", "no dispatch arm")


# ---------------------------------------------------------------------------
# Parsing
# ---------------------------------------------------------------------------

NODE_RE = re.compile(
    r"\{ nid := (\d+), op := (.+?), width := (\d+), deps := \[([^\]]*)\] \}"
)


def _balanced(text, i):
    """text[i] is '('.  Return (inner, index_after_close)."""
    assert text[i] == "("
    depth = 0
    for j in range(i, len(text)):
        if text[j] == "(":
            depth += 1
        elif text[j] == ")":
            depth -= 1
            if depth == 0:
                return text[i + 1 : j], j + 1
    return text[i + 1 :], len(text)


def parse_sources(text):
    """sid -> (width, value_expr).  Handles the chain and the BT forms."""
    out = {}
    # mk_bv <W> ( <value> )  preceded by either `if n = <sid> then ` (chain)
    # or `BT.nd <sid> (fun i s => ` / `(fun i => ` (bridge BT tree).
    for m in re.finditer(r"if n = (\d+) then mk_bv (\d+) ", text):
        sid, w = int(m.group(1)), int(m.group(2))
        k = text.find("(", m.end())
        val, _ = _balanced(text, k) if k != -1 else ("", 0)
        out[sid] = (w, val.strip())
    for m in re.finditer(r"BT\.nd (\d+) \(fun i(?: s)? => mk_bv (\d+) ", text):
        sid, w = int(m.group(1)), int(m.group(2))
        k = text.find("(", m.end())
        val, _ = _balanced(text, k) if k != -1 else ("", 0)
        out[sid] = (w, val.strip())
    # Bridge-mode `_src<id>` facts give the fast-side leaf spelling too; use them
    # to fill in any width the tree scan missed.
    for m in re.finditer(r"^theorem \w+_src(\d+) .*?= bvenc \((.+?)\) := by$", text, re.M):
        sid, leaf = int(m.group(1)), m.group(2).strip()
        if sid in out:
            continue
        mw = re.match(r"BitVec\.ofInt (\d+) (.+)$", leaf)
        if mw:
            out[sid] = (int(mw.group(1)), mw.group(2).strip())
    return out


def const_value(expr):
    """Parse an emitted certificate constant into an int, or None if not a constant.

    Emitted forms (int_of_const): `Int.ofNat 7`, `-Int.ofNat 1`, and parenthesised
    variants.  A leaf mentioning `i.` / `s.` / `BitVec.toNat` is an input/flop, not
    a constant.
    """
    e = expr.strip()
    while e.startswith("(") and e.endswith(")"):
        e = e[1:-1].strip()
    if "BitVec.toNat" in e or re.search(r"\b[is]\.", e):
        return None
    m = re.fullmatch(r"-\s*Int\.ofNat\s+(\d+)", e)
    if m:
        return -int(m.group(1))
    m = re.fullmatch(r"Int\.ofNat\s+(\d+)", e)
    if m:
        return int(m.group(1))
    m = re.fullmatch(r"-?\d+", e)
    if m:
        return int(e)
    return None


def is_all_ones(width, value):
    if value is None:
        return False
    return value == -1 or value == (1 << width) - 1


WIDTH_BUCKETS = [(1, 8), (9, 32), (33, 64), (65, 128), (129, 256), (257, 1 << 30)]


def bucket(w):
    for lo, hi in WIDTH_BUCKETS:
        if lo <= w <= hi:
            return "%d-%s" % (lo, "max" if hi > (1 << 20) else str(hi))
    return "?"


def histo_widths(widths):
    c = collections.Counter(bucket(w) for w in widths)
    order = ["%d-%s" % (lo, "max" if hi > (1 << 20) else str(hi)) for lo, hi in WIDTH_BUCKETS]
    return [(k, c[k]) for k in order if c[k]]


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    quiet = "--quiet" in sys.argv[1:]
    if len(args) != 1:
        print(__doc__)
        return 2
    path = args[0]
    text = open(path).read()

    mtop = re.search(r"^structure (\w+)_in where", text, re.M)
    top = mtop.group(1) if mtop else "?"

    nodes = {}
    for m in NODE_RE.finditer(text):
        nid = int(m.group(1))
        op = m.group(2).strip()
        op = op[len("LGraphOp.") :] if op.startswith("LGraphOp.") else op
        width = int(m.group(3))
        deps = [int(x) for x in re.findall(r"\d+", m.group(4))]
        nodes[nid] = (op, width, deps)

    sources = parse_sources(text)
    width_of = {nid: w for nid, (_, w, _) in nodes.items()}
    for sid, (w, _) in sources.items():
        width_of[sid] = w

    bridge_mode = "_refines_fast" in text
    n_flops = len(re.findall(r"^  st_\w+ : BitVec \d+$", text, re.M))
    n_rec = len(re.findall(r"^theorem \w+_rec\d+ ", text, re.M))

    print("design           : %s" % top)
    print("file             : %s" % path)
    print("cert nodes       : %d" % len(nodes))
    print("cert sources     : %d" % len(sources))
    print("state fields     : %d" % n_flops)
    print("bridge emitted   : %s%s" % (bridge_mode, (" (%d _rec theorems)" % n_rec) if bridge_mode else ""))
    if not nodes:
        print("\nNO certificate nodes found -- was this emitted with formal.lean.emit_cert=true?")
        return 1

    # -- (op, arity) histogram + dispatch status -----------------------------
    # Status is computed PER NODE, not per (op, arity): some conditions depend on
    # this node's widths (a widening vs truncating SRA, SLT at equal vs unequal
    # widths). Caching by (op, arity) would report whichever node happened to come
    # first and could hide a failing one behind a passing twin.
    per_pair = collections.Counter()
    examples = {}
    for nid, (op, w, deps) in nodes.items():
        st, note = dispatch_status(op, len(deps), [width_of.get(d) for d in deps], w)
        key = (op, len(deps), st, note)
        per_pair[key] += 1
        examples.setdefault(key, nid)

    print("\n(op, arity, dispatch) histogram -- %d distinct rows" % len(per_pair))
    print("  %-22s %5s %6s  %-9s %s" % ("op", "arity", "count", "status", "note"))
    for (op, ar, st, note), n in per_pair.most_common():
        print("  %-22s %5d %6d  %-9s %s" % (op, ar, n, st, note))

    # -- width census --------------------------------------------------------
    widths = [w for _op, w, _d in nodes.values()]
    print("\nnode output widths : max=%d" % max(widths))
    for k, n in histo_widths(widths):
        print("  %-10s %6d" % (k, n))
    wide = sum(1 for w in widths if w > 128)
    # Measured: the GetMask `by decide` side condition costs ~0.29 ms per mask bit
    # (linear, not quadratic), so wide nodes are a modest cost, not a cliff. Kept
    # visible because it is the only width-DEPENDENT term in the per-node proof.
    print("  wide (>128 bits) : %d  (~0.29 ms/bit if not on the all-ones fast path)" % wide)

    # -- GetMask all-ones census --------------------------------------------
    gm_total = gm_allones = 0
    gm_allones_widths = []
    gm_unknown = 0
    gm_overwide = []  # mask wider than the output -> NO lemma applies
    for nid, (op, w, deps) in nodes.items():
        if not op.startswith("Op_GetMask") or len(deps) != 2:
            continue
        gm_total += 1
        mdep = deps[1]
        if mdep not in sources:
            gm_unknown += 1
            continue
        mw, mval = sources[mdep]
        if is_all_ones(mw, const_value(mval)):
            gm_allones += 1
            gm_allones_widths.append(mw)
            # Both GetMask lemmas require (mask_indices M).length <= b, and for an
            # all-ones mask that length IS mw.  So mw > b is unprovable by either
            # `getmask_bridge'` or the all-ones fast path: the generic `by decide`
            # is deciding a FALSE proposition.  The emitter widens the mask to
            # max(src_w, out_w), so this happens exactly when a Get_mask
            # TRUNCATES (src wider than out).  Absent from DINO, hence never hit.
            if mw > w:
                gm_overwide.append((nid, mw, w))
    if gm_total:
        pct = 100.0 * gm_allones / gm_total
        print("\nGetMask nodes      : %d" % gm_total)
        print("  const all-ones mask : %d (%.1f%%)  <- getmask_bridge_allones candidates" % (gm_allones, pct))
        if gm_unknown:
            print("  mask dep not a source (computed mask): %d" % gm_unknown)
        if gm_allones_widths:
            print("  all-ones mask widths : max=%d" % max(gm_allones_widths))
            for k, n in histo_widths(gm_allones_widths):
                print("    %-10s %6d" % (k, n))
        if bridge_mode:
            # The emitted `first | <all-ones> | <generic>` dispatch means a spelling
            # drift degrades silently to the slow-but-correct branch, and the
            # emitter also silences the linters that would reveal it.  So report
            # how many nodes the EMITTER put on the fast path; a number well below
            # the all-ones count means the recognizer stopped matching.
            n_fast = len(re.findall(r"getmask_bridge_allones_ofNat", text))
            print("  emitted on fast path : %d / %d all-ones nodes" % (n_fast, gm_allones))
            if gm_allones and n_fast < gm_allones:
                print("    NOTE: %d all-ones node(s) did NOT get the fast path -- check the"
                      % (gm_allones - n_fast))
                print("          cert-leaf spelling recognizer in pass_lean.cpp (Op_GetMask arm).")
            print("  (to confirm Lean *takes* that branch, re-run with")
            print("   linter.unreachableTactic on: the generic branch should report unreachable)")

    # -- verdict ------------------------------------------------------------
    bad = [(k, n) for k, n in per_pair.items() if k[2] != "ok"]
    print("")
    if gm_overwide:
        print("FAIL: %d GetMask node(s) have an all-ones mask WIDER than the output width."
              % len(gm_overwide))
        print("      Both getmask_bridge' and the all-ones fast path require")
        print("      (mask_indices M).length <= out_width, which is FALSE here, so the")
        print("      generic `by decide` is deciding a false proposition and the node")
        print("      cannot close. Needs a truncating-GetMask lemma (pack_low of the")
        print("      low out_width selected bits). Examples (nid, mask_w, out_w):")
        for nid, mw, w in gm_overwide[:8]:
            print("        nid %-10d mask_w=%-5d out_w=%d" % (nid, mw, w))
        if len(gm_overwide) > 8:
            print("        ... and %d more" % (len(gm_overwide) - 8))
        return 1
    if not bad:
        print("PASS: all %d node(s) across %d (op, arity, dispatch) row(s) are handled."
              % (sum(per_pair.values()), len(per_pair)))
        return 0
    n_bad_nodes = sum(n for _k, n in bad)
    print("FAIL: %d node(s) across %d dispatch row(s) are NOT handled:" % (n_bad_nodes, len(bad)))
    for (op, ar, st, note), n in sorted(bad, key=lambda kv: -kv[1]):
        print("  %-22s arity %-3d %6d nodes  [%s] %s (e.g. nid %d)"
              % (op, ar, n, st, note, examples[(op, ar, st, note)]))
    print("\n'unhandled' -> emitter writes `sorry -- TODO(step5)`.")
    print("'trap'      -> emitter writes a bridge call that CANNOT unify; it fails only")
    print("               once the typecheck reaches it. Fix before any long run.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
