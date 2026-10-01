#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Compiling into an lg: library that already holds a module must leave that
# module declaring exactly the ports its source declares now. tolg's GraphIO
# setup used to only ADD ports, so renaming input `a` to `a2` and compiling
# into the same --emit-dir lg: kept `input 2 a` next to `input 2 a2`: cgen
# emitted a phantom `input a`, `lhd synth lg:L` passed with the stale port,
# and pass.partition crashed on the unnamed pin (SIGSEGV in opt).
#
#   1. Verilog: rename + reorder + a new output, recompiled into the same
#      lg: dir, equals a fresh compile (library.txt and emitted Verilog); synth
#      of the reused library carries the new interface.
#   2. Pyrope: a source redefining a module of an absorbed lg: input with other
#      ports (`lhd synth psub.prp ptop.prp lg:L1`) emits the source interface.
#   3. A module of the lg: input that this compile does NOT rebuild, but which
#      instantiates a module whose ports changed, is refused (its instance
#      binds by port id and would be silently rewired): a config error whose
#      hint names that lg: input.
#   4. pass.partition refuses a GraphIO with two ports on one port id (the
#      yosys reader still writes one on the same rename) with a config
#      diagnostic, never a crash.
#   5. Pyrope, incremental (--workdir): an edit that drops a unit from the
#      import closure AND reorders a module the dropped unit instantiated is
#      NOT refused -- the dropped unit's module is a leftover the kernel prunes
#      right after lowering -- and equals a cold compile.
#   6. Verilog never prunes a leftover: dropping the module that instantiated a
#      reordered child is refused (a config error naming the leftover), never
#      silently rewired.
#   7. The flattener refuses a CHILD GraphIO with two ports on one port id
#      (the yosys reader's add-only merge on a `keep_hierarchy` child), under
#      both mappers, instead of emitting a netlist that differs from a fresh one.
#   8. Leg 5's edit, first attempted with mistakes (failing compiles, which
#      republish the compile cache's source manifest but not the library):
#      the fixed edit still recompiles incrementally, equal to cold.
#   9. Leg 5's edit under a changed compile option (a compile cache context
#      miss): still incremental, equal to a cold compile with that option.
#  10. A SHARED lg: dir: another design (another top, same --workdir) rebuilt
#      mid.md there and still instantiates it. Leg 5's closure change (mid
#      leaves this design's closure), with or without a failed attempt first,
#      keeps mid.md: the library's ownership record says another design holds
#      it, and the prune never deletes a module something left in the library
#      still instantiates.
#  11. The same shared dir with leaf reordered: the other design's mid.md
#      binds leaf.lf by port id, so the incremental compile (a partial restore)
#      is refused like a compile without a workdir, never a silent rewire.
#  12. Ownership is written with the library: a compile saves the library
#      (with a new unit `extra`) and then fails in a later emit, a failing edit
#      drops `extra`, then the fixed edit compiles: extra.ex is this design's
#      leftover (the record says so, although no graph generation was stored)
#      and is pruned, equal to cold.
#  13. A partial restore measures dirtiness against the stored graph
#      generation, not only the manifest: leaf's port swap in a compile that
#      fails past the parse, then the fix, rebuilds leaf's importer mid, equal
#      to cold (it restored mid's stale body, silently rewiring its instance).
#  14. A shared lg: dir, two designs with their own workdirs: design ta edits
#      its body and a generic binding, then design tb recompiles UNCHANGED
#      (the all-clean directory-swap path, and the restore path with a Verilog
#      emit). tb's stored generation snapshotted ta's modules too; restoring
#      them silently reverted ta.ta in the library (exit 0).
#  15. One --workdir, one design compiled with and without --top, plus a tb
#      design over it: leg 5's closure change compiles under every command
#      line, equal to cold. An ownership record per command-line spelling
#      claimed the other's leftover mid.md, which then bound leaf's old ports:
#      every compile of either spelling was refused (exit 4) until the
#      workdir was wiped.

set -u

LHD=lhd/lhd
# Hermetic: the vendored Liberty, no PDK; no STA (not what this test is about).
LIB=inou/prp/tests/abc/test.lib
W="${TEST_TMPDIR:-/tmp/lhd_lg_reuse_io_$$}"
rm -rf "$W"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

[ -f "$LIB" ] || fail "missing liberty $LIB"

# $1 = a command's exit status, $2 = its combined output file: a crash (signal
# or the crash reporter) is always a failure, whatever the leg expects.
not_a_crash() {
  if [ "$1" -ge 128 ] || grep -q 'FATAL signal' "$2"; then
    fail "lhd crashed (exit $1): $(tail -5 "$2")"
  fi
}

# ── 1. Verilog: rename + reorder + new output into the same lg: dir ──────────
cat > "$W/d1.v" <<'EOF'
module top(input clk, input [3:0] a, input [3:0] b, output reg [3:0] y);
  always @(posedge clk) y <= a + b;
endmodule
EOF
cat > "$W/d2.v" <<'EOF'
module top(input clk, input [3:0] b, input [3:0] a2, output reg [3:0] y, output [3:0] z);
  always @(posedge clk) y <= a2 + b;
  assign z = a2 ^ b;
endmodule
EOF
"$LHD" compile "$W/d1.v" --emit-dir lg:"$W/L" --workdir "$W/w1" -q --result-json "$W/r1.json" >/dev/null \
  || fail "first compile failed: $(cat "$W/r1.json" 2>/dev/null)"
"$LHD" compile "$W/d2.v" --emit-dir lg:"$W/L" --emit verilog:"$W/reused.v" --workdir "$W/w2" -q \
  --result-json "$W/r2.json" >/dev/null || fail "recompile into the same lg: dir failed: $(cat "$W/r2.json" 2>/dev/null)"
"$LHD" compile "$W/d2.v" --emit-dir lg:"$W/F" --emit verilog:"$W/fresh.v" --workdir "$W/w3" -q \
  --result-json "$W/r3.json" >/dev/null || fail "fresh compile failed: $(cat "$W/r3.json" 2>/dev/null)"
[ -f "$W/L/library.txt" ] && [ -f "$W/F/library.txt" ] || fail "an lg: library is missing"
grep -Eq '^  input [0-9]+ a ' "$W/L/library.txt" && fail "the renamed-away input survived: $(cat "$W/L/library.txt")"
cmp -s "$W/L/library.txt" "$W/F/library.txt" \
  || fail "reused library differs from a fresh one: $(diff "$W/L/library.txt" "$W/F/library.txt")"
[ -s "$W/fresh.v" ] || fail "fresh compile emitted no Verilog"
cmp -s "$W/reused.v" "$W/fresh.v" || fail "reused Verilog differs from a fresh one: $(diff "$W/reused.v" "$W/fresh.v")"

"$LHD" synth lg:"$W/L" --set synth.liberty="$LIB" --set synth.opentimer=false --emit verilog:"$W/net.v" --workdir "$W/ws" -q --result-json "$W/rs.json" >"$W/s.log" 2>&1
st=$?
not_a_crash "$st" "$W/s.log"
[ "$st" -eq 0 ] || fail "synth of the reused library failed: $(cat "$W/rs.json" 2>/dev/null)"
grep -Eq 'input \[3:0\] a2' "$W/net.v" || fail "synthesized top lost input a2: $(head -12 "$W/net.v")"
grep -Eq 'input \[3:0\] a$' "$W/net.v" && fail "synthesized top kept the stale input a: $(head -12 "$W/net.v")"
echo "PASS: a recompile into a reused lg: dir declares the source interface, like a fresh one"

# ── 2. Pyrope source redefining an absorbed lg: module ─────────────────────
cat > "$W/psub.prp" <<'EOF'
pub mod dly(din:U8) -> (q:U8@[1]) {
  reg r:U8 = 0
  q = r
  r = din
}
EOF
cat > "$W/ptop.prp" <<'EOF'
const dly = import("psub.dly")
pub mod ptop(a:U8, k:U8) -> (o:U8@[1], z:U8@[]) {
  mut d = dly::[name=d](din = a ^ k)
  o = d.q
  z = a & k
}
EOF
"$LHD" compile "$W/psub.prp" "$W/ptop.prp" --emit-dir lg:"$W/L1" -q --result-json "$W/p1.json" >/dev/null \
  || fail "pyrope compile into L1 failed: $(cat "$W/p1.json" 2>/dev/null)"
cat > "$W/ptop.prp" <<'EOF'
const dly = import("psub.dly")
pub mod ptop(a2:U8, k:U8) -> (o:U8@[1], z:U8@[]) {
  mut d = dly::[name=d](din = a2 ^ k)
  o = d.q
  z = a2 & k
}
EOF
"$LHD" compile "$W/psub.prp" "$W/ptop.prp" lg:"$W/L1" --emit-dir lg:"$W/L2" -q --result-json "$W/p2.json" >/dev/null \
  || fail "source redefining an lg: module failed: $(cat "$W/p2.json" 2>/dev/null)"
"$LHD" compile "$W/psub.prp" "$W/ptop.prp" --emit-dir lg:"$W/F2" -q --result-json "$W/p3.json" >/dev/null \
  || fail "fresh pyrope compile failed: $(cat "$W/p3.json" 2>/dev/null)"
cmp -s "$W/L2/library.txt" "$W/F2/library.txt" \
  || fail "redefined lg: module differs from a fresh compile: $(diff "$W/L2/library.txt" "$W/F2/library.txt")"
"$LHD" synth "$W/psub.prp" "$W/ptop.prp" lg:"$W/L1" --top ptop --set synth.liberty="$LIB" --set synth.opentimer=false \
  --emit verilog:"$W/pnet.v" --workdir "$W/pws" -q \
  --result-json "$W/ps.json" >"$W/ps.log" 2>&1
st=$?
not_a_crash "$st" "$W/ps.log"
[ "$st" -eq 0 ] || fail "synth of source + lg: redefinition failed: $(cat "$W/ps.json" 2>/dev/null)"
grep -Eq 'input \[7:0\] a2' "$W/pnet.v" || fail "synthesized ptop lost input a2: $(grep -n input "$W/pnet.v")"
grep -Eq 'input \[7:0\] a$' "$W/pnet.v" && fail "synthesized ptop kept the lg: input's stale a: $(grep -n input "$W/pnet.v")"
echo "PASS: a source module redefining an absorbed lg: module declares the source interface"

# ── 3. an lg: module NOT rebuilt still instantiating the old interface ────────
cat > "$W/psub.prp" <<'EOF'
pub mod dly(x:U8, din2:U8) -> (q:U8@[1]) {
  reg r:U8 = 0
  q = r
  r = din2 ^ x
}
EOF
"$LHD" compile "$W/psub.prp" lg:"$W/L1" --emit-dir lg:"$W/L3" --emit verilog:"$W/stale.v" -q \
  --result-json "$W/p4.json" >"$W/p4.log" 2>&1
st=$?
not_a_crash "$st" "$W/p4.log"
[ "$st" -ne 0 ] || fail "a stale lg: instance of a changed interface was silently rewired: $(grep -A6 '^dly ' "$W/stale.v")"
grep -q "instantiates it with the old ports" "$W/p4.json" || fail "missing stale-instance diagnostic: $(cat "$W/p4.json")"
grep -q "ptop.ptop" "$W/p4.json" || fail "the diagnostic does not name the stale instantiator: $(cat "$W/p4.json")"
[ "$st" -eq 4 ] || fail "a stale lg: library is a config error (exit 4), got exit $st: $(cat "$W/p4.json")"
grep -q "'ptop.ptop' comes from the lg: input $W/L1" "$W/p4.json" \
  || fail "the hint does not name the lg: input the stale instantiator came from: $(cat "$W/p4.json")"
echo "PASS: an unrebuilt lg: instance of a changed interface is refused, naming its lg: input"

# ── 4. pass.partition: two ports on one port id is a diagnostic, not a crash ──
# The yosys reader still ADDS a renamed port next to the old one; when it does,
# the library is exactly the pre-fix tolg shape. If the reader stops producing
# it, the leg degenerates to "synth of a reused yosys library works".
cat > "$W/y2.v" <<'EOF'
module top(input clk, input [3:0] a2, input [3:0] b, output reg [3:0] y);
  always @(posedge clk) y <= a2 + b;
endmodule
EOF
"$LHD" compile "$W/d1.v" --reader yosys-verilog --emit-dir lg:"$W/Ly" --workdir "$W/wy1" -q \
  --result-json "$W/y1.json" >/dev/null || fail "yosys compile failed: $(cat "$W/y1.json" 2>/dev/null)"
"$LHD" compile "$W/y2.v" --reader yosys-verilog --emit-dir lg:"$W/Ly" --workdir "$W/wy2" -q \
  --result-json "$W/y2.json" >/dev/null || fail "yosys recompile failed: $(cat "$W/y2.json" 2>/dev/null)"
clash=$(awk '/^graph_io /{g=$3} /^  (input|output) /{k=g" "$2; if (seen[k]++) print k}' "$W/Ly/library.txt")
for mapper in abc usyn; do
  "$LHD" synth lg:"$W/Ly" --set synth.liberty="$LIB" --set synth.opentimer=false --set synth.mapper="$mapper" \
    --workdir "$W/wys_$mapper" -q \
    --result-json "$W/ys_$mapper.json" >"$W/ys_$mapper.log" 2>&1
  st=$?
  not_a_crash "$st" "$W/ys_$mapper.log"
  if [ -n "$clash" ]; then
    [ "$st" -ne 0 ] || fail "$mapper: synth accepted a GraphIO with two ports on one id ($clash)"
    grep -q "on the same port id" "$W/ys_$mapper.json" \
      || fail "$mapper: missing io-port-clash diagnostic: $(cat "$W/ys_$mapper.json")"
    [ "$st" -eq 4 ] || fail "$mapper: a drifted lg: library is a config error (exit 4), got exit $st: $(cat "$W/ys_$mapper.json")"
  else
    [ "$st" -eq 0 ] || fail "$mapper: synth of a reused yosys library failed: $(cat "$W/ys_$mapper.json")"
  fi
done
if [ -n "$clash" ]; then
  echo "PASS: pass.partition refuses two ports on one port id with a config diagnostic (both mappers)"
else
  echo "PASS: synth of a reused yosys-reader library (the reader no longer writes a port clash)"
fi

# A library listing without hhds tombstones (`graph_io_deleted`): a pruned
# module leaves one in a reused dir, and a cold dir never saw the module.
live_lib() {
  grep -v '^graph_io_deleted ' "$1"
}

# The design legs 5, 8 and 9 edit: top -> mid -> leaf, and top -> other.
closure_v1() {
  mkdir -p "$1"
  cat > "$1/leaf.prp" <<'EOF'
pub mod lf(a:U8, b:U8) -> (q:U8@[1]) {
  reg r:U8 = 0
  q = r
  wrap r = a - b
}
EOF
  cat > "$1/mid.prp" <<'EOF'
const lf = import("leaf.lf")
pub mod md(x:U8, y:U8) -> (q:U8@[1]) {
  mut l = lf::[name=l](a = x, b = y)
  q = l.q
}
EOF
  cat > "$1/other.prp" <<'EOF'
pub mod ot(a:U8, b:U8) -> (q:U8@[1]) {
  reg r:U8 = 0
  q = r
  wrap r = a + b
}
EOF
  cat > "$1/top.prp" <<'EOF'
const md = import("mid.md")
const ot = import("other.ot")
pub mod tp(x:U8, y:U8) -> (o:U8@[1], p:U8@[1]) {
  mut m = md::[name=m](x = x, y = y)
  mut t = ot::[name=t](a = x, b = y)
  o = m.q
  p = t.q
}
EOF
}

# The edit: top instantiates leaf directly (mid leaves the import closure;
# other stays clean) and leaf swaps its inputs, so mid.md's Sub binds stale
# port ids. $2 = the leaf call's arguments: a bad list is a failing edit.
closure_v2() {
  leaf_swapped "$1"
  top_direct "$1" "$2"
}
leaf_swapped() {
  cat > "$1/leaf.prp" <<'EOF'
pub mod lf(b:U8, a:U8) -> (q:U8@[1]) {
  reg r:U8 = 0
  q = r
  wrap r = a - b
}
EOF
}
top_direct() {  # top.prp only: leaf directly, mid out of the closure
  cat > "$1/top.prp" <<EOF
const lf = import("leaf.lf")
const ot = import("other.ot")
pub mod tp(x:U8, y:U8) -> (o:U8@[1], p:U8@[1]) {
  mut m = lf::[name=m]($2)
  mut t = ot::[name=t](a = x, b = y)
  o = m.q
  p = t.q
}
EOF
}

# $1 = the leg's dir, $2 = what the leg did: its warm library L (tombstones
# aside) and warm.v must equal a cold compile of the same sources ($3.. = the
# warm compile's extra options, which the cold one repeats).
closure_matches_cold() {
  local dir=$1 what=$2
  shift 2
  "$LHD" compile "$dir/top.prp" --top tp --emit-dir lg:"$dir/F" --emit verilog:"$dir/cold.v" "$@" -q \
    --result-json "$dir/cold.json" >/dev/null || fail "$what: cold pyrope compile failed: $(cat "$dir/cold.json" 2>/dev/null)"
  grep -q '^graph_io [0-9]* mid.md$' "$dir/L/library.txt" \
    && fail "$what: the dropped unit's mid.md was not pruned: $(cat "$dir/L/library.txt")"
  live_lib "$dir/L/library.txt" >"$dir/L.live"
  live_lib "$dir/F/library.txt" >"$dir/F.live"
  cmp -s "$dir/L.live" "$dir/F.live" || fail "$what: incremental library differs from a cold one: $(diff "$dir/L.live" "$dir/F.live")"
  [ -s "$dir/cold.v" ] || fail "$what: cold compile emitted no Verilog"
  cmp -s "$dir/warm.v" "$dir/cold.v" || fail "$what: incremental Verilog differs from a cold one: $(diff "$dir/warm.v" "$dir/cold.v")"
}

# ── 5. Pyrope: import-closure change + child reorder, incremental ─────────────
P="$W/closure"
closure_v1 "$P"
"$LHD" compile "$P/top.prp" --top tp --emit-dir lg:"$P/L" --workdir "$P/w" -q --result-json "$P/c1.json" >/dev/null \
  || fail "first incremental pyrope compile failed: $(cat "$P/c1.json" 2>/dev/null)"
grep -q '^graph_io [0-9]* mid.md$' "$P/L/library.txt" || fail "the first compile did not emit mid.md: $(cat "$P/L/library.txt")"
closure_v2 "$P" "a = x, b = y"
"$LHD" compile "$P/top.prp" --top tp --emit-dir lg:"$P/L" --emit verilog:"$P/warm.v" --workdir "$P/w" -q \
  --result-json "$P/c2.json" >"$P/c2.log" 2>&1
st=$?
not_a_crash "$st" "$P/c2.log"
[ "$st" -eq 0 ] || fail "incremental compile after an import-closure change was refused: $(cat "$P/c2.json")"
closure_matches_cold "$P" "closure change"
echo "PASS: an import-closure change with a child reorder recompiles incrementally, equal to cold"

# ── 6. Verilog: a leftover instantiator of a reordered child is refused ──────
cat > "$W/v1.v" <<'EOF'
module leaf(input clk, input [3:0] a, input [3:0] b, output reg [3:0] q);
  always @(posedge clk) q <= a - b;
endmodule
module mid(input clk, input [3:0] x, input [3:0] y, output [3:0] q);
  leaf l(.clk(clk), .a(x), .b(y), .q(q));
endmodule
module top(input clk, input [3:0] x, input [3:0] y, output [3:0] o);
  mid m(.clk(clk), .x(x), .y(y), .q(o));
endmodule
EOF
cat > "$W/v2.v" <<'EOF'
module leaf(input clk, input [3:0] b, input [3:0] a, output reg [3:0] q);
  always @(posedge clk) q <= a - b;
endmodule
module top(input clk, input [3:0] x, input [3:0] y, output [3:0] o);
  leaf l(.clk(clk), .a(x), .b(y), .q(o));
endmodule
EOF
"$LHD" compile "$W/v1.v" --top top --emit-dir lg:"$W/Lv" --workdir "$W/wv1" -q --result-json "$W/v1.json" >/dev/null \
  || fail "verilog compile into Lv failed: $(cat "$W/v1.json" 2>/dev/null)"
"$LHD" compile "$W/v2.v" --top top --emit-dir lg:"$W/Lv" --emit verilog:"$W/vstale.v" --workdir "$W/wv2" -q \
  --result-json "$W/v2.json" >"$W/v2.log" 2>&1
st=$?
not_a_crash "$st" "$W/v2.log"
[ "$st" -ne 0 ] || fail "a leftover verilog instance of a reordered child was silently rewired"
[ "$st" -eq 4 ] || fail "a stale lg: library is a config error (exit 4), got exit $st: $(cat "$W/v2.json")"
grep -q "instantiates it with the old ports" "$W/v2.json" || fail "missing stale-instance diagnostic: $(cat "$W/v2.json")"
grep -q "the lg: library $W/Lv still holds 'mid', a module this compile does not define" "$W/v2.json" \
  || fail "the hint does not name the leftover module and its lg: dir: $(cat "$W/v2.json")"
echo "PASS: a leftover verilog instance of a reordered child is refused with a rebuild hint"

# ── 7. the flattener: two ports on one port id in a CHILD GraphIO ─────────────
cat > "$W/k1.v" <<'EOF'
(* keep_hierarchy *) module sub(input clk, input [3:0] x, input [3:0] w, output reg [3:0] q);
  always @(posedge clk) q <= x + w;
endmodule
module top(input clk, input [3:0] a, input [3:0] b, output [3:0] y);
  sub s(.clk(clk), .x(a ^ b), .w(b), .q(y));
endmodule
EOF
cat > "$W/k2.v" <<'EOF'
(* keep_hierarchy *) module sub(input clk, input [3:0] x2, input [3:0] w, output reg [3:0] q);
  always @(posedge clk) q <= x2 + w;
endmodule
module top(input clk, input [3:0] a, input [3:0] b, output [3:0] y);
  sub s(.clk(clk), .x2(a ^ b), .w(b), .q(y));
endmodule
EOF
"$LHD" compile "$W/k1.v" --reader yosys-verilog --emit-dir lg:"$W/Lk" --workdir "$W/wk1" -q \
  --result-json "$W/k1.json" >/dev/null || fail "yosys compile of k1 failed: $(cat "$W/k1.json" 2>/dev/null)"
"$LHD" compile "$W/k2.v" --reader yosys-verilog --emit-dir lg:"$W/Lk" --workdir "$W/wk2" -q \
  --result-json "$W/k2.json" >/dev/null || fail "yosys recompile of k2 failed: $(cat "$W/k2.json" 2>/dev/null)"
kclash=$(awk '/^graph_io /{g=$3} /^  (input|output) /{k=g" "$2; if (seen[k]++) print k}' "$W/Lk/library.txt")
for mapper in abc usyn; do
  "$LHD" synth lg:"$W/Lk" --top top --set synth.liberty="$LIB" --set synth.opentimer=false --set synth.mapper="$mapper" \
    --workdir "$W/wks_$mapper" -q --result-json "$W/ks_$mapper.json" >"$W/ks_$mapper.log" 2>&1
  st=$?
  not_a_crash "$st" "$W/ks_$mapper.log"
  if [ -n "$kclash" ]; then
    [ "$st" -ne 0 ] || fail "$mapper: synth flattened a child GraphIO with two ports on one id ($kclash)"
    grep -q "'sub' (instance 's') declares ports 'x' and 'x2' on the same port id" "$W/ks_$mapper.json" \
      || fail "$mapper: missing flatten io-port-clash diagnostic: $(cat "$W/ks_$mapper.json")"
    [ "$st" -eq 4 ] || fail "$mapper: a drifted lg: library is a config error (exit 4), got exit $st: $(cat "$W/ks_$mapper.json")"
  else
    [ "$st" -eq 0 ] || fail "$mapper: synth of a reused yosys library failed: $(cat "$W/ks_$mapper.json")"
  fi
done
if [ -n "$kclash" ]; then
  echo "PASS: the flattener refuses a child with two ports on one port id with a config diagnostic (both mappers)"
else
  echo "PASS: synth of a reused keep_hierarchy yosys library (the reader no longer writes a port clash)"
fi

# ── 8. the same edit, first attempted with mistakes ──────────────────────────
# A failing compile republishes the compile cache's source manifest (now
# without mid) but leaves the lg: library and the stored graph generation
# alone, so mid.md must still count as this scope's leftover when the fixed
# edit compiles -- not as a foreign module, refused on every retry.
Q="$W/closure_fail"
closure_v1 "$Q"
"$LHD" compile "$Q/top.prp" --top tp --emit-dir lg:"$Q/L" --workdir "$Q/w" -q --result-json "$Q/c1.json" >/dev/null \
  || fail "failing-edit leg: first compile failed: $(cat "$Q/c1.json" 2>/dev/null)"
n=0
for bad in "a = x, c = y" "a = x"; do  # an unknown argument, then a missing one
  n=$((n + 1))
  closure_v2 "$Q" "$bad"
  "$LHD" compile "$Q/top.prp" --top tp --emit-dir lg:"$Q/L" --workdir "$Q/w" -q \
    --result-json "$Q/bad$n.json" >"$Q/bad$n.log" 2>&1
  st=$?
  not_a_crash "$st" "$Q/bad$n.log"
  [ "$st" -ne 0 ] || fail "failing-edit leg: the broken call ($bad) compiled"
  grep -q "instantiates it with the old ports" "$Q/bad$n.json" \
    && fail "failing-edit leg: the broken call ($bad) was refused as a stale instance: $(cat "$Q/bad$n.json")"
done
grep -q '^graph_io [0-9]* mid.md$' "$Q/L/library.txt" || fail "failing-edit leg: a failed compile rewrote the lg: library"
closure_v2 "$Q" "a = x, b = y"
for try in 1 2; do  # the fixed edit, then an unchanged warm rerun
  "$LHD" compile "$Q/top.prp" --top tp --emit-dir lg:"$Q/L" --emit verilog:"$Q/warm.v" --workdir "$Q/w" -q \
    --result-json "$Q/fix$try.json" >"$Q/fix$try.log" 2>&1
  st=$?
  not_a_crash "$st" "$Q/fix$try.log"
  [ "$st" -eq 0 ] || fail "failing-edit leg: compile $try after the failed attempts was refused: $(cat "$Q/fix$try.json")"
done
closure_matches_cold "$Q" "closure change after failed attempts"
echo "PASS: the closure change still recompiles incrementally after failed attempts, equal to cold"

# ── 9. the same edit under a changed compile option ──────────────────────────
# The option changes the compile cache context (a cache miss), not which
# design the scope holds: mid.md is still this scope's leftover.
X="$W/closure_ctx"
closure_v1 "$X"
"$LHD" compile "$X/top.prp" --top tp --emit-dir lg:"$X/L" --workdir "$X/w" -q --result-json "$X/c1.json" >/dev/null \
  || fail "option-change leg: first compile failed: $(cat "$X/c1.json" 2>/dev/null)"
closure_v2 "$X" "a = x, b = y"
"$LHD" compile "$X/top.prp" --top tp --emit-dir lg:"$X/L" --emit verilog:"$X/warm.v" --workdir "$X/w" \
  --set upass.verifier=true -q --result-json "$X/c2.json" >"$X/c2.log" 2>&1
st=$?
not_a_crash "$st" "$X/c2.log"
[ "$st" -eq 0 ] || fail "option-change leg: the closure change under a new option was refused: $(cat "$X/c2.json")"
closure_matches_cold "$X" "closure change under a new option" --set upass.verifier=true
echo "PASS: the closure change under a changed compile option recompiles incrementally, equal to cold"

# A second design in the same directory: top2 -> mid -> leaf.
top2_v1() {
  cat > "$1/top2.prp" <<'EOF'
const md = import("mid.md")
pub mod tp2(x:U8, y:U8) -> (o:U8@[1]) {
  mut m = md::[name=m](x = x, y = y)
  o = m.q
}
EOF
}

# ── 10. a shared lg: dir: another design still holds the dropped module ──────
for failed_first in 0 1; do
  S="$W/shared_$failed_first"
  closure_v1 "$S"
  top2_v1 "$S"
  "$LHD" compile "$S/top.prp" --top tp --emit-dir lg:"$S/L" --workdir "$S/w" -q --result-json "$S/a1.json" >/dev/null \
    || fail "shared-dir leg: first tp compile failed: $(cat "$S/a1.json" 2>/dev/null)"
  if [ "$failed_first" = 1 ]; then
    top_direct "$S" "a = x, c = y"
    "$LHD" compile "$S/top.prp" --top tp --emit-dir lg:"$S/L" --workdir "$S/w" -q --result-json "$S/a2.json" >/dev/null 2>&1 \
      && fail "shared-dir leg: the broken call compiled"
  fi
  "$LHD" compile "$S/top2.prp" --top tp2 --emit-dir lg:"$S/L" --workdir "$S/w" -q --result-json "$S/b1.json" >/dev/null \
    || fail "shared-dir leg: the tp2 compile failed: $(cat "$S/b1.json" 2>/dev/null)"
  top_direct "$S" "a = x, b = y"
  "$LHD" compile "$S/top.prp" --top tp --emit-dir lg:"$S/L" --workdir "$S/w" -q --result-json "$S/a3.json" >"$S/a3.log" 2>&1
  st=$?
  not_a_crash "$st" "$S/a3.log"
  [ "$st" -eq 0 ] || fail "shared-dir leg: tp dropping mid failed: $(cat "$S/a3.json")"
  grep -q '^graph_io [0-9]* mid.md$' "$S/L/library.txt" \
    || fail "shared-dir leg ($failed_first): tp pruned mid.md, which tp2 still instantiates: $(cat "$S/L/library.txt")"
  "$LHD" compile lg:"$S/L" --top top2.tp2 --emit verilog:"$S/tp2.v" -q --result-json "$S/e1.json" >/dev/null \
    || fail "shared-dir leg: tp2 no longer compiles from the shared library: $(cat "$S/e1.json" 2>/dev/null)"
  grep -q '^module md' "$S/tp2.v" || fail "shared-dir leg ($failed_first): tp2's netlist lost module md: $(cat "$S/tp2.v")"
done
echo "PASS: dropping a module from one design's closure keeps it for another design sharing the lg: dir"

# ── 11. the shared dir, with the shared leaf reordered ───────────────────────
S="$W/shared_reorder"
closure_v1 "$S"
top_direct "$S" "a = x, b = y"
"$LHD" compile "$S/top.prp" --top tp --emit-dir lg:"$S/L" --workdir "$S/w" -q --result-json "$S/a1.json" >/dev/null \
  || fail "shared-reorder leg: first tp compile failed: $(cat "$S/a1.json" 2>/dev/null)"
"$LHD" compile "$S/mid.prp" --top md --emit-dir lg:"$S/L" --workdir "$S/w" -q --result-json "$S/b1.json" >/dev/null \
  || fail "shared-reorder leg: the md compile failed: $(cat "$S/b1.json" 2>/dev/null)"
cp "$S/L/library.txt" "$S/before.txt"
leaf_swapped "$S"
for work in "$S/w" ""; do  # incremental (a partial restore), then no workdir
  "$LHD" compile "$S/top.prp" --top tp --emit-dir lg:"$S/L" ${work:+--workdir "$work"} --emit verilog:"$S/warm.v" -q \
    --result-json "$S/a2.json" >"$S/a2.log" 2>&1
  st=$?
  not_a_crash "$st" "$S/a2.log"
  [ "$st" -ne 0 ] || fail "shared-reorder leg (workdir '$work'): md's instance of the reordered leaf was silently rewired"
  [ "$st" -eq 4 ] || fail "shared-reorder leg (workdir '$work'): a stale instance is a config error (exit 4), got $st: $(cat "$S/a2.json")"
  grep -q "'mid.md' (already in the lg: library, not rebuilt by this compile) instantiates it with the old ports" "$S/a2.json" \
    || fail "shared-reorder leg (workdir '$work'): missing stale-instance diagnostic: $(cat "$S/a2.json")"
  cmp -s "$S/L/library.txt" "$S/before.txt" || fail "shared-reorder leg (workdir '$work'): a refused compile rewrote the library"
done
echo "PASS: an incremental compile reordering a module another design instantiates is refused, like a cold one"

# ── 12. ownership is recorded with the library save ─────────────────────────
X="$W/owner_save"
closure_v1 "$X"
"$LHD" compile "$X/top.prp" --top tp --emit-dir lg:"$X/L" --workdir "$X/w" -q --result-json "$X/c1.json" >/dev/null \
  || fail "owner-save leg: first compile failed: $(cat "$X/c1.json" 2>/dev/null)"
cat > "$X/extra.prp" <<'EOF'
const lf = import("leaf.lf")
pub mod ex(x:U8, y:U8) -> (q:U8@[1]) {
  mut l = lf::[name=l](a = x, b = y)
  q = l.q
}
EOF
cat > "$X/top.prp" <<'EOF'
const ex = import("extra.ex")
const ot = import("other.ot")
pub mod tp(x:U8, y:U8) -> (o:U8@[1], p:U8@[1]) {
  mut m = ex::[name=m](x = x, y = y)
  mut t = ot::[name=t](a = x, b = y)
  o = m.q
  p = t.q
}
EOF
mkdir -p "$X/ro"
chmod 500 "$X/ro"  # the Verilog emit, after lg.save, fails
"$LHD" compile "$X/top.prp" --top tp --emit-dir lg:"$X/L" --emit verilog:"$X/ro/x.v" --workdir "$X/w" -q \
  --result-json "$X/c2.json" >"$X/c2.log" 2>&1
st=$?
chmod 700 "$X/ro"
not_a_crash "$st" "$X/c2.log"
[ "$st" -ne 0 ] || fail "owner-save leg: the Verilog emit into a read-only dir succeeded"
grep -q '^graph_io [0-9]* extra.ex$' "$X/L/library.txt" || fail "owner-save leg: the failing compile did not save extra.ex first"
closure_v2 "$X" "a = x, c = y"
"$LHD" compile "$X/top.prp" --top tp --emit-dir lg:"$X/L" --workdir "$X/w" -q --result-json "$X/c3.json" >/dev/null 2>&1 \
  && fail "owner-save leg: the broken call compiled"
closure_v2 "$X" "a = x, b = y"
"$LHD" compile "$X/top.prp" --top tp --emit-dir lg:"$X/L" --emit verilog:"$X/warm.v" --workdir "$X/w" -q \
  --result-json "$X/c4.json" >"$X/c4.log" 2>&1
st=$?
not_a_crash "$st" "$X/c4.log"
[ "$st" -eq 0 ] || fail "owner-save leg: the fixed edit was refused: $(cat "$X/c4.json")"
grep -q '^graph_io [0-9]* extra.ex$' "$X/L/library.txt" && fail "owner-save leg: this design's leftover extra.ex was not pruned"
closure_matches_cold "$X" "ownership recorded with the library save"
echo "PASS: a module that a compile failing after lg.save left is still this design's leftover, pruned like cold"

# ── 13. a partial restore measures dirtiness against the stored generation ──
# leaf swaps its ports in a compile that fails past the parse: that republishes
# the compile cache's manifest (leaf now clean against it) but stores no graph
# generation. The fixed compile saw mid clean too and restored mid.md's stored
# body, whose instance still bound leaf's OLD port ids: `lf l(.b(x), .a(y))`
# for `lf(a = x, b = y)`, a silent miscompile. mid must be rebuilt.
G="$W/stale_generation"
closure_v1 "$G"
"$LHD" compile "$G/top.prp" --top tp --emit-dir lg:"$G/L" --workdir "$G/w" -q --result-json "$G/c1.json" >/dev/null \
  || fail "stale-generation leg: first compile failed: $(cat "$G/c1.json" 2>/dev/null)"
cp "$G/top.prp" "$G/top.good"
leaf_swapped "$G"
sed 's/md::\[name=m\](/md::[name=m](zq = x, /' "$G/top.good" > "$G/top.prp"
"$LHD" compile "$G/top.prp" --top tp --emit-dir lg:"$G/L" --workdir "$G/w" -q --result-json "$G/c2.json" >/dev/null 2>&1 \
  && fail "stale-generation leg: the unknown argument compiled"
grep -q 'unknown argument `zq`' "$G/c2.json" || fail "stale-generation leg: the edit did not fail past the parse: $(cat "$G/c2.json")"
cp "$G/top.good" "$G/top.prp"
"$LHD" compile "$G/top.prp" --top tp --emit-dir lg:"$G/L" --emit verilog:"$G/warm.v" --workdir "$G/w" -q \
  --result-json "$G/c3.json" >"$G/c3.log" 2>&1
st=$?
not_a_crash "$st" "$G/c3.log"
[ "$st" -eq 0 ] || fail "stale-generation leg: the fixed compile failed: $(cat "$G/c3.json")"
"$LHD" compile "$G/top.prp" --top tp --emit verilog:"$G/cold.v" -q --result-json "$G/cold.json" >/dev/null \
  || fail "stale-generation leg: cold compile failed: $(cat "$G/cold.json" 2>/dev/null)"
cmp -s "$G/warm.v" "$G/cold.v" || fail "stale-generation leg: incremental Verilog differs from cold: $(diff "$G/warm.v" "$G/cold.v")"
echo "PASS: a partial restore after a failed interface edit rebuilds the importers, equal to cold"

# ── 14. an unchanged design never restores another design's modules ─────────
R="$W/shared_revert"
mkdir -p "$R"
cat > "$R/g.prp" <<'EOF'
pub mod sib<W=4>(a:Unsigned(bits=W * 2)) -> (y:Unsigned(bits=W)@[0]) { wrap y = a }
EOF
design_t() {  # $1 = name, $2 = the constant it adds, $3 = sib's W
  cat > "$R/$1.prp" <<EOF
const sib = import("g.sib")
pub mod $1(a:U8) -> (y:U8@[0]) {
  const s = sib<W=$3>(a=a)
  wrap y = s.y + $2
}
EOF
}
design_t ta 1 4
design_t tb 7 4
for d in ta tb; do
  "$LHD" compile "$R/$d.prp" --top "$d" --emit-dir lg:"$R/L" --workdir "$R/w_$d" -q --result-json "$R/${d}1.json" >/dev/null \
    || fail "shared-revert leg: first $d compile failed: $(cat "$R/${d}1.json" 2>/dev/null)"
done
design_t ta 3 5
"$LHD" compile "$R/ta.prp" --top ta --emit-dir lg:"$R/L" --workdir "$R/w_ta" -q --result-json "$R/ta2.json" >/dev/null \
  || fail "shared-revert leg: the ta edit failed: $(cat "$R/ta2.json" 2>/dev/null)"
"$LHD" compile lg:"$R/L" --top ta.ta --emit verilog:"$R/cold.v" -q --result-json "$R/cold.json" >/dev/null \
  || fail "shared-revert leg: ta does not compile from the library: $(cat "$R/cold.json" 2>/dev/null)"
grep -q "sib__U10_W_5" "$R/cold.v" || fail "shared-revert leg: the edited ta does not bind sib<W=5>: $(cat "$R/cold.v")"
for emit in "" "--emit verilog:$R/tb.v"; do
  # shellcheck disable=SC2086  # $emit is empty or one option pair
  "$LHD" compile "$R/tb.prp" --top tb --emit-dir lg:"$R/L" $emit --workdir "$R/w_tb" -q --result-json "$R/tb2.json" \
    >"$R/tb2.log" 2>&1
  st=$?
  not_a_crash "$st" "$R/tb2.log"
  [ "$st" -eq 0 ] || fail "shared-revert leg: the unchanged tb compile failed: $(cat "$R/tb2.json")"
  "$LHD" compile lg:"$R/L" --top ta.ta --emit verilog:"$R/ta_from_L.v" -q --result-json "$R/e.json" >/dev/null \
    || fail "shared-revert leg: ta no longer compiles from the shared library: $(cat "$R/e.json" 2>/dev/null)"
  cmp -s "$R/ta_from_L.v" "$R/cold.v" \
    || fail "shared-revert leg (tb emit '$emit'): tb reverted ta's modules in the library: $(diff "$R/ta_from_L.v" "$R/cold.v")"
done
echo "PASS: an unchanged design's warm compile leaves another design's rebuilt modules alone"

# ── 15. one design under several command lines, plus a tb, on one workdir ────
V="$W/spellings"
closure_v1 "$V"
cat > "$V/tb.prp" <<'EOF'
const tp = import("top.tp")
pub mod tb(x:U8, y:U8) -> (o:U8@[1], p:U8@[1]) {
  mut d = tp::[name=d](x = x, y = y)
  o = d.o
  p = d.p
}
EOF
spell() {  # $1 = tag, rest = the command line's own options
  local tag=$1
  shift
  "$LHD" compile "$@" --emit-dir lg:"$V/L" --emit verilog:"$V/$tag.v" --workdir "$V/w" -q --result-json "$V/$tag.json" \
    >"$V/$tag.log" 2>&1
  st=$?
  not_a_crash "$st" "$V/$tag.log"
  [ "$st" -eq 0 ] || fail "spellings leg: compile $tag failed: $(cat "$V/$tag.json")"
}
spell b1 "$V/top.prp" --top tp
spell a1 "$V/top.prp"
spell t1 "$V/tb.prp" --top tb
closure_v2 "$V" "a = x, b = y"
for round in 2 3; do  # the edit, then unchanged reruns
  spell "b$round" "$V/top.prp" --top tp
  spell "a$round" "$V/top.prp"
  spell "t$round" "$V/tb.prp" --top tb
done
grep -q '^graph_io [0-9]* mid.md$' "$V/L/library.txt" && fail "spellings leg: nothing holds mid.md, yet it was kept"
"$LHD" compile "$V/top.prp" --top tp --emit verilog:"$V/cold_tp.v" -q --result-json "$V/cold_tp.json" >/dev/null \
  || fail "spellings leg: cold tp compile failed: $(cat "$V/cold_tp.json" 2>/dev/null)"
"$LHD" compile "$V/tb.prp" --top tb --emit verilog:"$V/cold_tb.v" -q --result-json "$V/cold_tb.json" >/dev/null \
  || fail "spellings leg: cold tb compile failed: $(cat "$V/cold_tb.json" 2>/dev/null)"
cmp -s "$V/b3.v" "$V/cold_tp.v" || fail "spellings leg: incremental tp Verilog differs from cold: $(diff "$V/b3.v" "$V/cold_tp.v")"
cmp -s "$V/t3.v" "$V/cold_tb.v" || fail "spellings leg: incremental tb Verilog differs from cold: $(diff "$V/t3.v" "$V/cold_tb.v")"
echo "PASS: a closure change compiles under every command line sharing a workdir, equal to cold"

echo "lhd_lg_reuse_io_test passed"
