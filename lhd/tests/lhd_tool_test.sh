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
grep -qE '^    flop_[0-9]+  : flop' "$W/htk.out" || fail "kind:register must list a flop row: $(cat "$W/htk.out")"
grep -qE '^    memory_[0-9]+  : memory' "$W/htk.out" || fail "kind:memory must list a memory row: $(cat "$W/htk.out")"

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

echo "PASS: lhd tool cat/grep/diff/tree (lg path)"
