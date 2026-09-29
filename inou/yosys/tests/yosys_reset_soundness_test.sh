#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# lgcheck must never PROVE a non-equivalent pair because of how it treats a
# conventionally named reset input.
#
# The temporal-induction stage (sat -tempinduct) held the detected reset with
# per-step `-set-at` constraints INSIDE its checked window. yosys adds a
# simple-path constraint (all states distinct) to every base case; the zero
# power-on state and the post-reset state are the same state, so every base
# case of length >= 2 was vacuously UNSAT and the induction step then closed:
# `f ^ d` PROVED equal to `f + d` whenever the port was named `rst` (renaming
# it made lgcheck refute). Verdict discipline: rc 0 = proven, 1 = refuted,
# 2 = inconclusive. Each non-equivalent pair must REFUTE (with and without the
# reset name); an equivalent reset-bearing pair must never refute.
set -u
W="${TEST_TMPDIR:-/tmp/lgcheck_reset_soundness_$$}"
mkdir -p "$W"
LGCHECK="${LGCHECK:-$PWD/inou/yosys/lgcheck}"
YOSYS_ABS="${YOSYS_ABS:-$PWD/inou/yosys/yosys2}"
fail() { echo "FAIL: $*" >&2; exit 1; }

# Combinational, reset gates the output.
cat >"$W/comb_xor.v" <<'V'
module dut(input rst, input [1:0] f, input [1:0] d, output [1:0] o);
  assign o = rst ? 2'b00 : (f ^ d);
endmodule
V
sed 's/(f ^ d)/(f + d)/' "$W/comb_xor.v" >"$W/comb_add.v"
# Combinational, reset present but unused.
cat >"$W/combu_xor.v" <<'V'
module dut(input clk, input rst, input [1:0] f, input [1:0] d, output [1:0] o);
  assign o = f ^ d;
endmodule
V
sed 's/f ^ d/f + d/' "$W/combu_xor.v" >"$W/combu_add.v"
# Sequential, synchronous reset.
cat >"$W/seq_xor.v" <<'V'
module dut(input clk, input rst, input [1:0] d, output [1:0] q);
  reg [1:0] f;
  always @(posedge clk) f <= rst ? 2'b00 : (f ^ d);
  assign q = f;
endmodule
V
sed 's/(f ^ d)/(f + d)/' "$W/seq_xor.v" >"$W/seq_add.v"
# The reported shape: a negedge register against a netlist of DFF instances.
cat >"$W/cells_xor.v" <<'V'
module dffp(input CLK, input D, output reg Q);
  always @(posedge CLK) Q <= D;
endmodule
module dut(input clk, input rst, input [1:0] d, output [1:0] q);
  wire nclk = ~clk;
  wire [1:0] f;
  dffp f0(.CLK(nclk), .D(~rst & (f[0] ^ d[0])), .Q(f[0]));
  dffp f1(.CLK(nclk), .D(~rst & (f[1] ^ d[1])), .Q(f[1]));
  assign q = f;
endmodule
V
cat >"$W/negr_add.v" <<'V'
module dut(input clk, input rst, input [1:0] d, output [1:0] q);
  reg [1:0] f;
  always @(negedge clk) f <= rst ? 2'b00 : (f + d);
  assign q = f;
endmodule
V
sed 's/(f + d)/(f ^ d)/' "$W/negr_add.v" >"$W/negr_xor.v"
# Active-low spelling.
sed -e 's/input rst/input rst_n/' -e 's/rst ?/!rst_n ?/' "$W/seq_xor.v" >"$W/seqn_xor.v"
sed 's/(f ^ d)/(f + d)/' "$W/seqn_xor.v" >"$W/seqn_add.v"
# An equivalent reset-bearing pair written differently.
cat >"$W/seq_eq.v" <<'V'
module dut(input clk, input rst, input [1:0] d, output [1:0] q);
  reg [1:0] f;
  always @(posedge clk) f <= {2{~rst}} & {f[1] ^ d[1], f[0] ^ d[0]};
  assign q = f;
endmodule
V
# Equivalent only AFTER reset: the flag z powers up 0 (q and the next-state
# function differ) and the reset sets it for good. Its zero power-on state
# differs, so no engine may refute it: the post-reset comparison is the
# contract.
cat >"$W/seq_z.v" <<'V'
module dut(input clk, input rst, input [1:0] d, output [1:0] q);
  reg [1:0] g;
  reg z;
  always @(posedge clk) begin
    z <= rst ? 1'b1 : z;
    g <= rst ? 2'b00 : (z ? (g ^ d) : (g + d));
  end
  assign q = z ? g : (g ^ 2'b01);
endmodule
V

# Renamed twins: the same pairs without a conventional reset name.
for base in comb_xor comb_add combu_xor combu_add seq_xor seq_add cells_xor negr_add; do
  sed 's/\brst\b/foo/g' "$W/$base.v" >"$W/${base}_ren.v"
done

LGCHECK_BUDGET="${LGCHECK_BUDGET:-120}"
# name:impl:ref:expect (refuted | not_refuted)
cases=(
  comb:comb_add:comb_xor:refuted
  combu:combu_add:combu_xor:refuted
  seq:seq_add:seq_xor:refuted
  cells:cells_xor:negr_add:refuted
  seqn:seqn_add:seqn_xor:refuted
  comb_ren:comb_add_ren:comb_xor_ren:refuted
  combu_ren:combu_add_ren:combu_xor_ren:refuted
  seq_ren:seq_add_ren:seq_xor_ren:refuted
  cells_ren:cells_xor_ren:negr_add_ren:refuted
  seq_eq:seq_eq:seq_xor:not_refuted
  cells_eq:cells_xor:negr_xor:not_refuted
  seq_z:seq_z:seq_xor:not_refuted
)
pids=()
for c in "${cases[@]}"; do
  IFS=: read -r name impl ref _ <<<"$c"
  mkdir -p "$W/$name"
  (cd "$W/$name" && LGCHECK_EQUIV_TIMEOUT="$LGCHECK_BUDGET" "$LGCHECK" \
    --yosys "$YOSYS_ABS" --top dut \
    --reference "$W/$ref.v" --implementation "$W/$impl.v") >"$W/$name.log" 2>&1 &
  pids+=($!)
done

i=0
bad=0
for c in "${cases[@]}"; do
  IFS=: read -r name _ _ want <<<"$c"
  wait "${pids[$i]}"
  rc=$?
  i=$((i + 1))
  case "$want:$rc" in
  refuted:1 | not_refuted:0 | not_refuted:2)
    echo "ok: $name rc=$rc ($want)"
    ;;
  *)
    echo "---- $name.log"
    tail -20 "$W/$name.log"
    echo "BAD: $name rc=$rc (expected $want)"
    bad=1
    ;;
  esac
done
[ "$bad" -eq 0 ] || fail "lgcheck reset soundness"
echo "PASS: lgcheck reset soundness"
