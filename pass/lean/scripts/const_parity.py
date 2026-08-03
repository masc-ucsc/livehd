#!/usr/bin/env python3
"""Precise static const-parity gate for the step-5 fast-view bridge.

For every node k and every dep d of k that is a CONSTANT source, the fast model's
body for fv_k must contain the cert leaf spelling of d verbatim.  A value-equal but
differently-spelled constant is an unprovable per-node goal for ops where the const
survives into the result (compares inside `decide (_=_)`, MuxN branches, mask &&&).

This is precise (no value heuristics): `0#w` used as a MuxN default branch or a
flop reset is NOT flagged, because it is not a const *source dep*.

Usage: const_parity2.py <generated.lean>   -> exit 0 if parity holds
"""
import re, sys, collections

def main():
    txt = open(sys.argv[1]).read()
    lines = txt.split("\n")

    # 1. const source leaves:  theorem <Top>_src<id> ... = bvenc (<leaf>) := by
    leaves = {}
    for m in re.finditer(r"^theorem \w+_src(\d+) .*?= bvenc \((.+?)\) := by$", txt, re.M):
        leaves[int(m.group(1))] = m.group(2).strip()
    consts = {k: v for k, v in leaves.items() if v.startswith("BitVec.ofInt")}

    # 2. nodeCerts: nid -> deps
    deps = {}
    for m in re.finditer(r"\{ nid := (\d+), op := ([^,]+), width := (\d+), deps := \[([^\]]*)\] \}", txt):
        deps[int(m.group(1))] = (m.group(2).strip(), [int(x) for x in re.findall(r"\d+", m.group(4))])

    # 3. fv bodies: def <Top>_fv<nid> ... :=  <body lines until next def/blank>
    bodies = {}
    cur = None
    for ln in lines:
        m = re.match(r"^def \w+_fv(\d+) ", ln)
        if m:
            cur = int(m.group(1)); bodies[cur] = ""
            if ":=" in ln:
                bodies[cur] += ln.split(":=", 1)[1]
            continue
        if ln.startswith("def ") or ln.startswith("theorem ") or not ln.strip():
            cur = None; continue
        if cur is not None:
            bodies[cur] += " " + ln

    print(f"parsed: const source leaves={len(consts)}  nodes={len(deps)}  fv bodies={len(bodies)}")

    missing = []      # const dep whose leaf spelling is absent from the consumer body
    for nid, (op, dl) in sorted(deps.items()):
        body = bodies.get(nid)
        if body is None:
            continue
        for idx, d in enumerate(dl):
            # Op_Sext's amount operand (index 1) is deliberately absent from the fast
            # body: fast `bv_sext a` ignores it, and the bridge passes it explicitly
            # to sext_bridge / sext_bridge_low.  Not a spelling divergence.
            if op == "LGraphOp.Op_Sext" and idx == 1:
                continue
            if d in consts:
                leaf = consts[d]
                if leaf not in body:
                    # report what the body does contain, to classify the divergence
                    got = re.findall(r"BitVec\.ofInt \d+ \(\(?-?(?:Int\.ofNat )?\d+\)?\)|\b\d+#\d+\b|\(1#\d+ <<< \d+\) - 1#\d+", body)
                    missing.append((nid, op, d, leaf, sorted(set(got))[:3]))

    if missing:
        print(f"\nFAIL: {len(missing)} node(s) whose fast body lacks the cert leaf spelling of a const dep")
        byop = collections.Counter(op for _, op, _, _, _ in missing)
        print("  by op: " + ", ".join(f"{o}={c}" for o, c in byop.most_common()))
        for nid, op, d, leaf, got in missing[:8]:
            print(f"  fv{nid} ({op}) dep src{d}: cert leaf {leaf!r}")
            print(f"      fast body has: {got}")
    else:
        print("\nPASS: every const dep's cert leaf spelling appears verbatim in the consumer's fast body")
    sys.exit(1 if missing else 0)

main()
