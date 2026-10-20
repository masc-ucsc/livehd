#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A proven (collapsed) child owes its parent a `bbin:` obligation per input
# port -- except a port the child's body never lets reach an output. A
# synthesized parent may tie such a port to 0 while the source still computes
# it (lhdtrack lec_netlist br_fifo_shared_pstatic_flops: a disabled bypass's
# `bypass_ready`); the child ignores it on both sides, so no output can show the
# difference. The collapsed proof must settle without a flat retry. A child
# that READS the port must still refute, and a TRUSTED child keeps every input
# obligation (the boxed model is the contract: REFUTED-cex).
set -uo pipefail
LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then LHD=./lhd/lhd; fi
W="${TEST_TMPDIR:-$(mktemp -d)}/box_dead_input"
rm -rf "$W"
mkdir -p "$W"
fail=0

# Child with an unread input `u` (and a dead pipeline stage on it), reached
# through an outputless helper instance -- the bedrock `br_misc_unused` shape.
cat >"$W/ref.v" <<'V'
module sink_only(input in); endmodule
module child(input clk, input rst, input [3:0] a, input u, output reg [3:0] y);
  reg u_q;
  sink_only k(.in(u_q));
  always @(posedge clk) begin
    u_q <= u;
    if (rst) y <= 4'd0; else y <= a + 4'd1;
  end
endmodule
module top(input clk, input rst, input [3:0] a, input b, output [3:0] y);
  child c(.clk(clk), .rst(rst), .a(a), .u(a[0] ^ b), .y(y));
endmodule
V
sed 's/\.u(a\[0\] ^ b)/.u(1'"'"'b0)/' "$W/ref.v" >"$W/impl.v"
# The same pair where the child reads `u`: the tie-off is a real difference.
sed 's/y <= a + 4'"'"'d1;/y <= a + {3'"'"'d0, u};/' "$W/ref.v" >"$W/ref_live.v"
sed 's/y <= a + 4'"'"'d1;/y <= a + {3'"'"'d0, u};/' "$W/impl.v" >"$W/impl_live.v"
grep -q "1'b0" "$W/impl.v" && grep -q '{3' "$W/ref_live.v" || { echo "FAIL: fixture rewrite"; exit 1; }

run() {  # tag ref impl [extra...]
  local tag=$1 ref=$2 impl=$3
  shift 3
  "$LHD" lec --set compile.specialize_modules=false --set formal.effort=proof --set formal.timeout=20 --set formal.lec.semdiff=none \
    --set formal.simfail_run=false --ref "$W/$ref" --impl "$W/$impl" --top top \
    --workdir "$W/$tag" --result-json "$W/$tag.json" "$@" >"$W/$tag.log" 2>&1
  RC=$?
  VERDICT=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("lec",{}).get("verdict",""))' "$W/$tag.json" 2>/dev/null)
}

# 1) unread port: PROVEN under collapse, with no flat retry/confirmation.
run dead ref.v impl.v
if [ "$RC" -ne 0 ] || [ "$VERDICT" != proven-full ]; then
  echo "FAIL: unread box input rc=$RC verdict=$VERDICT (want proven-full)"; tail -5 "$W/dead.log"; fail=1
elif ! grep -q "^lec\[hier\]: 'top' PROVEN (1 child collapse)" "$W/dead.log"; then
  echo "FAIL: top was not proven with the child collapsed"; grep "^lec" "$W/dead.log"; fail=1
elif grep -q "under collapse" "$W/dead.log"; then
  echo "FAIL: the unread box input still needed a flat retry"; grep "under collapse" "$W/dead.log"; fail=1
else
  echo "ok: an unread box input owes no obligation (collapsed PROVEN)"
fi

# 2) the child reads the port: still REFUTED-cex.
run live ref_live.v impl_live.v
if [ "$RC" -ne 10 ] || [ "$VERDICT" != refuted-cex ]; then
  echo "FAIL: read box input rc=$RC verdict=$VERDICT (want refuted-cex, 10)"; tail -5 "$W/live.log"; fail=1
else
  echo "ok: a box input the child reads keeps its obligation (REFUTED-cex)"
fi

# 3) a TRUSTED child keeps every input obligation, read or not.
run trusted ref.v impl.v --trust child
if [ "$RC" -ne 10 ] || [ "$VERDICT" != refuted-cex ]; then
  echo "FAIL: trusted box rc=$RC verdict=$VERDICT (want refuted-cex, 10)"; tail -5 "$W/trusted.log"; fail=1
elif ! grep -q "trusted-box input" "$W/trusted.log"; then
  echo "FAIL: trusted-box input refute not disclosed"; grep "^lec" "$W/trusted.log"; fail=1
else
  echo "ok: a trusted box keeps its input obligations (REFUTED-cex at bbin)"
fi

# 4) a child with NO input (a tie-off constant generator) is never boxed: the
#    impl folded one instance's constant into the parent (its instance is gone)
#    while another instance survives, so a box would be one-sided.
cat >"$W/tie_ref.v" <<'V'
module tie(output [3:0] out); assign out = 4'd0; endmodule
module top(input clk, input [3:0] a, output [3:0] y, output [3:0] z);
  wire [3:0] t, t2;
  reg  [3:0] r;
  tie k(.out(t));
  tie k2(.out(t2));
  always @(posedge clk) r <= a;
  assign y = r | t;
  assign z = r ^ t2;
endmodule
V
cat >"$W/tie_impl.v" <<'V'
module tie(output [3:0] out); assign out = 4'd0; endmodule
module top(input clk, input [3:0] a, output [3:0] y, output [3:0] z);
  wire [3:0] t2;
  reg  [3:0] r;
  tie k2(.out(t2));
  always @(posedge clk) r <= a;
  assign y = r;
  assign z = r ^ t2;
endmodule
V
run tie tie_ref.v tie_impl.v
if [ "$RC" -ne 0 ] || [ "$VERDICT" != proven-full ]; then
  echo "FAIL: folded tie-off rc=$RC verdict=$VERDICT (want proven-full)"; tail -5 "$W/tie.log"; fail=1
elif grep -q "under collapse" "$W/tie.log"; then
  echo "FAIL: a no-input child was boxed (needed a flat retry)"; grep "under collapse" "$W/tie.log"; fail=1
else
  echo "ok: a no-input child is descended, not boxed"
fi

# 5) a proven child's CONSTANT output is that constant on both sides, not a
#    free box symbol: the impl folded `k` (= 0) out of the parent, the ref still
#    ORs it in. With k = 1 the same fold is a real difference and refutes.
for kv in 0 1; do
  cat >"$W/k${kv}_ref.v" <<V
module child(input clk, input [3:0] a, output reg [3:0] q, output [3:0] k);
  always @(posedge clk) q <= a;
  assign k = 4'd${kv};
endmodule
module top(input clk, input [3:0] a, output [3:0] y);
  wire [3:0] q, k;
  child c(.clk(clk), .a(a), .q(q), .k(k));
  assign y = q | k;
endmodule
V
  sed 's/assign y = q | k;/assign y = q;/' "$W/k${kv}_ref.v" >"$W/k${kv}_impl.v"
done
run k0 k0_ref.v k0_impl.v
if [ "$RC" -ne 0 ] || [ "$VERDICT" != proven-full ]; then
  echo "FAIL: folded constant box output rc=$RC verdict=$VERDICT (want proven-full)"; tail -5 "$W/k0.log"; fail=1
elif grep -q "under collapse" "$W/k0.log"; then
  echo "FAIL: a constant box output stayed a free symbol (needed a flat retry)"; grep "under collapse" "$W/k0.log"; fail=1
else
  echo "ok: a proven child's constant output is a constant, not a free symbol"
fi
run k1 k1_ref.v k1_impl.v
if [ "$RC" -ne 10 ] || [ "$VERDICT" != refuted-cex ]; then
  echo "FAIL: dropping a nonzero constant rc=$RC verdict=$VERDICT (want refuted-cex, 10)"; tail -5 "$W/k1.log"; fail=1
else
  echo "ok: dropping a nonzero constant box output still refutes"
fi

if [ "$fail" -eq 0 ]; then echo "PASS: lec_box_dead_input_test"; else echo "FAIL: lec_box_dead_input_test"; fi
exit "$fail"
