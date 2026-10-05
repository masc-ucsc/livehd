#!/usr/bin/env python3
"""Deterministic native NPN4 library. No ABC, solver or third-party tables.

Enumerate all input permutations/phases and output phases, keeping the first
(lowest) representative. Synthesize bounded Shannon and Davio expressions with
area/depth variants, using canonical structural hashing for DAG pricing. This
covers every function but does not claim optimal networks. Regenerate with:
  python3 pass/usyn/tools/generate_npn4.py
"""
from functools import lru_cache
from itertools import permutations
from pathlib import Path

PERMS = list(permutations(range(4)))
ZERO = ("0",)
VARS = tuple(("v", i) for i in range(4))


def inv(a):
    return a[1] if a[0] == "n" else ("n", a)


def gate(op, a, b):
    if op == "a":
        if a == ZERO or b == ZERO: return ZERO
        if a == inv(ZERO): return b
        if b == inv(ZERO): return a
        if a == b: return a
        if a == inv(b): return ZERO
    else:
        phase = False
        if a[0] == "n": a, phase = a[1], not phase
        if b[0] == "n": b, phase = b[1], not phase
        if a == b: value = ZERO
        elif a == ZERO: value = b
        elif b == ZERO: value = a
        else: value = (op, *sorted((a, b)))
        return inv(value) if phase else value
    return (op, *sorted((a, b)))


def land(a, b): return gate("a", a, b)
def lxor(a, b): return gate("x", a, b)
def lor(a, b): return inv(land(inv(a), inv(b)))


@lru_cache(None)
def price(expr):
    seen = set()
    def visit(e):
        if e[0] == "n": return visit(e[1])
        if e[0] in ("0", "v"): return 0
        seen.add(e)
        return 1 + max(visit(e[1]), visit(e[2]))
    depth = visit(expr)
    return sum(2 if e[0] == "a" else 4 for e in seen), depth


@lru_cache(None)
def synth(truth, variables, depth_mode):
    width = 1 << len(variables)
    if truth == 0: return ZERO
    if truth == (1 << width) - 1: return inv(ZERO)
    # Complement normalization lets NAND/NOR forms share subexpressions.
    if truth & 1: return inv(synth(truth ^ ((1 << width) - 1), variables, depth_mode))
    candidates = []
    for i, variable in enumerate(variables):
        rest = variables[:i] + variables[i + 1:]
        cof = [0, 0]
        for x in range(1 << len(rest)):
            low = x & ((1 << i) - 1)
            old = low | ((x ^ low) << 1)
            for sign in (0, 1): cof[sign] |= ((truth >> (old | (sign << i))) & 1) << x
        f, t = (synth(v, rest, depth_mode) for v in cof)
        s = VARS[variable]
        candidates.append(lor(land(s, t), land(inv(s), f)))
        # Boolean difference is synthesized from its total table, rather than
        # keeping two separately synthesized cofactors in every XOR mux.
        delta = synth(cof[0] ^ cof[1], rest, depth_mode)
        candidates.append(lxor(f, land(s, delta)))
        candidates.append(lxor(t, land(inv(s), delta)))
        if cof[0] & ~cof[1] == 0: candidates.append(lor(f, land(s, t)))
        if cof[1] & ~cof[0] == 0: candidates.append(lor(t, land(inv(s), f)))
    def rank(e):
        cost, depth = price(e)
        return (depth, cost, e) if depth_mode else (cost, depth, e)
    return min(candidates, key=rank)


def evaluate(expr, x):
    if expr[0] == "0": return False
    if expr[0] == "v": return bool(x & (1 << expr[1]))
    if expr[0] == "n": return not evaluate(expr[1], x)
    a, b = evaluate(expr[1], x), evaluate(expr[2], x)
    return a and b if expr[0] == "a" else a != b


def main():
    lookup = [None] * 65536
    representatives = []
    for truth in range(65536):
        if lookup[truth] is not None: continue
        cls = len(representatives)
        representatives.append(truth)
        for pi, perm in enumerate(PERMS):
            for phase in range(16):
                transformed = 0
                for x in range(16):
                    y = sum((((x >> perm[i]) & 1) ^ ((phase >> i) & 1)) << i for i in range(4))
                    transformed |= ((truth >> y) & 1) << x
                for output_phase in (0, 1):
                    fn = transformed ^ (65535 if output_phase else 0)
                    if lookup[fn] is None:
                        lookup[fn] = cls | (pi << 8) | (phase << 13) | (output_phase << 17)
    assert len(representatives) == 222 and all(v is not None for v in lookup)
    steps, records = [], []
    for truth in representatives:
        for mode in (False, True):
            expr = synth(truth, (0, 1, 2, 3), mode)
            assert sum(int(evaluate(expr, x)) << x for x in range(16)) == truth
            ids = {ZERO: 0, **{v: i + 1 for i, v in enumerate(VARS)}}
            offset = len(steps)
            def emit(e):
                if e[0] == "n": return emit(e[1]) ^ 1
                if e not in ids:
                    a, b = emit(e[1]), emit(e[2])
                    steps.append((int(e[0] == "x"), a, b))
                    ids[e] = 5 + len(steps) - offset - 1
                return ids[e] * 2
            root = emit(expr)
            records.append((offset, len(steps) - offset, root))
    lines = ["// Generated by tools/generate_npn4.py. Do not edit by hand.",
             "constexpr std::array<uint32_t, 65536> npn_lookup{{"]
    for i in range(0, len(lookup), 16): lines.append("  " + ", ".join(str(v) for v in lookup[i:i+16]) + ",")
    lines += ["}};", "constexpr std::array<std::array<uint8_t, 4>, 24> npn_permutations{{"]
    lines += ["  {" + ", ".join(map(str, p)) + "}," for p in PERMS]
    lines += ["}};", f"constexpr std::array<Npn_step, {len(steps)}> npn_steps{{{{"]
    lines += ["  {" + ", ".join(map(str, s)) + "}," for s in steps]
    lines += ["}};", "constexpr std::array<Npn_record, 444> npn_records{{"]
    lines += ["  {" + ", ".join(map(str, r)) + "}," for r in records]
    lines += ["}};"]
    path = Path(__file__).resolve().parents[1] / "npn4_data.inc"
    path.write_text("\n".join(lines) + "\n")
    print(f"{path.name}: 222 classes, {len(steps)} gates, max {max(r[1] for r in records)} gates per template")


if __name__ == "__main__": main()
