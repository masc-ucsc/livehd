#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Regressions for the slang reader's `wire` classification and the multi-write
# `wire` split — the two changes that made the emitted Pyrope recompile and LEC
# against the source (before, `--emit-dir pyrope:` over-used `wire`: a whole
# datapath fused into one false SCC by coarse per-instance modeling was blanket-
# declared `wire`, and a `wire` written more than once emitted N drivers, which
# is illegal for a single-driver net).
#
#  (1) BACK-EDGE ONLY: a net is `wire` only when a reader emits before its writer
#      in the emission order (a genuine read-before-write). A false-SCC net whose
#      writer precedes its reader stays `mut`.
#  (2) SPLIT: a `wire` that is multiply written (case + priority-if, bit-slice
#      chain, multiple drivers, a partial/conditional proc write, or an instance
#      output wired through a slice) is split into a `mut <net>__wtmp`
#      accumulator (program-order writes) + a single `<net> = <net>__wtmp` bridge
#      (the wire's one driver); cross-driver reads see the resolved wire.
#  (3) LATE STATEFUL CALLEE: a parent may lower before the body of an imported
#      stateful child. Once every body exists, the child's Sub must be refreshed
#      as a loop break so Verilog cgen orders a forward wire's producer before
#      its blocking consumer instead of emitting the whole feedback SCC in
#      storage order.
#  (4) GENERATED OUTPUT ALIAS: a child output written into cgen's temporary
#      wire, then copied through an always_comb alias, must survive a coarse
#      cross-instance SCC.  In particular a constant-ready child cannot lose
#      that output merely because a dispatcher feeds one of its inputs.
#
# Both are verified the strong way: the emitted Pyrope must recompile AND cvc5-
# prove equivalent to the original Verilog.

set -u
LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_slang_wire_$$}"
mkdir -p "$W"
fail() { echo "FAIL: $*" >&2; exit 1; }

# ── (1) false SCC (coarse instance) → NO wire, and it still LECs ───────────────
cat >"$W/bedge.sv" <<'EOF'
module bsub (input logic [3:0] a_i, output logic [3:0] x_o, output logic [3:0] y_o);
  assign x_o = 4'd5;        // constant: does NOT depend on a_i → the SCC is false
  assign y_o = a_i + 4'd1;
endmodule

module bedge (input logic [1:0] op_i, input logic c_i, output logic [3:0] z_o, output logic [3:0] w_o);
  logic [3:0] foo, bar, un;
  bsub u (.a_i(foo), .x_o(bar), .y_o(un));   // reads foo, writes bar → false cycle
  always_comb begin                           // MULTIPLY-written foo → needs the split
    case (op_i)
      2'd0:    foo = 4'd3;
      2'd1:    foo = bar;
      default: foo = 4'd0;
    endcase
    if (c_i) foo = 4'd9;
  end
  assign z_o = foo;
  assign w_o = un;
endmodule
EOF
$LHD compile "$W/bedge.sv" --top bedge --emit-dir pyrope:"$W/bv" --workdir "$W/bw" -q \
  || fail "false-SCC + multi-write design did not compile"
grep -q '__wtmp' "$W/bv/bedge.prp" || fail "expected a wire split (mut __wtmp accumulator) for the multi-write cyclic net"
# The emitted Pyrope must RECOMPILE (a bare multi-driver wire would not)…
$LHD compile "$W/bv"/*.prp --top bedge.bedge --workdir "$W/brc" -q \
  || fail "emitted Pyrope (with wire split) did not recompile"
# …and be PROVEN equivalent to the original Verilog.
$LHD lec --impl pyrope:"$W/bv"/ --impl-top bedge.bedge --ref verilog:"$W/bedge.sv" --ref-top bedge \
  --set formal.solver=cvc5 --workdir "$W/blec" -q --result-json "$W/blec.json" \
  || fail "wire-split design not proven equivalent: $(cat "$W/blec.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/blec.json" || fail "bedge lec not pass: $(cat "$W/blec.json")"
echo "PASS: false-SCC multi-write net splits (mut accumulator + wire bridge), recompiles, and LECs"

# ── (2) a plain acyclic design uses NO wire (all mut/const, none over-promoted) ─
cat >"$W/plain.sv" <<'EOF'
module plain (input logic [1:0] op_i, input logic c_i, input logic [3:0] x_i, output logic [3:0] y_o);
  logic [3:0] t;
  always_comb begin
    case (op_i)
      2'd0:    t = x_i;
      2'd1:    t = x_i + 4'd1;
      default: t = 4'd0;
    endcase
    if (c_i) t = 4'd9;
  end
  assign y_o = t;
endmodule
EOF
$LHD compile "$W/plain.sv" --top plain --emit-dir pyrope:"$W/pv" --workdir "$W/pw" -q \
  || fail "plain acyclic design did not compile"
# No read-before-write anywhere ⇒ NO wire at all (t is a plain `mut`).
if grep -qE '  wire ' "$W/pv/plain.prp"; then
  fail "acyclic design should emit no wire (over-promotion): $(grep '  wire ' "$W/pv/plain.prp")"
fi
$LHD compile "$W/pv"/*.prp --top plain.plain --workdir "$W/prc" -q || fail "plain design did not recompile"
echo "PASS: acyclic multi-write net stays mut (no wire over-promotion), recompiles"

# ── (3) stateful child body built after parent → refresh the Sub loop break ────
cat >"$W/State.prp" <<'EOF'
pub mod State(clock:u1, d:u8) -> (q:u8@[]) {
  reg r:u8 = 0
  r = d
  q = r
}
EOF
cat >"$W/Top.prp" <<'EOF'
const State = import("State.State")

pub mod Top::[timecheck=false](clock:u1, a:u8, sel:u1) -> (y:u8@[]) {
  wire q:u8 = nil
  mut m:u8 = if sel != 0 { q } else { a }
  mut state = State::[name=state](clock=clock, d=m)
  q = state
  y = q
}
EOF
$LHD compile "$W/Top.prp" --top Top --recipe O0 --emit-dir verilog:"$W/hrv" --workdir "$W/hrw" -q \
  || fail "late stateful-callee hierarchy did not emit Verilog"
TOP_V="$W/hrv/Top.Top.v"
producer_line=$(grep -nE '= state_o[0-9]+;' "$TOP_V" | head -1 | cut -d: -f1)
consumer_line=$(grep -nE 'mux_[0-9]+ = q;' "$TOP_V" | head -1 | cut -d: -f1)
[ -n "$producer_line" ] && [ -n "$consumer_line" ] \
  || fail "stateful hierarchy fixture lost its producer/consumer shape: $(sed -n '1,120p' "$TOP_V")"
[ "$producer_line" -lt "$consumer_line" ] \
  || fail "stateful Sub stayed combinational: cgen emitted q's consumer on line $consumer_line before its producer on line $producer_line"
$LHD compile "$W/hrv/State.State.v" "$TOP_V" --top Top --workdir "$W/hrr" -q \
  || fail "stateful hierarchy's generated Verilog did not read back"
echo "PASS: late stateful callee is refreshed as a loop break before Verilog emission"

# ── (4) child output -> generated temporary -> procedural alias in SCC ───
cat >"$W/output_alias.sv" <<'EOF'
module ready_child(input logic valid_i, output logic ready_o, output logic seen_o);
  assign ready_o = 1'b1;       // independent of valid_i: hierarchy SCC is false
  assign seen_o = valid_i;
endmodule

module ready_dispatch(input logic ready_i, output logic valid_o, output logic ready_o);
  assign valid_o = ready_i;
  assign ready_o = ready_i;
endmodule

module output_alias(output logic ready_o, output logic seen_o);
  wire child_ready;
  wire dispatch_valid;
  logic ready_alias;
  ready_child child(.valid_i(dispatch_valid), .ready_o(child_ready), .seen_o(seen_o));
  ready_dispatch dispatch(.ready_i(ready_alias), .valid_o(dispatch_valid), .ready_o(ready_o));
  always_comb begin
    ready_alias = child_ready;
  end
endmodule
EOF
$LHD compile "$W/output_alias.sv" --top output_alias --emit-dir pyrope:"$W/av" --workdir "$W/aw" -q \
  || fail "generated child-output alias fixture did not emit Pyrope"
$LHD lec --impl pyrope:"$W/av"/ --impl-top output_alias.output_alias \
  --ref verilog:"$W/output_alias.sv" --ref-top output_alias \
  --set formal.solver=cvc5 --workdir "$W/alec" -q --result-json "$W/alec.json" \
  || fail "generated child-output alias was not preserved: $(cat "$W/alec.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/alec.json" || fail "output-alias lec not pass: $(cat "$W/alec.json")"
# Recompile that correct Pyrope through cgen. Its stable node order deliberately
# emits the two generated temporaries after their consumers inside one
# always_comb. Reading that RTL with slang must recover the simultaneous
# combinational equations, not freeze the first-pass X values.
$LHD compile "$W/av"/*.prp --top output_alias.output_alias --recipe O0 \
  --emit verilog:"$W/output_alias_all.v" --workdir "$W/acgen" -q \
  || fail "output-alias Pyrope did not regenerate Verilog"
$LHD lec --impl verilog:"$W/output_alias_all.v" --impl-top output_alias \
  --ref pyrope:"$W/av"/ --ref-top output_alias.output_alias \
  --set formal.solver=cvc5 --workdir "$W/artlec" -q --result-json "$W/artlec.json" \
  || fail "generated always_comb temporary order was not recovered: $(cat "$W/artlec.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/artlec.json" || fail "output-alias round-trip lec not pass: $(cat "$W/artlec.json")"
echo "PASS: generated child-output alias survives a false cross-instance SCC and Verilog re-read"

# ── (5) module variable first assigned in a compile-time-dead branch ─────────
# A module-scope variable exists outside every procedural arm.  Declaring it
# lazily at its first textual assignment put the declaration inside `if (0)`;
# uPass then removed the dead arm and left the ELSE value's later consumer as an
# unresolved reference (Minion generated Verilog: mux_49628).
cat >"$W/const_cond_tmp.sv" <<'EOF'
module const_cond_tmp(
  input logic clk,
  input logic rst_ni,
  input logic [7:0] d,
  output logic [7:0] q
);
  logic tmp;
  logic en;
  always_comb begin
    if (1'b0) tmp = 1'b1;
    else      tmp = 1'b0;
    if (!rst_ni) en = 1'b1;
    else         en = tmp;
  end
  always_ff @(posedge clk) if (en) q <= d;
endmodule
EOF
$LHD compile "$W/const_cond_tmp.sv" --top const_cond_tmp --recipe O0 \
  --emit-dir pyrope:"$W/cpv" --workdir "$W/cpw" >"$W/cp.log" 2>&1 \
  || { tail -20 "$W/cp.log"; fail "constant-conditional temporary did not emit Pyrope"; }
$LHD compile "$W/cpv"/*.prp --top const_cond_tmp.const_cond_tmp --recipe O0 \
  --emit-dir lg:"$W/cplg" --workdir "$W/cpr" >"$W/cpr.log" 2>&1 \
  || { tail -20 "$W/cpr.log"; fail "constant-conditional temporary became unresolved on Pyrope re-read"; }
if grep -q 'unresolved ref' "$W/cp.log" "$W/cpr.log"; then
  fail "constant-conditional temporary lost its module-scope driver: $(grep 'unresolved ref' "$W/cp.log" "$W/cpr.log")"
fi
$LHD lec --impl lg:"$W/cplg" --impl-top const_cond_tmp.const_cond_tmp \
  --ref verilog:"$W/const_cond_tmp.sv" --ref-top const_cond_tmp \
  --set formal.solver=cvc5 --workdir "$W/cplec" -q --result-json "$W/cplec.json" \
  || fail "constant-conditional temporary changed behavior: $(cat "$W/cplec.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/cplec.json" || fail "constant-conditional temporary LEC not pass: $(cat "$W/cplec.json")"
echo "PASS: module variable first assigned in if (0) stays declared, recompiles, and LECs"

echo "PASS: all slang wire-classification regressions"
