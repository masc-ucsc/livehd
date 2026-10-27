#!/usr/bin/env python3
"""Controls-only DCERT2 mutator.  NOT part of the measurement path.

`dcert_delta.py` is read-only by contract, so the negative controls need a
separate tool to produce the inputs they assert on.  This is that tool, and it
is deliberately kept out of the analyzer: a measurement tool that can also
write certificates is one edit away from measuring its own output.

THE RENDERER IS SELF-CHECKED BEFORE IT IS TRUSTED.  Every mutation starts by
re-rendering the UNMODIFIED certificate and comparing it byte-for-byte with the
input.  If that fails nothing is written and the exit is non-zero, because a
renderer that cannot reproduce the original cannot be used to argue that the
only difference in the output is the intended mutation.

Mutations:

  none          re-render unchanged (exercises the self-check alone)
  renumber      swap two adjacent INDEPENDENT node slots and remap every
                reference -- a consistent renumbering, so the design is the same
  op            retag one node's operator
  dep           repoint one dependency of a node that a root actually reaches
  truncate      drop the last N tokens
  count         inflate a section count without adding records
  oor-dep       point a dependency at a slot outside the design
  token         replace one token with something that is not a decimal
  trailing      append a token after the last declared record
"""

import argparse
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from dcert_delta import MAGIC, Refused, read_cert, render_cert  # noqa: E402


# ONE renderer, imported from the analyzer.  A private copy here could
# drift from the one `read_cert` validates every real certificate against,
# and then a control would be asserting on bytes nothing else accepts.
render = render_cert


def roots(c: dict) -> set:
    s = {o[0] for o in c["outputs"]} | {m[2] for m in c["memories"]}
    for (_w, din, he, en, hr, rst, _rv, _al, _ck, _ar) in c["flops"]:
        s.add(din)
        if he:
            s.add(en)
        if hr:
            s.add(rst)
    return s


def reachable(c: dict) -> set:
    """Slots a root actually reaches.  A dependency mutation on anything else
    would be invisible to a digest and is not what the control claims to test."""
    n_src = len(c["sources"])
    seen, stack = set(), list(roots(c))
    while stack:
        s = stack.pop()
        if s in seen:
            continue
        seen.add(s)
        if s >= n_src:
            stack.extend(c["nodes"][s - n_src][3])
    return seen


def remap(c: dict, f) -> dict:
    """Apply a slot renaming to every reference in the certificate."""
    c["nodes"] = [(op, arg, w, tuple(f(d) for d in deps), org)
                  for (op, arg, w, deps, org) in c["nodes"]]
    c["outputs"] = [(f(s), w) for (s, w) in c["outputs"]]
    # Clock ordinals are NOT slots and are not remapped: renumbering the slot
    # space must leave the domain a record commits on exactly where it was.
    c["flops"] = [(w, f(din), he, f(en), hr, f(rst), rv, al, ck, ar)
                  for (w, din, he, en, hr, rst, rv, al, ck, ar) in c["flops"]]
    c["memories"] = [(aw, dw, f(nxt), ck) for (aw, dw, nxt, ck) in c["memories"]]
    return c


def mutate(c: dict, kind: str) -> bytes:
    n_src = len(c["sources"])
    blob = render(c)

    if kind == "none":
        return blob

    if kind == "renumber-many":
        # EVERY disjoint adjacent independent pair, swapped in one pass.  Each
        # swap is individually order-preserving (the lower node's deps are all
        # below it, and the upper one was checked not to depend on it), and the
        # pairs are disjoint, so the product is still a legal evaluation order.
        # This is the renumbering control at scale: a single swap would show the
        # property on four records and say nothing about 35,000.
        perm = {}
        i = 0
        while i < len(c["nodes"]) - 1:
            a, b = n_src + i, n_src + i + 1
            if a in c["nodes"][i + 1][3] or b in c["nodes"][i][3]:
                i += 1
                continue
            c["nodes"][i], c["nodes"][i + 1] = c["nodes"][i + 1], c["nodes"][i]
            perm[a], perm[b] = b, a
            i += 2
        if not perm:
            raise Refused("no adjacent independent node pair to swap")
        return render(remap(c, lambda d: perm.get(d, d)))

    if kind == "renumber":
        # Adjacent node slots i, i+1 with no dependency either way: swapping
        # them keeps every evaluation order valid, so the DESIGN is unchanged
        # and only the slot numbering moves.
        for i in range(len(c["nodes"]) - 1):
            a, b = n_src + i, n_src + i + 1
            if a in c["nodes"][i + 1][3] or b in c["nodes"][i][3]:
                continue
            c["nodes"][i], c["nodes"][i + 1] = c["nodes"][i + 1], c["nodes"][i]
            return render(remap(c, lambda d: b if d == a else (a if d == b else d)))
        raise Refused("no adjacent independent node pair to swap")

    if kind == "op":
        live = reachable(c)
        for i, (op, arg, w, deps, org) in enumerate(c["nodes"]):
            if n_src + i in live and len(deps) == 2 and op in (7, 8, 9):  # And/Or/Xor
                c["nodes"][i] = (7 if op != 7 else 8, arg, w, deps, org)
                return render(c)
        raise Refused("no live two-input And/Or/Xor node to retag")

    if kind == "dep":
        live = reachable(c)
        for i, (op, arg, w, deps, org) in enumerate(c["nodes"]):
            if n_src + i not in live or not deps:
                continue
            alt = next((d for d in range(n_src + i) if d != deps[0]), None)
            if alt is None:
                continue
            c["nodes"][i] = (op, arg, w, (alt,) + deps[1:], org)
            return render(c)
        raise Refused("no live node with a repointable dependency")

    if kind == "dup":
        # A second copy of a node that already exists.  Its canonical signature
        # is IDENTICAL to the original's, so an analyzer that collapsed
        # duplicates would report no change at all for a design that grew.
        c["nodes"].append(c["nodes"][-1])
        return render(c)

    if kind == "cycle":
        # A self-loop: no evaluation order exists, so no signature can be built.
        for i, (op, arg, w, deps, org) in enumerate(c["nodes"]):
            if deps:
                c["nodes"][i] = (op, arg, w, (n_src + i,) + deps[1:], org)
                return render(c)
        raise Refused("no node carries a dependency to turn into a self-loop")

    if kind == "oor-dep":
        # Structural, not textual: point a real dependency one slot PAST the end
        # of the design.  `render` does not validate, which is the point -- the
        # control is about whether the READER refuses it.
        for i, (op, arg, w, deps, org) in enumerate(c["nodes"]):
            if deps:
                c["nodes"][i] = (op, arg, w, (c["n_slots"],) + deps[1:], org)
                return render(c)
        raise Refused("no node carries a dependency to put out of range")

    # --- DCERT2 metadata, which DCERT1 could not express -------------------

    if kind == "clock":
        # Move a flop to ANOTHER DECLARED domain. The design changes and the
        # certificate stays well-formed, so an analyzer that leaves the domain
        # out of its signature reports zero churn for a real change.
        if len(c["clocks"]) < 2:
            raise Refused("needs at least two declared clocks to move a flop between them")
        if not c["flops"]:
            raise Refused("no flop to move between clock domains")
        w, din, he, en, hr, rst, rv, al, ck, ar = c["flops"][0]
        c["flops"][0] = (w, din, he, en, hr, rst, rv, al, (ck + 1) % len(c["clocks"]), ar)
        return render(c)

    if kind == "async":
        # Flip the COMMIT-side asynchronous-reset flag. DCERT1 had nowhere to
        # put this, which is why FP needed `asyncOK`; here it is a field.
        if not c["flops"]:
            raise Refused("no flop whose asyncReset flag can be flipped")
        w, din, he, en, hr, rst, rv, al, ck, ar = c["flops"][0]
        c["flops"][0] = (w, din, he, en, hr, rst, rv, al, ck, 1 - ar)
        return render(c)

    if kind == "mem-clock":
        if not c["memories"]:
            raise Refused("no memory whose commit domain can be moved")
        if len(c["clocks"]) < 2:
            raise Refused("needs at least two declared clocks to move a memory between them")
        aw, dw, nxt, ck = c["memories"][0]
        c["memories"][0] = (aw, dw, nxt, (ck + 1) % len(c["clocks"]))
        return render(c)

    if kind == "oor-clock":
        # An ordinal outside the declared table. The CODEC accepts this by
        # design -- it judges nothing -- so the control is about whether the
        # READER of this analyzer, and `certWfError`, refuse it.
        if not c["flops"]:
            raise Refused("no flop to put on an undeclared clock")
        w, din, he, en, hr, rst, rv, al, ck, ar = c["flops"][0]
        c["flops"][0] = (w, din, he, en, hr, rst, rv, al, len(c["clocks"]), ar)
        return render(c)

    if kind == "clock-name":
        if not c["clocks"]:
            raise Refused("no clock whose name can be changed")
        c["clocks"] = [c["clocks"][0] + b"_x"] + c["clocks"][1:]
        return render(c)

    # --- shapes that break the GRAMMAR -------------------------------------
    #
    # Made on BYTES, not on `body.split()`. A clock name is taken by count and
    # may contain a space or a newline, so tokenizing the whole file would
    # shred it and a "mutation" could land inside a name -- producing a
    # different file from the one the control names. Each edit below is
    # located structurally instead.
    if kind == "truncate":
        return blob[:-max(1, len(blob) // 100)]

    if kind == "count":
        # The LAST section's count -- now `clocks` -- inflated with no record
        # to back it. Located by re-rendering with an empty clocks section,
        # which ends in exactly `0\n`, rather than by counting tokens from the
        # end.
        empty = dict(c)
        empty["clocks"] = []
        pref = render(empty)
        assert pref.endswith(b"0\n")
        head = pref[:-2]
        tail = blob[len(head):]
        records = tail.split(b"\n", 1)[1] if b"\n" in tail else b""
        return head + str(len(c["clocks"]) + 1).encode() + b"\n" + records

    if kind == "token":
        # The first token after the magic is `nSources`; a non-decimal there is
        # refused before any record is read.
        head = blob[:len(MAGIC)]
        rest = blob[len(MAGIC):]
        nl = rest.index(b"\n")
        return head + b"banana" + rest[nl:]

    if kind == "trailing":
        return blob + b"7\n"

    raise Refused(f"unknown mutation `{kind}`")


def main() -> int:
    ap = argparse.ArgumentParser(description="Controls-only DCERT2 mutator")
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--kind", required=True)
    args = ap.parse_args()
    try:
        # `read_cert` itself refuses any file whose re-render is not byte-exact,
        # so by the time it returns, `render` is known to reproduce this input.
        # A second comparison here could never fire, and a control that cannot
        # fire is not a control.
        c = read_cert(args.src)
        blob = mutate(c, args.kind)
    except Refused as e:
        print(f"REFUSED: {e}", file=sys.stderr)
        return 2
    with open(args.dst, "wb") as fh:
        fh.write(blob)
    return 0


if __name__ == "__main__":
    sys.exit(main())
