#!/usr/bin/env python3
"""DCERT2 delta analyzer -- READ-ONLY and FAIL-CLOSED.

Measures how much a certificate changed between two emissions.  It exists to
answer ONE question that `DIRECTION4_INCREMENTAL.md` section 6 item 6 left
open: "how big is a typical delta?".  Nothing here makes anything incremental;
it only measures churn.

THE GRAMMAR IS THE NORMATIVE ONE.  `Compiler.CertIO.renderCert` defines the
wire format and this reader follows it field for field:

    DCERT2
    <nSources>  then per source, a tag and its fields:
       0 idx width                                   input
       1 width value                                 const
       2 idx width                                   flopQ
       3 idx width resetInput resetValue activeLow   flopQAsync
       4 idx aw dw                                   memImg
       5 aw dw n v0 .. v(n-1)                        memConst
    <nNodes>    then per node:    opCode opArg width nDeps d0 .. origin
    <nOutputs>  then per output:  slot width
    <nFlops>    then per flop:
                  width din hasEn en hasRst rst resetValue activeLow clock asyncReset
    <nMemories> then per memory:  aw dw nextImg clock
    <nClocks>   then per clock:   <nameBytes> <name>

RESTORED from pinned D4 f6ad1fe29f8f15aeda045115864fd81a34c33aef and adapted to
DCERT2. The flop record gained `clock` and `asyncReset`, the memory record
gained `clock`, and the clocks section is new. A historical DCERT1 file is
REFUSED by magic -- never re-read as a one-clock design, which would invent a
domain the bytes do not state.

The clock NAME forced one structural change: it is a byte count, one space and
exactly that many raw bytes, so a name may contain a space or a newline and the
whitespace tokenizer this reader used cannot see the file. The cursor now walks
BYTE OFFSETS, skipping whitespace exactly where `Compiler.CertIO.skipWs` does.
Everything the tokenizer refused, it still refuses.

Slots are a single space: sources occupy [0, nSources) and nodes occupy
[nSources, nSources + nNodes).  Every dependency, output `slot`, flop
`din`/`en`/`rst` and memory `nextImg` names one.

TWO METRICS, AND THE DIFFERENCE BETWEEN THEM IS THE POINT.

  ORDINAL churn compares records at the same position.  It is what a reader
  who diffed the files would see, and it is NOT renumbering-independent: moving
  one node down the slot space rewrites every record that referenced it.

  STRUCTURAL churn compares multisets of canonical signatures.  A slot's
  signature is built from its record kind, its payload, its width/op, and --
  RECURSIVELY -- the signatures of its dependencies, never their slot numbers.
  So a consistent renumbering is invisible to it, while a changed operator or a
  changed live dependency is not.  Duplicate signatures are COUNTED, never
  collapsed: two identical subtrees are two records, and a design that gains a
  second copy of an existing subtree has genuinely grown.

WHY FAIL-CLOSED MATTERS HERE.  A reader that skips a record it cannot parse
reports a SMALLER delta, which is the direction that flatters the result.  So
every refusal below exits non-zero and publishes nothing: truncation, a count
that does not match the records that follow, a dependency naming a slot outside
the design, an unknown source tag or operator code, a non-decimal token, and
any trailing byte after the last declared record.
"""

import argparse
import hashlib
import json
import sys
from collections import Counter

MAGIC = b"DCERT2\n"
MAX_OPCODE = 28  # Op_MemWriteBE; `CertIO.opOfCode` returns none above this.


class Refused(Exception):
    """A certificate this reader will not claim to have understood."""


WS = b" \n\r\t"


class Cursor:
    """Decimal tokens at BYTE offsets, with position for diagnostics.

    Offsets rather than `blob.split()`, because a clock name is taken by COUNT
    and may contain a space or a newline: a tokenizer would shred it and then
    mis-read every record after it. Whitespace is skipped exactly where
    `Compiler.CertIO.skipWs` skips it, over the same four bytes.
    """

    def __init__(self, blob: bytes, path: str):
        self.path = path
        self.blob = blob
        if not blob.startswith(MAGIC):
            raise Refused(f"{path}: bad magic: expected DCERT2")
        # Past "DCERT2"; the magic's newline is leading whitespace to the first
        # read, which is how the Lean reader starts too.
        self.i = len(MAGIC) - 1

    def _skip_ws(self) -> None:
        while self.i < len(self.blob) and self.blob[self.i] in WS:
            self.i += 1

    def _next(self, what: str) -> bytes:
        self._skip_ws()
        start = self.i
        while self.i < len(self.blob) and self.blob[self.i] not in WS:
            self.i += 1
        if self.i == start:
            raise Refused(f"{self.path}: truncated: ran out of input reading {what}")
        return self.blob[start:self.i]

    def name(self, what: str) -> bytes:
        """A length-prefixed raw name: the count, exactly ONE space -- CHECKED,
        not assumed -- then exactly that many bytes, taken without scanning."""
        n = self.nat(f"{what} length")
        if self.i >= len(self.blob):
            raise Refused(f"{self.path}: {what}: truncated before its separator")
        if self.blob[self.i] != 0x20:
            raise Refused(
                f"{self.path}: {what}: separator is "
                f"0x{self.blob[self.i]:02x}, not the single space renderCert writes"
            )
        self.i += 1
        if self.i + n > len(self.blob):
            raise Refused(f"{self.path}: {what}: declares {n} bytes but only "
                          f"{len(self.blob) - self.i} remain")
        raw = self.blob[self.i:self.i + n]
        self.i += n
        try:
            raw.decode("utf-8")
        except UnicodeDecodeError as e:
            raise Refused(f"{self.path}: {what}: not valid UTF-8: {e}")
        return raw

    def int(self, what: str) -> int:
        """A signed decimal in the ONE spelling `renderCert` emits.

        `natBytes`/`intBytes` produce no leading zeros, no `-0`, no sign on a
        positive, and no `Int.ofNat`.  Anything else is a token this format
        cannot have produced, and reading it would let the structural metric
        report a distinction the writer cannot express.  Textual throughout, so
        a 200-bit constant survives verbatim.
        """
        t = self._next(what)
        neg = t[:1] == b"-"
        s = t[1:] if neg else t
        if not s or not s.isdigit():
            raise Refused(f"{self.path}: {what}: `{t.decode('utf-8', 'replace')}` is not a decimal integer")
        if len(s) > 1 and s[:1] == b"0":
            raise Refused(f"{self.path}: {what}: `{t.decode()}` has a leading zero; renderCert emits none")
        if neg and s == b"0":
            raise Refused(f"{self.path}: {what}: `-0` is not canonical; renderCert emits `0`")
        return int(t)

    def nat(self, what: str) -> int:
        """A field the format types as `Nat`.  `natBytes` cannot emit a sign, so
        a negative here is not a small number -- it is a record `renderCert`
        could not have written."""
        v = self.int(what)
        if v < 0:
            raise Refused(f"{self.path}: {what}: {v} is negative, but this field is a Nat")
        return v

    def bit(self, what: str) -> int:
        """`if a then 1 else 0` -- nothing else is in the image of the writer."""
        v = self.nat(what)
        if v not in (0, 1):
            raise Refused(f"{self.path}: {what}: {v} is not the 0/1 a boolean field is written as")
        return v

    def count(self, what: str) -> int:
        """A section count.  Bounded by the bytes that remain -- no record is
        shorter than one byte -- so a declared count of 10^9 is refused here
        rather than after a long allocation."""
        n = self.nat(what)
        if n > len(self.blob) - self.i:
            raise Refused(
                f"{self.path}: {what} declares {n} records but only "
                f"{len(self.blob) - self.i} bytes remain"
            )
        return n

    def done(self) -> None:
        self._skip_ws()
        if self.i != len(self.blob):
            raise Refused(
                f"{self.path}: {len(self.blob) - self.i} trailing byte(s) after the "
                f"last declared record; the counts do not describe this file"
            )


# ---------------------------------------------------------------------------
# Reader
# ---------------------------------------------------------------------------

# Per tag: the name, and ONE field class per field, in order.
#   n = Nat, i = signed Int, b = 0/1 boolean
SRC_FIELDS = {0: ("input", "nn"), 1: ("const", "ni"), 2: ("flopQ", "nn"),
              3: ("flopQAsync", "nnnib"), 4: ("memImg", "nnn")}

# The operators whose payload slot carries anything.  `opCode` writes `0` for
# every other operator, and `opOfCode` would push a negative through
# `Int.toNat` for the two Nat-typed ones -- so a negative there is a record
# that means something different to the reader than to the writer.
OP_ARG_SIGNED = {0}            # Op_Const c      : Int
OP_ARG_NAT = {1, 28}           # Op_Sum n, Op_MemWriteBE b : Nat


def read_cert(path: str) -> dict:
    try:
        with open(path, "rb") as fh:
            blob = fh.read()
    except OSError as e:
        raise Refused(f"{path}: cannot read: {e}")

    c = Cursor(blob, path)

    n_src = c.count("nSources")
    sources = []
    for k in range(n_src):
        tag = c.nat(f"source {k} tag")
        if tag in SRC_FIELDS:
            name, classes = SRC_FIELDS[tag]
            rd = {"n": c.nat, "i": c.int, "b": c.bit}
            fields = [rd[cl](f"source {k} ({name}) field {j}") for j, cl in enumerate(classes)]
        elif tag == 5:
            name = "memConst"
            aw = c.nat(f"source {k} (memConst) aw")
            dw = c.nat(f"source {k} (memConst) dw")
            n = c.count(f"source {k} (memConst) length")
            fields = [aw, dw, n] + [c.int(f"source {k} (memConst) value {j}") for j in range(n)]
        else:
            raise Refused(f"{path}: source {k}: unknown tag {tag} (the format defines 0..5)")
        sources.append((tag, name, tuple(fields)))

    n_node = c.count("nNodes")
    n_slots = n_src + n_node
    nodes = []
    for k in range(n_node):
        op = c.nat(f"node {k} opCode")
        if op > MAX_OPCODE:
            raise Refused(f"{path}: node {k}: opCode {op} is not an operator (0..{MAX_OPCODE})")
        # The payload slot is always present, but only three operators may put
        # anything in it.  A stray payload on the other twenty-six is a token
        # `opCode` never writes, and treating it as data would let the
        # structural metric separate two records that mean the same design.
        if op in OP_ARG_SIGNED:
            arg = c.int(f"node {k} opArg")
        elif op in OP_ARG_NAT:
            arg = c.nat(f"node {k} opArg")
        else:
            arg = c.int(f"node {k} opArg")
            if arg != 0:
                raise Refused(
                    f"{path}: node {k}: opCode {op} carries no payload, but its "
                    f"opArg is {arg}; renderCert writes 0 there"
                )
        width = c.nat(f"node {k} width")
        nd = c.count(f"node {k} nDeps")
        deps = []
        for j in range(nd):
            d = c.nat(f"node {k} dep {j}")
            if d >= n_slots:
                raise Refused(
                    f"{path}: node {k} dep {j} names slot {d}, outside the "
                    f"{n_slots}-slot space ({n_src} sources + {n_node} nodes)"
                )
            deps.append(d)
        origin = c.nat(f"node {k} origin")
        nodes.append((op, arg, width, tuple(deps), origin))

    def slot(what: str) -> int:
        s = c.nat(what)
        if s >= n_slots:
            raise Refused(f"{path}: {what} names slot {s}, outside the {n_slots}-slot space")
        return s

    n_out = c.count("nOutputs")
    outputs = [(slot(f"output {k} slot"), c.nat(f"output {k} width")) for k in range(n_out)]

    n_flop = c.count("nFlops")
    flops = []
    for k in range(n_flop):
        width = c.nat(f"flop {k} width")
        din = slot(f"flop {k} din")
        has_en = c.bit(f"flop {k} hasEn")
        en = slot(f"flop {k} en")
        has_rst = c.bit(f"flop {k} hasRst")
        rst = slot(f"flop {k} rst")
        rv = c.int(f"flop {k} resetValue")
        alow = c.bit(f"flop {k} activeLow")
        # DCERT2. Written unconditionally, so the record has a fixed arity
        # whatever the metadata says; the ordinal is bounds-checked against the
        # clocks section once that section has been read.
        clk = c.nat(f"flop {k} clock")
        arst = c.bit(f"flop {k} asyncReset")
        # `flopBody` writes the pair `(0, 0)` for an absent pin, so a nonzero
        # slot behind a 0 presence flag is an absent placeholder spelled a way
        # the writer never spells it -- and the reader would ignore the slot.
        for flag, val, nm in ((has_en, en, "en"), (has_rst, rst, "rst")):
            if flag == 0 and val != 0:
                raise Refused(
                    f"{path}: flop {k}: {nm} is {val} with its presence flag 0; "
                    f"renderCert writes slot 0 for an absent pin"
                )
        flops.append((width, din, has_en, en, has_rst, rst, rv, alow, clk, arst))

    n_mem = c.count("nMemories")
    mems = [(c.nat(f"memory {k} aw"), c.nat(f"memory {k} dw"), slot(f"memory {k} nextImg"),
             c.nat(f"memory {k} clock"))
            for k in range(n_mem)]

    # DCERT2's new section, LAST because it is the one this version added.
    n_clk = c.count("nClocks")
    clocks = [c.name(f"clock {k} name") for k in range(n_clk)]

    # A clock ORDINAL outside the declared table is a record this reader cannot
    # describe -- the design it names does not exist. This is the delta
    # analyser's own refusal, not a claim about `certWfError`: the CODEC admits
    # such a certificate by design, and semantic admission is a separate layer.
    # A churn figure computed over a domain that is not there would be a number
    # about nothing.
    for k, f in enumerate(flops):
        if f[8] >= n_clk:
            raise Refused(f"{path}: flop {k} commits on clock ordinal {f[8]}, "
                          f"outside the {n_clk} declared")
    for k, m in enumerate(mems):
        if m[3] >= n_clk:
            raise Refused(f"{path}: memory {k} is on clock ordinal {m[3]}, "
                          f"outside the {n_clk} declared")

    c.done()
    cert = {"path": path, "bytes": len(blob), "sources": sources, "nodes": nodes,
            "outputs": outputs, "flops": flops, "memories": mems, "clocks": clocks,
            "n_slots": n_slots}

    # THE COMPLETE-FORMAT CHECK.  The field rules above each give a precise
    # diagnostic, but they are a list and a list can be short.  This closes it:
    # if re-rendering the parsed structure does not reproduce the input byte for
    # byte, then something in the file is outside the image of `renderCert` --
    # a separator, a newline, a spelling -- and this reader has no licence to
    # describe it.
    again = render_cert(cert)
    if again != blob:
        n = min(len(again), len(blob))
        at = next((j for j in range(n) if again[j] != blob[j]), n)
        raise Refused(
            f"{path}: not canonical DCERT2: re-rendering the parsed certificate "
            f"differs from the input at byte {at} "
            f"({len(blob)} bytes in, {len(again)} out)"
        )
    return cert


def render_cert(c: dict) -> bytes:
    """`Compiler.CertIO.renderCert`, field for field: single spaces inside a
    record, '\\n' after every record, each section count on its own line.

    Pure -- it writes nothing.  It exists so `read_cert` can prove it understood
    the whole file, and `dcert_mutate.py` imports THIS one so there is a single
    renderer rather than two that can drift apart.
    """
    out = [MAGIC]

    def rec(*fs):
        out.append(" ".join(str(f) for f in fs).encode() + b"\n")

    out.append(f"{len(c['sources'])}\n".encode())
    for tag, _name, fields in c["sources"]:
        rec(tag, *fields)
    out.append(f"{len(c['nodes'])}\n".encode())
    for (op, arg, width, deps, origin) in c["nodes"]:
        rec(op, arg, width, len(deps), *deps, origin)
    out.append(f"{len(c['outputs'])}\n".encode())
    for o in c["outputs"]:
        rec(*o)
    out.append(f"{len(c['flops'])}\n".encode())
    for f in c["flops"]:
        rec(*f)
    out.append(f"{len(c['memories'])}\n".encode())
    for m in c["memories"]:
        rec(*m)
    out.append(f"{len(c['clocks'])}\n".encode())
    for nm in c["clocks"]:
        # Raw, by count: no escaping and no re-encoding, so a name containing a
        # space or a newline comes back exactly as it went in.
        out.append(str(len(nm)).encode() + b" " + nm + b"\n")
    return b"".join(out)


# ---------------------------------------------------------------------------
# Canonical signatures -- the renumbering-independent half
# ---------------------------------------------------------------------------

def _h(*parts) -> str:
    m = hashlib.sha256()
    for p in parts:
        m.update(str(p).encode())
        m.update(b"\x1f")
    return m.hexdigest()[:32]


def slot_signatures(cert: dict) -> list:
    """One canonical signature per slot.

    Built bottom-up with an EXPLICIT stack: `csr_regfile` is ~35 k nodes and a
    recursive walk would hit the interpreter limit, which is a crash dressed up
    as a result.  `origin` is deliberately NOT in the signature -- it is the
    emitter's provenance handle, not design meaning, and including it would
    report churn for a renumbering of the LGraph rather than of the design.
    """
    n_src = len(cert["sources"])
    sig = [None] * cert["n_slots"]

    for i, (tag, name, fields) in enumerate(cert["sources"]):
        sig[i] = _h("S", tag, name, *fields)

    nodes = cert["nodes"]
    # 0 = unvisited, 1 = on the stack (its children are being computed), 2 = done
    state = [0] * len(nodes)
    for root in range(len(nodes)):
        if state[root] == 2:
            continue
        stack = [root]
        while stack:
            k = stack[-1]
            if state[k] == 2:
                stack.pop()
                continue
            op, arg, width, deps, _origin = nodes[k]
            pending = [d - n_src for d in deps if d >= n_src and state[d - n_src] != 2]
            if pending:
                if state[k] == 1:
                    # Its children were already expanded once and one is still
                    # unfinished: the only way back here is through a cycle.
                    raise Refused(
                        f"{cert['path']}: dependency cycle reaching node {k}; "
                        f"no evaluation order exists and no signature can be built"
                    )
                state[k] = 1
                stack.extend(pending)
                continue
            sig[n_src + k] = _h("N", op, arg, width, len(deps), *[sig[d] for d in deps])
            state[k] = 2
            stack.pop()
    return sig


def structural_multiset(cert: dict, sig: list) -> Counter:
    """Every record as a kind-tagged canonical signature.  Slot references are
    replaced by the signature of what they reference, so nothing in here can see
    a slot number."""
    c = Counter(sig)  # one entry per source and per node
    # A clock domain is referenced by ORDINAL, which is a position handle just
    # like a slot number: reordering the clock table is not a design change,
    # and moving a flop to a different domain is. So the domain enters a
    # signature through its NAME, exactly as a slot enters through the
    # signature of what it references. Leaving the domain out altogether --
    # which the DCERT1 version necessarily did, having no domains -- would
    # report ZERO structural churn for a design whose flops moved to another
    # clock, the one direction that flatters the result.
    def ck(k):
        return _h("CK", cert["clocks"][k].decode("utf-8", "replace"))

    for s, w in cert["outputs"]:
        c[_h("O", w, sig[s])] += 1
    for (w, din, he, en, hr, rst, rv, alow, clk, arst) in cert["flops"]:
        c[_h("F", w, sig[din], he, sig[en] if he else "-", hr,
             sig[rst] if hr else "-", rv, alow, ck(clk), arst)] += 1
    for (aw, dw, nxt, mclk) in cert["memories"]:
        c[_h("M", aw, dw, sig[nxt], ck(mclk))] += 1
    # The table itself is records too: a design that declares a domain nothing
    # commits on still declares it, and losing one is a change.
    for nm in cert["clocks"]:
        c[_h("C", nm.decode("utf-8", "replace"))] += 1
    return c


# ---------------------------------------------------------------------------
# Ordinal records -- the position-sensitive half
# ---------------------------------------------------------------------------

def ordinal_records(cert: dict) -> list:
    """The flat record sequence in file order, rendered canonically.  Slot
    numbers are kept VERBATIM here: that is exactly what makes this metric
    sensitive to renumbering."""
    out = []
    for tag, name, fields in cert["sources"]:
        out.append(("src", tag, fields))
    for (op, arg, width, deps, origin) in cert["nodes"]:
        out.append(("node", op, arg, width, deps, origin))
    out.extend(("out",) + o for o in cert["outputs"])
    out.extend(("flop",) + f for f in cert["flops"])
    out.extend(("mem",) + m for m in cert["memories"])
    out.extend(("clock", nm) for nm in cert["clocks"])
    return out


def section_delta(a: list, b: list) -> dict:
    n = min(len(a), len(b))
    changed = sum(1 for i in range(n) if a[i] != b[i])
    return {"a": len(a), "b": len(b), "same_slot_changed": changed,
            "inserted": max(0, len(b) - len(a)), "deleted": max(0, len(a) - len(b))}


def common_affixes(a: list, b: list) -> tuple:
    n = min(len(a), len(b))
    pre = 0
    while pre < n and a[pre] == b[pre]:
        pre += 1
    suf = 0
    while suf < n - pre and a[len(a) - 1 - suf] == b[len(b) - 1 - suf]:
        suf += 1
    return pre, suf


def pct(num: int, den: int) -> float:
    return 0.0 if den == 0 else round(100.0 * num / den, 4)


# ---------------------------------------------------------------------------

def analyze(pa: str, pb: str) -> dict:
    A, B = read_cert(pa), read_cert(pb)
    sa, sb = slot_signatures(A), slot_signatures(B)
    ma, mb = structural_multiset(A, sa), structural_multiset(B, sb)

    # Multiset symmetric difference, WITH multiplicity.
    only_a = sum(max(0, ma[k] - mb[k]) for k in ma)
    only_b = sum(max(0, mb[k] - ma[k]) for k in mb)
    tot_a, tot_b = sum(ma.values()), sum(mb.values())

    ra, rb = ordinal_records(A), ordinal_records(B)
    pre, suf = common_affixes(ra, rb)
    ord_changed = sum(1 for i in range(min(len(ra), len(rb))) if ra[i] != rb[i])
    ord_churn = ord_changed + abs(len(ra) - len(rb))

    return {
        "a": {"path": pa, "bytes": A["bytes"], "sources": len(A["sources"]),
              "nodes": len(A["nodes"]), "outputs": len(A["outputs"]),
              "flops": len(A["flops"]), "memories": len(A["memories"]),
              "clocks": len(A["clocks"]),
              "records": len(ra), "signatures": tot_a},
        "b": {"path": pb, "bytes": B["bytes"], "sources": len(B["sources"]),
              "nodes": len(B["nodes"]), "outputs": len(B["outputs"]),
              "flops": len(B["flops"]), "memories": len(B["memories"]),
              "clocks": len(B["clocks"]),
              "records": len(rb), "signatures": tot_b},
        "sections": {k: section_delta([r for r in ra if r[0] == k],
                                      [r for r in rb if r[0] == k])
                     for k in ("src", "node", "out", "flop", "mem", "clock")},
        "ordinal": {
            "common_record_prefix": pre, "common_record_suffix": suf,
            "same_slot_changed": ord_changed,
            "inserted": max(0, len(rb) - len(ra)),
            "deleted": max(0, len(ra) - len(rb)),
            "churn": ord_churn,
            "churn_pct_of_a": pct(ord_churn, len(ra)),
            "churn_pct_of_max": pct(ord_churn, max(len(ra), len(rb))),
        },
        "structural": {
            "only_in_a": only_a, "only_in_b": only_b,
            "churn": only_a + only_b,
            "churn_pct_of_a": pct(only_a + only_b, tot_a),
            "churn_pct_of_max": pct(only_a + only_b, max(tot_a, tot_b)),
            "distinct_signatures_a": len(ma), "distinct_signatures_b": len(mb),
            "duplicate_bearing_signatures_a": sum(1 for v in ma.values() if v > 1),
            "duplicate_bearing_signatures_b": sum(1 for v in mb.values() if v > 1),
        },
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="DCERT2 A/B delta (read-only, fail-closed)")
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    args = ap.parse_args()
    try:
        res = analyze(args.a, args.b)
    except Refused as e:
        print(f"REFUSED: {e}", file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps(res, sort_keys=True))
        return 0
    o, s = res["ordinal"], res["structural"]
    print(f"a  {res['a']['sources']} src  {res['a']['nodes']} node  "
          f"{res['a']['flops']} flop  {res['a']['memories']} mem  "
          f"{res['a']['clocks']} clk  {res['a']['bytes']} bytes")
    print(f"b  {res['b']['sources']} src  {res['b']['nodes']} node  "
          f"{res['b']['flops']} flop  {res['b']['memories']} mem  "
          f"{res['b']['clocks']} clk  {res['b']['bytes']} bytes")
    print(f"ordinal     churn {o['churn']}  ({o['churn_pct_of_max']}% of max)  "
          f"prefix {o['common_record_prefix']}  suffix {o['common_record_suffix']}")
    print(f"structural  churn {s['churn']}  ({s['churn_pct_of_max']}% of max)  "
          f"only_a {s['only_in_a']}  only_b {s['only_in_b']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
