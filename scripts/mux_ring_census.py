#!/usr/bin/env python3
"""Is an LGraph SCC a mux ring that can never be taken?  DIAGNOSTIC ONLY --
it decides eligibility for a proposed rewrite, and rewrites nothing.

THE RULE BEING MEASURED (pass/lean/CYCLE_PROVENANCE.txt part 6).  One
txfmafrac_top SCC was traced by hand to a ring of three muxes whose selectors
are a complete decode of a 2-bit value, so the ring arm is dead.  "All 39 SCCs
contain a mux" does NOT make that true of the other 38, and this script is what
turns the candidate into a count.  An SCC is ELIGIBLE when all of:

  R1  its in-SCC edges form ONE simple ring.
  R2  every ring member is a `mux`, or a pass-through PROVED to preserve
      bv_uint -- the NUMERIC VALUE, not the width.  See "what R2 proves".
  R3  every ring edge enters a mux DATA pin, never a selector pin, and every
      non-feedback arm and every selector is driven from OUTSIDE the SCC.
  R4  every ring mux's selector resolves to an equality predicate, or an OR of
      equality predicates, over the SAME root value.
  R5  the ring is UNREACHABLE: no value of that root makes every ring mux
      select its feedback arm.

================================ SEMANTICS ================================
Everything here is read off the model pass.lean emits, not guessed.  A
diagnostic that disagrees with the model can call a LIVE ring dead, which is
the one error that would license deleting a real feedback path.

BV.  `bv_uint x = x.value % 2^x.width` and `mk_bv w v = v % 2^w`
(Translation/LGraphModel.lean:68,74).  Every value below is a `bv_uint`, i.e.
non-negative and taken at a stated width.

Op_EQ (LGraphModel.lean:176) compares `bv_uint` of each operand AT ITS OWN
WIDTH -- it does NOT resize them to a common width first.  pass.lean
materializes an EQ's CONSTANT operand at
    dep_w = max(1, pin_width of every operand of that EQ)      (pass_lean.cpp:1498)
so the constant's contribution is `value % 2^dep_w`.  Two consequences, both of
which could produce a wrong ELIGIBLE if ignored:
  * `-1` is not a distinguished value.  At dep_w = 2 it IS 3, so `eq(V,-1)`
    and `eq(V,3)` are the SAME predicate on a 2-bit V -- a reader comparing
    the integers -1 and 3 sees two incompatible constraints and calls a
    satisfiable ring unsatisfiable.
  * dep_w is a property of the EQ's IMMEDIATE operands, never of the root the
    walk below ends at.  Widen V from 2 to 3 bits through a zext before the
    EQ and `-1` becomes 7, not 3 -- a predicate no 2-bit V can satisfy.  So
    the constant is normalised where the EQ is, before any walking.

Op_MuxN (LGraphModel.lean:188) selects `args[bv_uint sel]`, and pass.lean puts
the selector first and the data arms in pin order, so DATA PIN pid is taken
exactly when `bv_uint(selector) == pid - 1`.  That is where mux polarity comes
from: it is read off the pin index, never off a name.  Op_MuxBool (chosen when
there are 2 arms and a 1-bit selector) agrees: p1 on 0, p2 on nonzero.

WHAT R2 PROVES, and what it does not.  A pass-through is accepted when
`bv_uint(out) == bv_uint(a)` -- the numeric value survives the step.  That is
NOT the same as the step being a no-op: a zero extension from 2 bits to 3
preserves bv_uint while changing the BitVec type, and this accepts it.  So a
future rewrite that bypasses or deletes such a step MUST re-establish the
node's own OUTPUT WIDTH at the bypass point.  Dropping a `get_mask(a,-1)` that
widened 2 bits to 3 and wiring the 2-bit source straight through changes every
downstream operator that reads a width -- `Op_Sext`'s sign position,
`Op_MuxN`'s selector index, the next `bv_resize` -- even though no VALUE moved.
This census decides reachability, which depends only on values; preserving
widths is the rewrite's obligation, not something measured here.

Pass-throughs are accepted only with their exact width conditions; see
`step_mask`.  The headline trap is Op_Sext (LGraphModel.lean:191): at output
width w > n it subtracts 2^n from a value whose bit n-1 is set, so bv_uint is
NOT preserved, and "the amount equals the input width" is not enough to accept
it.

================================ FAIL-CLOSED ==============================
Parsing is `scripts/lgraph_scc.py`'s, which refuses a dump whose endpoints do
not resolve.  Beyond that: the `const_bits` column is REQUIRED (an older dump
without it is refused outright, not silently treated as width-less), its
entries must align one-for-one with `consts`, and a `?` width on a constant an
EQ needs refuses that SCC.  Every step either proves its precondition or
records a REASON, so a shape this script does not understand lands in the
histogram, never in the eligible count.
"""
import argparse
import json
import os
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lgraph_scc as L  # noqa: E402

EXIT_OK = 0
EXIT_UNSOUND = 3

WALK_DEPTH_CAP = 64


class Refuse(Exception):
    """Not an error: a measured reason this SCC is not eligible.

    `note` carries a SECOND measurement taken after the refusal -- it never
    changes eligibility, it says how much is lost by refusing."""
    def __init__(self, reason, note=None):
        super().__init__(reason)
        self.reason = reason
        self.note = note


def low_mask(w):
    return (1 << w) - 1


def norm(value, width):
    """`bv_uint (mk_bv width value)`.  Python's % already returns the
    non-negative residue, which is what Lean's Int.emod gives for a positive
    modulus -- so -1 at width 2 is 3, here and there alike."""
    return value % (1 << width)


# --------------------------------------------------------------------------
# the dump
# --------------------------------------------------------------------------
def load_node_consts(path):
    """nid -> pin -> list of (value, width|None).

    hhds' CONST_NODE is skipped by the node/edge iterators, so a literal
    operand appears only in the node record's `consts` column, and its WIDTH
    only in `const_bits` (lhd/lhd_kernel_tool.cpp).  The two are emitted by one
    loop in one order; that is checked here rather than assumed, because a
    silent misalignment would attach one constant's width to another's value."""
    out = defaultdict(lambda: defaultdict(list))
    saw_column = False
    for lineno, line in enumerate(open(path), 1):
        line = line.strip()
        if not line:
            continue
        r = json.loads(line)
        if r.get("t") != "node":
            continue
        cs, cb = r.get("consts"), r.get("const_bits", "MISSING")
        if cb == "MISSING":
            raise L.Unsound(
                f"{path}:{lineno} has no `const_bits` column.  This dump predates "
                f"the constant-width metadata, and without it a negative literal "
                f"cannot be normalised to the width LGraph compares it at.  "
                f"Re-dump with a current `lhd tool cat`.")
        saw_column = True
        if not cs or cs == "nil":
            if cb and cb != "nil":
                raise L.Unsound(f"{path}:{lineno} has const_bits but no consts")
            continue
        vals, bits = cs.split(","), (cb or "").split(",")
        if cb == "nil" or len(vals) != len(bits):
            raise L.Unsound(f"{path}:{lineno} consts has {len(vals)} entries but "
                            f"const_bits has {0 if cb == 'nil' else len(bits)}")
        for ve, be in zip(vals, bits):
            vlabel, _s1, vtxt = ve.partition("=")
            blabel, _s2, btxt = be.partition("=")
            if not _s1 or not _s2 or vlabel != blabel:
                raise L.Unsound(f"{path}:{lineno} consts/const_bits entries are not "
                                f"aligned: {ve!r} vs {be!r}")
            try:
                value = int(vtxt, 0)
            except ValueError:
                raise L.Unsound(f"{path}:{lineno} const {ve!r} is not an integer literal")
            if btxt == "?":
                width = None            # stated-as-unknown: refuse at the use site
            else:
                # Anything else must be a usable width.  Letting a non-integer
                # through would surface as a traceback, and 0 or a negative
                # would reach `norm`/`low_mask` and silently produce an empty
                # value domain -- i.e. a predicate nothing can satisfy, which
                # is the wrong-YES direction.
                if not btxt.lstrip("-").isdigit():
                    raise L.Unsound(f"{path}:{lineno} const width {be!r} is neither "
                                    f"an integer nor '?'")
                width = int(btxt)
                if width < 1:
                    raise L.Unsound(f"{path}:{lineno} const width {be!r} is not positive; "
                                    f"a constant is compared at a width of at least 1")
            out[r["nid"]][vlabel].append((value, width))
    if not saw_column:
        raise L.Unsound(f"{path} declares no node with a const_bits column")
    return out


def build_inputs(raw_edges, idx):
    """nid -> sink pin -> list of ('node', nid, pin, bits) | ('io', name, bits).

    From the RAW edges, before any cut: a mux arm driven by a flop is cut for
    the SCC walk but is still an arm.  `bits` is the DRIVER pin's width and is
    kept because it is the only place a graph input's width appears."""
    out = defaultdict(lambda: defaultdict(list))
    for e in raw_edges:
        d = L.resolve(e["from"], idx)
        s = L.resolve(e["to"], idx)
        if s[0] != "node" or d[0] == "bad":
            continue
        out[s[1]][s[2]].append(tuple(d) + (e.get("bits"),))
    return out


def pin_index(pin):
    if len(pin) >= 2 and pin[0] == "p" and pin[1:].isdigit():
        return int(pin[1:])
    return None


class Resolver:
    def __init__(self, nodes, inputs, consts, driver_pins):
        self.nodes = nodes
        self.inputs = inputs
        self.consts = consts
        self.driver_pins = driver_pins

    # ---- widths -------------------------------------------------------
    def out_bits(self, nid):
        """A node's own output width.  Refuses a multi-output node rather than
        pick one: every op this walk accepts has exactly one driver pin."""
        d = self.driver_pins.get(nid) or {}
        ws = {b for b in d.values() if b}
        if len(ws) != 1:
            raise Refuse("output_width_not_single")
        return ws.pop()

    def drv_bits(self, drv):
        """`pin_width` of a driver: its own width, or 1 when unset -- the same
        floor pass_lean.cpp:216 applies."""
        b = drv[-1]
        return b if b else 1

    def var_edges(self, nid, pin):
        return list(self.inputs.get(nid, {}).get(pin, []))

    def const_list(self, nid, pin):
        return list(self.consts.get(nid, {}).get(pin, []))

    # ---- one pass-through step ----------------------------------------
    def step_mask(self, nid, kind):
        """-> (driver_of_a, mask) when `bv_uint(out) == bv_uint(a) & mask`,
        else None (meaning: this node is a root, not a pass-through).

        `mask` is an int; it is never None, so a caller wanting value
        preservation asks for `mask == low_mask(bits(a))`.  Note what that
        does and does not say: it is a statement about bv_uint, so a step that
        WIDENS (a zext) qualifies.  Every case states the widths it depends on
        -- that is the whole point of this function."""
        w = self.out_bits(nid)

        if kind == "get_mask":
            a, ms = self.var_edges(nid, "p0"), self.const_list(nid, "p2")
            if len(a) != 1 or len(ms) != 1 or self.const_list(nid, "p0"):
                return None
            # pass.lean materialises the mask at max(src_w, out_w)
            # (pass_lean.cpp:1615), so its own declared width is irrelevant
            # here and a -1 mask always selects every source bit.
            ab = self.drv_bits(a[0])
            m = norm(ms[0][0], max(ab, w))
            if m != low_mask(max(ab, w)) and (m & (m + 1)) != 0:
                return None          # not contiguous-low: bv_get_mask PACKS, not ANDs
            return (a[0], m & low_mask(w))

        if kind == "and":
            a, cs = self.var_edges(nid, "p0"), self.const_list(nid, "p0")
            if len(a) != 1:
                return None
            # An And's constant operand is materialised at the AND's own width
            # (pass_lean.cpp:1505 group), and `bv_bitwise` reads a constant's
            # bits above its width as 0 -- so `-1` here is all-ones AT w.
            eff = low_mask(w)                        # from bv_resize w a
            for cv, _cw in cs:
                eff &= norm(cv, w)
            return (a[0], eff)

        if kind == "or":
            a, cs = self.var_edges(nid, "p0"), self.const_list(nid, "p0")
            if len(a) != 1 or any(norm(cv, w) != 0 for cv, _cw in cs):
                return None
            return (a[0], low_mask(w))

        if kind == "sext":
            a, ns = self.var_edges(nid, "p0"), self.const_list(nid, "p1")
            if len(a) != 1 or len(ns) != 1:
                return None
            n = ns[0][0]
            if n <= 0:
                return None
            ab = self.drv_bits(a[0])
            # Op_Sext: u = bv_uint(a) % 2^n; s = u < 2^(n-1) ? u : u - 2^n;
            # out = s % 2^w.  Two shapes are a plain mask, and the one the old
            # version wrongly accepted -- a WIDENING sext over a value that can
            # set bit n-1 -- is not one of them, because the -2^n survives.
            if w <= n:
                return (a[0], low_mask(w))
            if n > ab:
                return (a[0], low_mask(ab))          # no sign bit reachable
            return None

        return None

    # ---- value walk ----------------------------------------------------
    def resolve_value(self, drv, mask=None, depth=0):
        """A driver -> (root_key, mask, root_bits), with the invariant
            bv_uint(drv) == bv_uint(root) & mask        (mask None == equality)

        Stopping early is always sound: the root is then an opaque value of its
        container width, so the enumerated domain is a SUPERSET of what it can
        really take, and no member of a superset satisfying the constraints
        still proves the ring dead."""
        if depth > WALK_DEPTH_CAP:
            raise Refuse("value_walk_depth_cap")
        if drv[0] == "io":
            return (("io", drv[1]), mask, self.drv_bits(drv))
        nid, pin = drv[1], drv[2]
        step = self.step_mask(nid, self.nodes[nid]["kind"])
        if step is None:
            return (("node", nid, pin), mask, self.drv_bits(drv))
        a, m = step
        full = low_mask(self.drv_bits(a))
        nm = mask if (m & full) == full else (m if mask is None else (mask & m))
        return self.resolve_value(a, nm, depth + 1)

    # ---- predicate walk -------------------------------------------------
    def eq_predicate(self, nid):
        """An `eq` node -> (root_key, mask, root_bits, c_norm).

        dep_w is computed HERE, from this EQ's immediate operands, before any
        walking -- see the Op_EQ note in the module docstring."""
        a, cs = self.var_edges(nid, "p0"), self.const_list(nid, "p0")
        if len(a) != 1 or len(cs) != 1:
            raise Refuse("eq_not_one_variable_vs_one_constant")
        cval, cwidth = cs[0]
        if cwidth is None:
            raise Refuse("eq_constant_width_unknown")
        dep_w = max(1, self.drv_bits(a[0]), cwidth)
        key, mask, rbits = self.resolve_value(a[0])
        return (key, mask, rbits, norm(cval, dep_w))

    def resolve_pred(self, drv, depth=0):
        """A selector driver -> list of (root_key, mask, root_bits, c_norm),
        read as an OR: the predicate is true iff the value equals ANY of them.

        What the mux reads is `bv_uint` of this driver, so a pass-through is
        only transparent for a 0/1 predicate if it is the exact identity."""
        if depth > WALK_DEPTH_CAP:
            raise Refuse("selector_walk_depth_cap")
        if drv[0] == "io":
            raise Refuse("selector_is_graph_io")
        nid = drv[1]
        kind = self.nodes[nid]["kind"]

        if kind == "eq":
            return [self.eq_predicate(nid)]

        if kind == "or":
            a, cs = self.var_edges(nid, "p0"), self.const_list(nid, "p0")
            w = self.out_bits(nid)
            if any(norm(cv, w) != 0 for cv, _cw in cs):
                raise Refuse("selector_or_has_nonzero_constant")
            if not a:
                raise Refuse("selector_or_empty")
            if len(a) == 1:
                if not self.value_preserving_step(nid, kind):
                    raise Refuse("selector_or_not_value_preserving")
                return self.resolve_pred(a[0], depth + 1)
            out = []
            for d in a:
                out.extend(self.resolve_pred(d, depth + 1))
            return out

        if kind in ("get_mask", "and", "sext"):
            if not self.value_preserving_step(nid, kind):
                raise Refuse("selector_%s_not_value_preserving" % kind)
            return self.resolve_pred(self.step_mask(nid, kind)[0], depth + 1)

        raise Refuse("selector_not_eq_or_or_of_eq:" + kind)

    def value_preserving_step(self, nid, kind):
        """True when `bv_uint(out) == bv_uint(a)`: the mask covers every bit
        the operand can set.  The OUTPUT WIDTH may still differ (a zext passes
        this), which is why the rewrite note in the module docstring exists."""
        step = self.step_mask(nid, kind)
        if step is None:
            return False
        a, m = step
        full = low_mask(self.drv_bits(a))
        return (m & full) == full


# --------------------------------------------------------------------------
def check_ring_member(rv, nid, in_pin):
    """R2 for a non-mux ring member: bv_uint preserved, at real widths.

    A ring member only has to carry the VALUE around the ring for the
    reachability question to be answered.  Width preservation is the separate
    obligation a rewrite inherits -- see the module docstring."""
    kind = rv.nodes[nid]["kind"]
    if kind not in ("sext", "get_mask", "and", "or"):
        raise Refuse("ring_member_not_mux_or_passthrough:" + kind)
    step = rv.step_mask(nid, kind)
    if step is None:
        raise Refuse("ring_%s_not_a_passthrough" % kind)
    if step[0][0] == "node" and in_pin != "p0":
        raise Refuse("ring_%s_feedback_on_non_data_pin" % kind)
    if not rv.value_preserving_step(nid, kind):
        raise Refuse("ring_%s_not_value_preserving" % kind)
    return "%s(value-preserving)" % kind


def domain_ok(rbits, cap):
    return rbits is not None and rbits <= cap


def ring_sat(preds, env_of):
    """Is there an assignment making EVERY ring mux take its feedback arm?"""
    for mx, pl in preds:
        val = 1 if any((env_of(key) if mask is None else (env_of(key) & mask)) == c
                       for key, mask, _rb, c in pl) else 0
        if val != mx["need_sel"]:
            return False
    return True


def multivar_note(preds, keys, cap):
    """R4 wants ONE shared root.  With several, ask the weaker question:
    treating each distinct root as an INDEPENDENT free variable, is the
    all-feedback assignment still unsatisfiable?

    Independence over-approximates the reachable set, so UNSAT here is still a
    proof that the ring is dead and would license a wider rule, while SAT only
    means neither rule decides it."""
    widths = {}
    for key, rbits in keys:
        if not domain_ok(rbits, cap):
            return f"multivar: a root is {rbits} bits, over the --max-root-width cap"
        widths[key] = rbits
    total = 1
    for w in widths.values():
        total *= (1 << w)
        if total > (1 << 20):
            return "multivar: product domain over 2^20, not enumerated"
    order = sorted(widths, key=str)      # set order is not stable; evidence must be
    for combo in range(total):
        env, rest = {}, combo
        for k in order:
            n = 1 << widths[k]
            env[k] = rest % n
            rest //= n
        if ring_sat(preds, lambda k: env[k]):
            return ("multivar: REACHABLE with all roots free, e.g. "
                    + ", ".join(f"{k}={env[k]}" for k in order))
    return "multivar: UNREACHABLE even with every root free -- a wider rule would dissolve it"


def analyse_scc(rv, members, internal, max_root_width):
    mset = set(members)
    succ, pred = defaultdict(list), defaultdict(list)
    for r in internal:
        succ[r["d"]].append(r)
        pred[r["s"]].append(r)

    # ---- R1: one simple ring -------------------------------------------
    for m in members:
        if len(succ[m]) != 1 or len(pred[m]) != 1:
            raise Refuse("not_simple_ring")
    start, walk, cur = members[0], [], members[0]
    for _ in range(len(members) + 1):
        walk.append(cur)
        cur = succ[cur][0]["s"]
        if cur == start:
            break
    else:
        raise Refuse("not_simple_ring")
    if len(walk) != len(members) or set(walk) != mset:
        raise Refuse("not_simple_ring")

    # ---- R2/R3 ----------------------------------------------------------
    muxes, passthroughs = [], []
    for m in members:
        fb = pred[m][0]
        if rv.nodes[m]["kind"] != "mux":
            passthroughs.append((m, check_ring_member(rv, m, fb["sp"])))
            continue
        pidx = pin_index(fb["sp"])
        if pidx is None:
            raise Refuse("ring_mux_feedback_on_named_pin")
        if pidx == 0:
            raise Refuse("feedback_into_selector")
        sel = rv.var_edges(m, "p0")
        if len(sel) != 1 or rv.const_list(m, "p0"):
            raise Refuse("mux_selector_arity")
        if sel[0][0] == "node" and sel[0][1] in mset:
            raise Refuse("selector_driven_from_inside_scc")
        for pin, drvs in rv.inputs.get(m, {}).items():
            if pin == fb["sp"]:
                continue
            for d in drvs:
                if d[0] == "node" and d[1] in mset:
                    raise Refuse("non_feedback_arm_inside_scc")
        muxes.append({"nid": m, "fb_pin": fb["sp"], "need_sel": pidx - 1, "sel_drv": sel[0]})
    if not muxes:
        raise Refuse("ring_has_no_mux")

    # ---- R4: one shared root --------------------------------------------
    keys, preds = set(), []
    for mx in muxes:
        pl = rv.resolve_pred(mx["sel_drv"])
        for key, _mask, rbits, _c in pl:
            keys.add((key, rbits))
        preds.append((mx, pl))
    keys = sorted(keys, key=str)
    if len(keys) != 1:
        raise Refuse("selectors_over_different_values", multivar_note(preds, keys, max_root_width))
    key, rbits = keys[0]
    if rbits is None:
        raise Refuse("root_width_unknown")
    if rbits > max_root_width:
        raise Refuse("root_width_over_cap")

    # ---- R5 --------------------------------------------------------------
    # A predicate net is 0/1, so a feedback arm at pid >= 3 can never be taken.
    trivially_dead = [mx["nid"] for mx, _ in preds if mx["need_sel"] > 1]
    sat = None
    if not trivially_dead:
        for x in range(1 << rbits):
            if ring_sat(preds, lambda _k, _x=x: _x):
                sat = x
                break
    if trivially_dead or sat is None:
        return {"ring": walk, "muxes": muxes, "preds": preds, "passthroughs": passthroughs,
                "root": key, "root_bits": rbits, "trivially_dead": trivially_dead}
    raise Refuse("ring_reachable_at_root_value_%d" % sat)


def explain(rv, members, internal):
    pred = defaultdict(list)
    for r in internal:
        pred[r["s"]].append(r)
    print("        --- explain ---")
    for m in sorted(members):
        kind = rv.nodes[m]["kind"]
        fb = pred[m][0]["sp"] if pred[m] else "?"
        if kind != "mux":
            print(f"        n_{m} {kind} (feedback enters {fb})")
            continue
        sel = rv.var_edges(m, "p0")
        if len(sel) != 1:
            print(f"        n_{m} mux fb={fb} selector drivers={sel}")
            continue
        try:
            pl = rv.resolve_pred(sel[0])
        except Refuse as e:
            print(f"        n_{m} mux fb={fb} selector UNRESOLVED {e.reason}")
            continue
        for key, mask, rbits, c in pl:
            print(f"        n_{m} mux fb={fb} (needs sel=={pin_index(fb) - 1}) "
                  f"<- (root {key}, {rbits}b, mask={mask}) == {c}")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("jsonl")
    ap.add_argument("--cut", default="lean", choices=L.CUTS)
    ap.add_argument("--max-root-width", type=int, default=16,
                    help="refuse (never accept) a root value wider than this")
    ap.add_argument("--explain", type=int, default=None, metavar="SCC")
    args = ap.parse_args()

    try:
        res = L.analyse(args.jsonl, args.cut)
        consts = load_node_consts(args.jsonl)
    except L.Unsound as e:
        print(f"UNSOUND: {e}", file=sys.stderr)
        return EXIT_UNSOUND

    nodes = res["nodes"]
    idx = L.endpoint_index(nodes)
    rv = Resolver(nodes, build_inputs(res["raw_edges"], idx), consts, res["driver_pins"])

    by_member = {}
    for i, c in enumerate(res["comps"]):
        for m in c:
            by_member[m] = i
    internal_of = defaultdict(list)
    for r in res["edges"]:
        if by_member.get(r["d"]) is not None and by_member.get(r["d"]) == by_member.get(r["s"]):
            internal_of[by_member[r["d"]]].append(r)

    print(f"# mux-ring eligibility census  dump={args.jsonl}")
    print(f"# cut={args.cut}  max_root_width={args.max_root_width}  SCCS={len(res['comps'])}")
    print()
    reasons, notes, eligible = defaultdict(int), defaultdict(int), []
    for i, comp in enumerate(res["comps"]):
        members = sorted(comp)
        kinds = defaultdict(int)
        for m in members:
            kinds[nodes[m]["kind"]] += 1
        ksum = ",".join(f"{k}={v}" for k, v in sorted(kinds.items()))
        head = f"SCC #{i} size={len(members)} [{ksum}]"
        try:
            d = analyse_scc(rv, members, internal_of[i], args.max_root_width)
        except Refuse as e:
            reasons[e.reason] += 1
            print(f"{head}  NOT-ELIGIBLE  {e.reason}")
            if e.note:
                print(f"        {e.note}")
                notes[e.note.split(",")[0].split(" -- ")[0]] += 1
            if args.explain == i:
                explain(rv, members, internal_of[i])
            continue
        eligible.append(i)
        print(f"{head}  ELIGIBLE  root={d['root']} {d['root_bits']}b")
        for mx, pl in d["preds"]:
            cs = "|".join(str(c) for _k, _m, _b, c in pl)
            ms = {m for _k, m, _b, _c in pl}
            mt = "" if ms == {None} else f" mask={sorted(str(m) for m in ms)}"
            print(f"        n_{mx['nid']}.{mx['fb_pin']}(sel=={mx['need_sel']}) <- eq{{{cs}}}{mt}")
        if d["passthroughs"]:
            print("        passthroughs: " + ", ".join(f"n_{n}={w}" for n, w in d["passthroughs"]))
        if d["trivially_dead"]:
            print("        unreachable because feedback pin index > 2 on: "
                  + ", ".join(f"n_{n}" for n in d["trivially_dead"]))
        if args.explain == i:
            explain(rv, members, internal_of[i])

    print()
    print(f"## ELIGIBLE {len(eligible)} of {len(res['comps'])}")
    print("## reason histogram (non-eligible)")
    for r, n in sorted(reasons.items(), key=lambda kv: (-kv[1], kv[0])):
        print(f"{n:7d} {r}")
    if notes:
        print("## secondary measurement on the non-eligible (does NOT affect the count)")
        for r, n in sorted(notes.items(), key=lambda kv: (-kv[1], kv[0])):
            print(f"{n:7d} {r}")
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
