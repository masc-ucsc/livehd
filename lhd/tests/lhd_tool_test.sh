#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `lhd tool`: the lg-side inspector — cat / grep / diff / tree over an
# LGraph library, with per-node/pin attribute access (the color-debug flow).
# Exercises target selection, the field:value filter grammar, the unified diff,
# jsonl output, and the --max guard.

set -u

LHD=lhd/lhd
V0=lhd/tests/part_hier.v
TOP=part_hier
W="${TEST_TMPDIR:-/tmp/lhd_tool_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# Build an LGraph library, plus a pristine copy for the diff.
"$LHD" compile verilog "$V0" --top "$TOP" --reader yosys-verilog --recipe O1 \
  --emit-dir lg:"$W/lg" --workdir "$W/w" -q --result-json "$W/r.json" 2>/dev/null \
  || fail "compile -> lg failed: $(cat "$W/r.json")"
cp -r "$W/lg" "$W/lg2"

P() { "$LHD" tool "$@" --diag-fmt pretty -q 2>/dev/null; }
J() { "$LHD" tool "$@" --diag-fmt jsonl  -q 2>/dev/null; }

# 1. cat --target node: module header + node lines with attribute columns. Only
#    set attributes are printed (unset ones, e.g. color before coloring, are
#    omitted to cut verbosity), so the src= column shows but color=nil does not.
P cat lg:"$W/lg" --top "$TOP" --target node >"$W/cat.out" || fail "tool cat exited nonzero"
head -1 "$W/cat.out" | grep -q "^module $TOP$" || fail "cat must start with the module line: $(head -1 "$W/cat.out")"
grep -q 'src=' "$W/cat.out" || fail "cat --target node must show a src= column"
grep -q 'color=nil' "$W/cat.out" && fail "cat must omit unset attributes (color=nil should not print)"

# 2. cat --target all: nested node + pin (bits) + wiring lines.
P cat lg:"$W/lg" --top "$TOP" --target all >"$W/all.out" || fail "tool cat all nonzero"
grep -qE 'bits=[0-9]' "$W/all.out" || fail "cat --target all must show pin bits"
grep -qE '\->|<-' "$W/all.out" || fail "cat --target all must show wiring arrows"

# 3. grep color:nil before coloring lists the (uncolored) partitionable nodes.
P grep color:nil lg:"$W/lg" --top "$TOP" --target node >"$W/g0.out" || fail "grep color:nil nonzero"
n0=$(wc -l <"$W/g0.out")
[ "$n0" -gt 0 ] || fail "grep color:nil (pre-color) must find uncolored nodes"
grep -q "^lg/$TOP " "$W/g0.out" || fail "grep lines must be prefixed lib/module: $(head -1 "$W/g0.out")"

# 4. grep needs a filter (it is a search, not a dump).
"$LHD" tool grep lg:"$W/lg" --top "$TOP" -q >"$W/ge.json" 2>/dev/null
grep -q '"class":"usage"' "$W/ge.json" || fail "grep without a filter must be a usage error: $(cat "$W/ge.json")"

# 5. numeric filter: bits:>8 over pins finds the 9-bit signals.
P grep 'bits:>8' --target pin lg:"$W/lg" --top "$TOP" >"$W/gb.out" || fail "grep bits:>8 nonzero"
grep -q 'bits=9' "$W/gb.out" || fail "grep bits:>8 must surface bits=9 pins: $(head -1 "$W/gb.out")"

# 5b. '=' is equivalent to ':' as a separator (Pyrope reads ':' as a type, so
#     '=' is the preferred filter spelling). color=nil must equal color:nil.
P grep color=nil lg:"$W/lg" --top "$TOP" --target node >"$W/geq.out" || fail "grep color=nil nonzero"
[ "$(wc -l <"$W/geq.out")" -eq "$n0" ] \
  || fail "grep color=nil must equal grep color:nil ($n0 vs $(wc -l <"$W/geq.out"))"

# 5c. a relational op may lead directly: bits>8 must match the same as bits:>8.
P grep 'bits>8' --target pin lg:"$W/lg" --top "$TOP" >"$W/gbd.out" || fail "grep bits>8 nonzero"
cmp -s <(sort "$W/gb.out") <(sort "$W/gbd.out") || fail "bits>8 must match bits:>8"

# 5d. a bare term (no field) matches anywhere it appears: grepping a node kind
#     finds those cells the way `cat` shows them (the get_mask scenario).
k=$("$LHD" tool cat lg:"$W/lg" --top "$TOP" --target node --diag-fmt jsonl -q 2>/dev/null \
      | sed -nE '1s/.*"kind":"([^"]+)".*/\1/p')
[ -n "$k" ] || fail "could not read a node kind from jsonl cat"
P grep "$k" --target node lg:"$W/lg" --top "$TOP" >"$W/gbare.out" || fail "grep bare '$k' nonzero"
[ "$(wc -l <"$W/gbare.out")" -gt 0 ] || fail "bare term '$k' must match the $k nodes"
grep -q "${k}_" "$W/gbare.out" || fail "bare match must surface the ${k}_<nid> identities"

# 6. After pass.color the uncolored set shrinks (coloring took effect).
"$LHD" pass color synth --top "$TOP" lg:"$W/lg" --workdir "$W/wc" -q 2>/dev/null || fail "pass color synth failed"
P grep color:nil lg:"$W/lg" --top "$TOP" --target node >"$W/g1.out"
n1=$(wc -l <"$W/g1.out")
[ "$n1" -lt "$n0" ] || fail "pass.color must reduce the color:nil set ($n0 -> $n1)"
# and a colored node is now greppable by its color id
P grep 'color:>0' lg:"$W/lg" --top "$TOP" --target node >"$W/gc.out"
[ "$(wc -l <"$W/gc.out")" -gt 0 ] || fail "grep color:>0 must find colored nodes after synth"

# 7. diff: the colored lib vs the pristine copy shows the color deltas; an
#    identical pair reports 'identical'.
P diff lg:"$W/lg" lg:"$W/lg2" --top "$TOP" --target node --attr color >"$W/diff.out" || fail "tool diff nonzero"
grep -qE '^[-+] ' "$W/diff.out" || fail "diff of colored vs uncolored must show -/+ lines"
P diff lg:"$W/lg" lg:"$W/lg" --top "$TOP" --target node --attr color >"$W/diff0.out" || fail "tool diff self nonzero"
grep -q 'identical' "$W/diff0.out" || fail "diff of a lib against itself must be 'identical': $(cat "$W/diff0.out")"

# 8. tree: the instance hierarchy line for the top, with a node count.
P tree lg:"$W/lg" --top "$TOP" >"$W/tree.out" || fail "tool tree nonzero"
grep -qE "^$TOP  \[[0-9]+ nodes\]" "$W/tree.out" || fail "tree must print the top with a node count: $(cat "$W/tree.out")"

# 8b. tree --target kind:register|memory: list the stateful cells that ride the
#     instance hierarchy. The yosys-verilog path flattens, so this uses a
#     hierarchical Pyrope design — `regs` (flops) and `ram` (a memory) each
#     instanced under the top.
"$LHD" compile lhd/tests/tree_hier.prp --top top --recipe O1 \
  --emit-dir lg:"$W/hlg" --workdir "$W/hw" -q --result-json "$W/hr.json" 2>/dev/null \
  || fail "compile tree_hier.prp -> lg failed: $(cat "$W/hr.json")"
HTOP=tree_hier.top

# bare tree: the instance hierarchy only — no register/memory rows.
P tree lg:"$W/hlg" --top "$HTOP" >"$W/ht.out" || fail "tool tree (hier) nonzero"
grep -qE "^$HTOP  \[[0-9]+ nodes\]" "$W/ht.out" || fail "hier tree must print the top: $(cat "$W/ht.out")"
grep -qE ': tree_hier\.(regs|ram)  \[' "$W/ht.out" || fail "hier tree must list the submodule instances"
grep -qE ': (flop|memory)' "$W/ht.out" && fail "bare tree must NOT list registers/memories (no --target kind)"

# kind:register + kind:memory (repeatable): both surface, indented under the
# module that owns them (one level past their instance line).
P tree lg:"$W/hlg" --top "$HTOP" --target kind:register --target kind:memory >"$W/htk.out" \
  || fail "tool tree --target kind nonzero"
# Flops and memories now carry their RTL names (tolg set_name); the fall-back
# formats are flop_<nid>/memory_<nid>. Match either spelling.
grep -qE '^    [A-Za-z_][A-Za-z0-9_]*  : flop' "$W/htk.out" || fail "kind:register must list a flop row: $(cat "$W/htk.out")"
grep -qE '^    [A-Za-z_][A-Za-z0-9_.]*  : memory' "$W/htk.out" || fail "kind:memory must list a memory row: $(cat "$W/htk.out")"

# a single kind narrows to just that kind; an exact Ntype name (flop) matches too.
P tree lg:"$W/hlg" --top "$HTOP" --target kind:memory >"$W/htm.out" || fail "tree kind:memory nonzero"
grep -q ': memory' "$W/htm.out" || fail "kind:memory must surface the memory"
grep -q ': flop' "$W/htm.out" && fail "kind:memory alone must NOT list flops"
P tree lg:"$W/hlg" --top "$HTOP" --target kind:flop >"$W/htf.out" || fail "tree kind:flop nonzero"
grep -q ': flop' "$W/htf.out" || fail "an exact Ntype name (kind:flop) must match"

# 9. jsonl: one flat type-tagged record per entity, nil -> null.
J cat lg:"$W/lg2" --top "$TOP" --target node >"$W/j.out" || fail "jsonl cat nonzero"
grep -q '{"t":"node"' "$W/j.out" || fail "jsonl must emit type-tagged node records"
grep -q '"color":null' "$W/j.out" || fail "jsonl must render an unset color as null"

# 10. --max caps the output and prints the truncation footer.
P cat lg:"$W/lg2" --top "$TOP" --target node --max 2 >"$W/m.out" || fail "tool cat --max nonzero"
grep -q 'truncated at --max 2' "$W/m.out" || fail "--max must print a truncation footer: $(cat "$W/m.out")"

# 11. ENDPOINT LABELLING.  An edge endpoint is `<debug_name>.<pin>`, and the
#     `<pin>` half used to come from `pin_name_of` on BOTH sides.  On a sink pin
#     of a node whose OUTPUT carries a name, that returns the output's name --
#     so a named mux printed its selector and both data arms under one label,
#     the same string it printed as its DRIVER endpoint.  Three distinct
#     operands collapsed onto one token, and the port id -- the only thing that
#     says selector-vs-arm -- was gone.
#
#     `tool_sink_pins.v` is built to show it: `named_mux_q` is a named mux with
#     a selector and two arms, one sibling mux carries a CONSTANT arm (the
#     default branch), and every data input traces back to a primary input.
"$LHD" compile verilog lhd/tests/tool_sink_pins.v --top tool_sink_pins \
  --reader yosys-verilog --recipe O1 --emit-dir lg:"$W/slg" --workdir "$W/sw" \
  -q --result-json "$W/sr.json" 2>/dev/null \
  || fail "compile tool_sink_pins.v -> lg failed: $(cat "$W/sr.json")"

J cat lg:"$W/slg" --top tool_sink_pins --target all --max 0 >"$W/sp.jsonl" \
  || fail "tool cat tool_sink_pins (jsonl) nonzero"
P cat lg:"$W/slg" --top tool_sink_pins --target all --max 0 >"$W/sp.pretty" \
  || fail "tool cat tool_sink_pins (pretty) nonzero"

# The node under test is found by its PIN NAME, never by a hardcoded nid.
mnid=$(sed -nE 's/^\{"t":"pin".*"nid":([0-9]+),"name":"named_mux_q".*/\1/p' "$W/sp.jsonl" | head -1)
[ -n "$mnid" ] || fail "fixture did not produce a mux whose output pin is named named_mux_q"

# 11a. its SINK endpoints are port ids, all three distinct.
sinks=$(sed -nE 's/^.*"to":"mux_'"$mnid"'\.([^"]*)".*/\1/p' "$W/sp.jsonl" | sort -u)
[ "$(echo "$sinks" | wc -l)" -eq 3 ] \
  || fail "named mux must have 3 DISTINCT sink endpoint labels, got: $(echo $sinks)"
for want in p0 p1 p2; do
  echo "$sinks" | grep -qx "$want" || fail "named mux sink labels must include $want, got: $(echo $sinks)"
done
# 11b. and none of them wears the DRIVER's name -- that is the collision.
grep -q '"to":"mux_'"$mnid"'\.named_mux_q"' "$W/sp.jsonl" \
  && fail "a sink endpoint printed the driver pin name (named_mux_q): the port id is lost"

# 11c. the DRIVER endpoint keeps its real name.
grep -q '"from":"mux_'"$mnid"'\.named_mux_q"' "$W/sp.jsonl" \
  || fail "the driver endpoint must keep the pin name named_mux_q"

# 11d. pretty output carries the same split: `<-` lines by port id, `->` by name.
awk '/^  mux_'"$mnid"'$/{f=1;next} /^  [a-z]/{f=0} f' "$W/sp.pretty" >"$W/sp.mux" \
  || fail "could not slice the named mux out of the pretty dump"
for want in p0 p1 p2; do
  grep -qE "^    \.$want  bits=[0-9]+  <- " "$W/sp.mux" \
    || fail "pretty sink line .$want missing for the named mux: $(cat "$W/sp.mux")"
done
grep -qE "^    \.named_mux_q  bits=[0-9]+  -> " "$W/sp.mux" \
  || fail "pretty driver line must keep the pin name: $(cat "$W/sp.mux")"
grep -qE "^    \.named_mux_q  bits=[0-9]+  <- " "$W/sp.mux" \
  && fail "a pretty SINK line printed the driver pin name"

# 11e. constant operands are labelled by port id too -- `consts` had the same
#      lossy label, which would have merged two constants on one node.
sed -nE 's/^.*"consts":"([^"]*)".*/\1/p' "$W/sp.jsonl" | tr ',' '\n' | sed -E 's/=.*//' \
  | sort -u >"$W/sp.clabels"
grep -qvE '^p[0-9]+$' "$W/sp.clabels" \
  && fail "every consts label must be p<port_id>, got: $(tr '\n' ' ' <"$W/sp.clabels")"
grep -q '"consts":"p1=' "$W/sp.jsonl" \
  || fail "the constant default arm must appear as a p1 const operand"

# 11f. edges are emitted from the DRIVER side, once per driver node -- and
#      `forward_class()` yields neither CONST_NODE nor a GRAPH INPUT.  The
#      constants are covered by `consts`; the graph inputs were covered by
#      nothing, so every primary input's fanout was missing from the dump.
for pi in sel a b c; do
  grep -q '"from":"\$'"$pi"'"' "$W/sp.jsonl" \
    || fail "no edge is emitted out of graph input \$$pi -- primary-input fanout is missing"
done
grep -q '"to":"\$q"' "$W/sp.jsonl" || fail "the graph output \$q must still be an edge sink"

# 12. CONSTANT WIDTHS.  `consts` prints a value; `const_bits` prints the width
#     that value is compared AT.  Op_EQ compares `bv_uint` per operand at its
#     own width (LGraphModel.lean:176) and pass.lean materialises an EQ's
#     constant at `dep_w = max` over its operands (pass_lean.cpp:1498), so
#     `-1` at width 2 IS 3 and a reader without the width cannot tell
#     `eq(V,-1)` from `eq(V,3)`.
#
#     tool_const_width_str mirrors the const branch of pass.lean's `pin_width`
#     (pass_lean.cpp:177).  The arms reachable from RTL are pinned here; the
#     "?" arm -- a declared width too narrow for its own value, which pass.lean
#     calls "a lie" -- is not constructible from a front end that sizes its own
#     constants, and is covered where it matters, at the consumer boundary, by
#     lhd/tests/mux_ring_census_test.sh case 8.
"$LHD" compile verilog lhd/tests/tool_const_width.v --top tool_const_width \
  --reader yosys-verilog --recipe O1 --emit-dir lg:"$W/clg" --workdir "$W/cw" \
  -q --result-json "$W/cr.json" 2>/dev/null \
  || fail "compile tool_const_width.v -> lg failed: $(cat "$W/cr.json")"
J cat lg:"$W/clg" --top tool_const_width --target node --max 0 >"$W/cw.jsonl" \
  || fail "tool cat tool_const_width (jsonl) nonzero"

# 12a. every entry is a width or "?", and `consts`/`const_bits` agree entry for
#      entry -- they are one loop in the emitter, and a silent misalignment
#      would attach one constant's width to another's value.
python3 - "$W/cw.jsonl" <<'PYEOF' || fail "consts/const_bits are not aligned"
import json, sys
n = 0
for line in open(sys.argv[1]):
    r = json.loads(line)
    if r.get("t") != "node":
        continue
    cs, cb = r.get("consts"), r.get("const_bits")
    if not cs:
        assert cb is None, (cs, cb)
        continue
    v, b = cs.split(","), cb.split(",")
    assert len(v) == len(b), (cs, cb)
    for ve, be in zip(v, b):
        assert ve.split("=")[0] == be.split("=")[0], (ve, be)
        w = be.split("=", 1)[1]
        assert w == "?" or (w.isdigit() and int(w) >= 1), be
        n += 1
assert n > 0, "fixture produced no constants"
print(f"checked {n} constant operands")
PYEOF

# 12b. a >64-bit literal keeps its real width.  Nothing that measures a
#      constant through an int64 can report this one.
grep -q '"consts":"p0=0x1234567890abcdef12345","const_bits":"p0=82"' "$W/cw.jsonl" \
  || fail "the 82-bit literal did not report const_bits=82: $(grep -o '"consts":"p0=0x[^"]*","const_bits":"[^"]*"' "$W/cw.jsonl")"

# 12c. the unsized `-1` of LiveHD's zext idiom reports a width of 1 -- never
#      nil, and never the value.
grep -q '"consts":"p2=-1","const_bits":"p2=1"' "$W/cw.jsonl" \
  || fail "the unsized -1 mask did not report const_bits=1"

# 12d. a NEGATIVE literal on a declared-width pin reports that declared width,
#      which is what decides the value it is compared as.  It survives
#      constant folding in the section-11 fixture (the default arm of its
#      mux), not in this one, so the assertion reads that dump.
grep -q '"consts":"p1=-0x5b","const_bits":"p1=8"' "$W/sp.jsonl" \
  || fail "the negative 8-bit mux-arm literal did not report const_bits=8: $(grep -o '"consts":"p1=-[^"]*","const_bits":"[^"]*"' "$W/sp.jsonl")"

echo "PASS: lhd tool cat/grep/diff/tree (lg path)"
