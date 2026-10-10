#!/usr/bin/env python3
"""STRUCTURAL FANOUT over a cgen-emitted netlist.

WHY THIS EXISTS.  A front-end A/B table compares emission HASHES.  When a row
changes, "what changed" is a separate question, and the previous unit answered
it by eye: it followed one net one hop, found no reader, and reported twenty
rows as dead logic.  They were not.  On `minion_dcache_reduce` the changed
register reaches `.wr_clock_0` on four memories, two hops further on.

So the claim is computed here instead, and the one property that matters is
that a DEAD verdict cannot come from a parse miss.  Every line of the netlist
must be classified; anything this reader does not understand POISONS the module
(`verdict=UNPARSED`) rather than contributing nothing to a fanout set.

What it reports, per seed register, is whether its transitive fanout reaches
  * a module OUTPUT port,
  * another register's next-state value or commit condition (STATE),
  * a memory instance's clock, data or control port (MEM-*),
and nothing else counts as live.
"""
import os, re, sys, json

IDENT = re.compile(r"\\[^\s]+\s|[A-Za-z_][A-Za-z0-9_$.]*")
NUMBER = re.compile(r"^\d*'[sS]?[bodhBODH][0-9a-fA-FxXzZ_]+$|^\d+$")
KEYWORDS = {
    "module","endmodule","input","output","inout","reg","wire","logic","signed","unsigned",
    "always","always_comb","always_ff","always_latch","begin","end","if","else","case","endcase",
    "posedge","negedge","or","and","assign","parameter","localparam","default","function","endfunction",
    "integer","genvar","generate","endgenerate","initial","for","while","include","timescale","define",
}
# `cgen_memory_multiclock_*` spells the clocks per port; the single-clock
# `cgen_memory_*` wrapper has ONE `clk`. Both are memory clock inputs, and
# missing the second one is how `txfma_top` first read as a bare "INSTANCE".
MEM_IN  = ("wr_addr","wr_enable","wr_clock","wr_din","rd_addr","rd_enable","rd_clock","clk")
MEM_OUT = ("rd_dout",)

SELECT  = re.compile(r"\[[^\]]*\]")
LITERAL = re.compile(r"\d*'[sS]?[bodhBODH][0-9a-fA-FxXzZ_?]+")

def idents(text):
    """Every net READ by `text`.

    Two things this has to get right, because both silently drop fanout edges:
    a bit- or part-select reads its BASE name (`wide[2]` reads `wide`, not a net
    spelled "wide[2]"), and it also reads whatever is INSIDE the select
    (`wide[din +: 2]` reads `din`). Verilog numeric literals are removed first,
    so the `b0` inside `1'b0` is not mistaken for a net of that name.
    """
    text = LITERAL.sub(" ", text)
    chunks = [SELECT.sub(" ", text), " ".join(SELECT.findall(text))]
    out = []
    for chunk in chunks:
        for m in IDENT.finditer(chunk):
            t = m.group(0).strip()
            if not t or t in KEYWORDS or NUMBER.match(t):
                continue
            out.append(t)
    return out

# The EXACT block openers cgen emits: `always_comb begin`, `always_latch begin`,
# `always_ff @(...) begin` and `always @(...) begin`. Every one of them ends in
# `begin`; an `always*` statement that does not is an inline body this reader
# does not follow.
ALWAYS_OPEN = re.compile(r"^always(_comb|_latch|_ff)?\s*(@\s*\([^)]*\))?\s*begin$")

# A BARE declaration of ONE name: `reg x;`, `wire [3:0] y;`, `logic signed z;`,
# `reg [7:0] \\esc.name ;`, with an optional trailing range. Deliberately does
# NOT accept an initializer or a comma-separated list -- both carry information
# this reader would otherwise drop on the floor.
DECL = re.compile(
    r"^(reg|wire|logic)\b"
    r"(\s+(signed|unsigned))?"
    r"(\s*\[[^\]]*\])?"
    r"\s+(?P<name>\\[^\s]+|[A-Za-z_][A-Za-z0-9_$.]*)"
    r"\s*(\[[^\]]*\]\s*)?"
    r";$")

# THE LEFT-HAND SIDE OF AN ASSIGNMENT, or a refusal.
#
# Supported, because cgen emits them: a plain identifier, an escaped
# identifier (`\name.with.dots `), either followed by a bit- or part-select,
# and either preceded by the `assign` keyword. EVERYTHING ELSE -- a
# concatenation, a nested select on an escaped name, anything with more than
# one identifier in it -- returns ok=False, which makes the caller report the
# module UNPARSED. There is deliberately no "best effort" branch: a target this
# reader cannot name is a fanout edge it would silently drop.
# A select on the LEFT is allowed only when it is CONSTANT. `wide[din +: 2]`
# also READS `din`, and a reader that silently dropped that would be inventing
# the very kind of missing edge this tool exists to stop -- so it is refused
# instead.
LHS_SEL     = r"(\[[0-9]+(:[0-9]+)?\]\s*)*"
LHS_PLAIN   = re.compile(r"^([A-Za-z_][A-Za-z0-9_$.]*)\s*" + LHS_SEL + r"$")
LHS_ESCAPED = re.compile(r"^(\\[^\s]+)\s+" + LHS_SEL + r"$")

def parse_lhs(text):
    t = text.strip()
    if t.startswith("assign "):
        t = t[len("assign "):].strip()
    m = LHS_ESCAPED.match(t + " ") if t.startswith("\\") else LHS_PLAIN.match(t)
    if not m:
        return None, False
    return m.group(1).strip(), True

class Netlist:
    def __init__(self, path):
        self.readers = {}     # net -> set of consumers it feeds
        self.outputs = set()
        self.regs = set()
        self.mem_ports = {}   # net -> list of "<inst>.<port>"
        self.unparsed = []
        self._parse(path)

    def _feed(self, src, dst):
        self.readers.setdefault(src, set()).add(dst)

    def _parse(self, path):
        lines = open(path, errors="replace").read().split("\n")
        # ONLY THE TOP MODULE. cgen inlines the memory wrapper's own definition
        # into the same file, and that second module's parameter header is not
        # this reader's business -- but parsing it would either poison every
        # verdict or, worse, mix its nets into the fanout graph.
        top = os.path.basename(path)[:-2] if path.endswith(".v") else os.path.basename(path)
        start, stop = None, len(lines)
        depth_names = []
        for ix, l in enumerate(lines):
            ls = l.strip()
            if ls.startswith("module "):
                depth_names.append((ix, ls.split()[1].split("(")[0]))
            elif ls == "endmodule" and depth_names:
                ix0, nm = depth_names.pop()
                if nm == top:
                    start, stop = ix0, ix
                    break
        if start is None:
            self.unparsed.append((0, f"no module named {top} in {path}"))
            return
        lines = lines[start:stop]
        in_header, cond_stack, blk = True, [], []
        i = 0
        while i < len(lines):
            raw = lines[i]; i += 1
            l = raw.strip()
            if not l or l.startswith("//") or l.startswith("/*") or l.startswith("`"):
                continue
            if in_header:
                if l.startswith("module "):
                    continue
                m = re.match(r",?\s*output\s+(?:reg\s+|wire\s+|logic\s+)?(?:signed\s+)?(?:\[[^\]]*\]\s*)?([A-Za-z_][\w$]*)", l)
                if m:
                    self.outputs.add(m.group(1)); continue
                if re.match(r",?\s*(input|inout)\b", l):
                    continue
                if l.startswith(");"):
                    in_header = False; continue
                self.unparsed.append((i, raw)); continue
            if re.match(r"^(reg|wire|logic)\b", l):
                # A DECLARATION MAY NOT CARRY AN INITIALIZER. `wire fwd = seed;`
                # is legal Verilog and a real connectivity edge, and the old
                # branch swallowed the whole line as "a declaration", recorded
                # `seed` as a declared NAME, and never wrote the edge -- so a
                # seed live at the module output came back DEAD. Anything other
                # than a bare declaration of one name poisons the module.
                dm = DECL.match(l)
                if dm is None or "=" in l:
                    self.unparsed.append((i, raw))
                    continue
                self.regs.add(dm.group("name").strip())
                continue
            if l in ("endmodule", "begin"):
                continue
            if l.startswith("always"):
                # ONLY A BLOCK OPENER. `always_comb outp = seed;` is legal
                # SystemVerilog -- cgen itself emits that form inside the memory
                # wrapper -- and `startswith("always")` swallowed it whole, so a
                # seed driving the module OUTPUT came back DEAD. Anything that
                # is not one of the exact openers below is refused.
                if ALWAYS_OPEN.match(l) is None:
                    self.unparsed.append((i, raw))
                continue
            if l == "end":
                if blk: blk.pop()
                continue
            if l.startswith("end else begin") or l == "else begin":
                continue
            m = re.match(r"^if\s*\((.*)\)\s*begin$", l)
            if m:
                blk.append(idents(m.group(1))); continue
            # an instance: `Name #( ... ) inst (` or `Name inst (`
            if re.match(r"^[A-Za-z_][\w$]*\s", l) and l.endswith("("):
                inst = l.split()[-2] if l.split()[-1] == "(" else l.split()[-1]
                ports = []
                while i < len(lines):
                    pl = lines[i].strip(); i += 1
                    if pl.startswith(");") or pl == ");":
                        break
                    pm = re.match(r",?\s*\.([A-Za-z_][\w$]*)\s*\((.*)\)\s*$", pl)
                    if not pm:
                        self.unparsed.append((i, pl)); break
                    ports.append((pm.group(1), pm.group(2)))
                for pname, net in ports:
                    for nid in idents(net):
                        if pname.startswith(MEM_OUT):
                            continue
                        self._feed(nid, f"MEMPORT:{inst}.{pname}" if pname.startswith(MEM_IN) else f"INST:{inst}.{pname}")
                continue
            m = re.match(r"^(.*?)\s*(<=|=)\s*(.*);$", l)
            if m:
                lhs, ok = parse_lhs(m.group(1))
                if not ok:
                    # AN LHS THIS READER CANNOT NAME POISONS THE MODULE. It used
                    # to keep whatever text it had: `assign out = seed;` was
                    # recorded as a net literally called "assign out", so `out`
                    # never gained an edge and a seed feeding the module's own
                    # OUTPUT came back DEAD. A verdict is only worth as much as
                    # the parse behind it.
                    self.unparsed.append((i, raw))
                    continue
                srcs = idents(m.group(3))
                for c in blk:
                    srcs.extend(c)
                tgt = f"NEXT:{lhs}" if m.group(2) == "<=" else lhs
                for sdr in srcs:
                    self._feed(sdr, tgt)
                continue
            self.unparsed.append((i, raw))

    def classify(self, seed, maxhops=200000):
        seen, front, hits = set(), [seed], set()
        while front:
            n = front.pop()
            if n in seen or len(seen) > maxhops:
                continue
            seen.add(n)
            for c in self.readers.get(n, ()):
                if c.startswith("MEMPORT:"):
                    port = c.split(".")[-1]
                    # `clk` is the single-clock wrapper's clock; `wr_clock_0`
                    # / `rd_clock_0` the multiclock one's. Both are clocks, and
                    # classing `clk` as plain control is how `txfma_top` first
                    # read as MEM-CTRL when it is a memory CLOCK.
                    hits.add("MEM-CLOCK" if ("clock" in port or port == "clk") else
                             "MEM-DATA" if ("din" in port or "dout" in port) else "MEM-CTRL")
                    continue
                if c.startswith("INST:"):
                    hits.add("INSTANCE"); continue
                if c.startswith("NEXT:"):
                    hits.add("STATE"); continue
                if c in self.outputs:
                    hits.add("OUTPUT")
                front.append(c)
        return hits

def main():
    seeds = sys.argv[2:]
    nl = Netlist(sys.argv[1])
    rec = {"file": sys.argv[1], "unparsed": len(nl.unparsed)}
    if nl.unparsed:
        rec["verdict"] = "UNPARSED"
        rec["first_unparsed"] = nl.unparsed[:3]
        print(json.dumps(rec)); return 2
    rec["seeds"] = {}
    for s in seeds:
        hits = sorted(nl.classify(s))
        rec["seeds"][s] = hits or ["DEAD"]
    print(json.dumps(rec))
    return 0

if __name__ == "__main__":
    sys.exit(main())
