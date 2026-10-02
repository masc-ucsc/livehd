#!/usr/bin/env python3
"""Do the fields of a packed-aggregate cycle OVERLAP, or not?

MEASUREMENT ONLY.  Nothing here rewrites a graph; it answers the one question a
`split_selfref` rule for `sra`/`sext`-shaped field reads turns on.

A packed net assembled by OR-ing disjoint ranges, read back through a slice of
itself, is a WORD-level cycle and may be no cycle at all: if word A's bits
[32,48) come from word B's bits [32,48) while B's [16,32) come from A's [16,32),
no bit depends on itself and the loop is an artifact of looking at whole words.
If the ranges touch, the dependency is real and the refusal is correct.

So this resolves the question at BIT granularity.  For every edge into a packed
`or`, it walks the chain back -- get_mask / shl / sra / sext -- building an
explicit map from destination bit to source bit, then asks whether any (word,
bit) reaches itself.

FAIL-CLOSED.  An op it does not model, a non-constant shift, a mask that is not
all-ones, a slice crossing a sign-extension boundary or running past the
operand -- each marks the edge UNKNOWN, and an SCC with any unknown edge is
reported UNKNOWN rather than disjoint.  "I could not follow it" must never read
as "it is safe".

Input: `lhd tool cat --target all --max 0 --diag-fmt jsonl` (needs the `consts`
column, which prints every constant feeding a node's sink pins).
"""
import argparse
import json
import os
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lgraph_scc as L  # noqa: E402

PACK_OPS = {"or"}            # the packed accumulators
WALK_OPS = {"get_mask", "shl", "sra", "sext"}


def parse_consts(s):
    """`p1=16,p2=-1` -> {'p1': 16, 'p2': -1}; a value we cannot read is dropped,
    which later shows up as a missing operand and marks the edge unknown."""
    out = {}
    if not s:
        return out
    for item in s.split(","):
        k, _, v = item.partition("=")
        try:
            out[k.strip()] = int(v.strip(), 0)
        except ValueError:
            pass
    return out


class Walker:
    """Bit-level back-walk from a packed `or`.

    A result is a map `dst_bit -> set of (src_or_nid, src_bit)`: the OR of
    several operands can make one destination bit depend on several sources, and
    sign replication makes a whole range depend on ONE bit.  Fill bits -- a
    zero-extension's top bit, a shift's vacated low bits -- depend on nothing
    and are simply absent.

    Modelling the fills is the point.  Treating `get_mask(x,-1)` as a plain
    pass-through made every read of a 16-bit sign-extended field "run past its
    sign position", because zero-extension widens 16 bits to 17 and the walker
    then asked the sext for a bit it does not have.  All seven SCCs came back
    UNKNOWN for that reason alone.
    """

    def __init__(self, nodes, consts, width, drivers):
        self.nodes = nodes
        self.consts = consts
        self.width = width
        self.drivers = drivers

    def op(self, nid):
        return self.nodes[nid]["kind"]

    def operands(self, nid):
        return [d for d in self.drivers.get(nid, [])]

    def trace(self, nid, want, depth=0):
        """-> (map, None) or (None, reason).  `map` is {dst_bit: {(or_nid, bit)}}
        over bits [0, want) of `nid`'s value."""
        if depth > 24:
            return None, "chain deeper than 24 reshaping nodes"
        kind = self.op(nid)
        w = self.width.get(nid, 0)
        if kind in PACK_OPS:
            return {i: {(nid, i)} for i in range(min(want, w or want))}, None
        if kind not in WALK_OPS:
            return None, f"op `{kind}` is not a modelled reshaping op"
        c = self.consts.get(nid, {})
        ops = self.operands(nid)
        if len(ops) != 1:
            return None, f"{kind}_{nid} has {len(ops)} non-constant operands, not 1"
        src = ops[0][0]
        sw = self.width.get(src, 0)
        if sw <= 0:
            return None, f"{kind}_{nid}'s operand {self.op(src)}_{src} has no known width"

        def sub(n):
            return self.trace(src, n, depth + 1)

        if kind == "get_mask":
            mask = c.get("p2", c.get("b"))
            if mask is None:
                return None, f"get_mask_{nid} has a non-constant mask"
            if mask != -1:
                return None, f"get_mask_{nid} mask is {mask}, not all-ones (-1)"
            # zext: operand bits below its width, ZERO above (no dependency).
            m, err = sub(min(want, sw))
            if m is None:
                return None, err
            return {i: v for i, v in m.items() if i < want}, None

        if kind == "shl":
            sh = c.get("p1", c.get("b"))
            if sh is None:
                return None, f"shl_{nid} has a non-constant shift"
            if sh < 0:
                return None, f"shl_{nid} shift is negative ({sh})"
            m, err = sub(max(0, min(want - sh, sw)))
            if m is None:
                return None, err
            return {i + sh: v for i, v in m.items() if i + sh < want}, None

        if kind == "sra":
            sh = c.get("p1", c.get("b"))
            if sh is None:
                return None, f"sra_{nid} has a non-constant shift"
            if sh < 0:
                return None, f"sra_{nid} shift is negative ({sh})"
            m, err = sub(min(sh + want, sw))
            if m is None:
                return None, err
            out = {}
            for i in range(want):
                j = i + sh
                if j < sw:
                    if j in m:
                        out[i] = m[j]
                elif (sw - 1) in m:
                    # arithmetic: above the operand, the replicated TOP bit.
                    # Conservative either way -- if the operand is unsigned these
                    # bits are zero, and claiming a dependency can only make the
                    # verdict stricter, never falsely disjoint.
                    out[i] = m[sw - 1]
            return out, None

        if kind == "sext":
            n = c.get("p1", c.get("b"))
            if n is None:
                return None, f"sext_{nid} has a non-constant sign position"
            if n <= 0:
                return None, f"sext_{nid} sign position is {n}"
            if n > sw:
                return None, f"sext_{nid} sign position {n} is past its {sw}-bit operand"
            m, err = sub(n)
            if m is None:
                return None, err
            out = {}
            for i in range(want):
                if i < n:
                    if i in m:
                        out[i] = m[i]
                elif (n - 1) in m:
                    out[i] = m[n - 1]   # the replicated sign
            return out, None

        return None, f"unhandled op `{kind}`"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("jsonl")
    ap.add_argument("--cut", default="lean", choices=L.CUTS)
    ap.add_argument("--stage", default="")
    args = ap.parse_args()

    a = L.analyse(args.jsonl, args.cut)
    nodes, edges, comps = a["nodes"], a["edges"], a["comps"]
    width = {n: max([b for b in d.values() if b is not None] or [0])
             for n, d in a["driver_pins"].items()}
    raw = {}
    for line in open(args.jsonl):
        r = json.loads(line)
        if r.get("t") == "node":
            raw[r["nid"]] = r.get("consts")
    consts = {n: parse_consts(v) for n, v in raw.items()}

    drivers = defaultdict(list)
    for e in edges:
        drivers[e["s"]].append((e["d"], e["dp"], e["sp"], e["bits"]))

    print(f"PACKED-FIELD INTERVALS stage={args.stage or '?'} cut={args.cut} "
          f"file={args.jsonl}")
    print(f"  {len(comps)} SCC(s)\n")
    verdicts = {}
    for i, comp in enumerate(comps):
        members = set(comp)
        packs = sorted(n for n in comp if nodes[n]["kind"] in PACK_OPS)
        w = Walker(nodes, consts, width, drivers)
        bitedges = {}        # (dst_or, dst_bit) -> (src_or, src_bit)
        unknown = []
        bitedges = defaultdict(set)
        for dst in packs:
            for (src_n, _dp, _sp, _bits) in drivers.get(dst, []):
                if src_n not in members:
                    continue          # an off-cycle operand carries no loop
                m, err = w.trace(src_n, width.get(dst, 0))
                if m is None:
                    unknown.append(f"{nodes[src_n]['kind']}_{src_n} -> or_{dst}: {err}")
                    continue
                for d, srcs in m.items():
                    for sn, sb in srcs:
                        if sn in members:
                            bitedges[(dst, d)].add((sn, sb))
        # Does any (word, bit) reach itself?  Transitive closure, because a
        # destination bit can depend on several source bits.
        loops = []
        for startkey in list(bitedges):
            seen, stack = set(), [startkey]
            hit = False
            while stack:
                cur = stack.pop()
                for nxt in bitedges.get(cur, ()):
                    if nxt == startkey:
                        hit = True
                        stack = []
                        break
                    if nxt not in seen:
                        seen.add(nxt)
                        stack.append(nxt)
            if hit:
                loops.append(startkey)
        if unknown:
            verdict = "UNKNOWN"
        elif loops:
            verdict = "OVERLAPPING"
        elif bitedges:
            verdict = "DISJOINT"
        else:
            verdict = "NO-PACK-EDGE"
        verdicts[i] = verdict
        print(f"--- SCC #{i} size={len(comp)} packs={len(packs)} verdict={verdict}")
        # CONTIGUOUS RUNS, not min..max.  A packed word's dependency is usually
        # SPARSE -- [0,16) and [32,48) with a hole in the middle -- and printing
        # min..max renders that as [0,48), which says the opposite of what the
        # bits say and would have made a disjoint result look overlapping.
        ivs = defaultdict(set)
        for (dn, db), srcs in bitedges.items():
            for (sn, sb) in srcs:
                ivs[(dn, sn, db - sb)].add(db)
        for (dn, sn, off), bs in sorted(ivs.items()):
            runs, lo = [], None
            prev = None
            for b in sorted(bs):
                if lo is None:
                    lo = b
                elif b != prev + 1:
                    runs.append((lo, prev + 1))
                    lo = b
                prev = b
            if lo is not None:
                runs.append((lo, prev + 1))
            covered = sum(hi - l for l, hi in runs)
            assert covered == len(bs), "run decomposition lost a bit"
            for (l, hi) in runs:
                print(f"      or_{dn} bits [{l},{hi}) <- or_{sn} bits [{l - off},{hi - off})")
        for u in unknown[:6]:
            print(f"      UNKNOWN: {u}")
        for lp in loops[:4]:
            print(f"      SELF-DEPENDENT BIT: or_{lp[0]} bit {lp[1]}")
        print()

    from collections import Counter
    c = Counter(verdicts.values())
    print("SUMMARY " + "  ".join(f"{k}={v}" for k, v in sorted(c.items())))
    # Only an all-disjoint result licenses a dissolution rule.
    return 0 if set(verdicts.values()) <= {"DISJOINT", "NO-PACK-EDGE"} else 1


if __name__ == "__main__":
    sys.exit(main())
