#!/usr/bin/env bash
# 2f-defer — the cross-language `.[defer]` feedback: a Pyrope module wires one
# of an imported Verilog module's outputs back into one of its inputs in the
# SAME cycle (`c = tmp.add.[defer]`). Legal because the netlist edge is added
# after the pass (the deferred edge); a defer cycle is not illegal (Verilog
# permits comb loops; a later lgraph pass handles real ones). Self-contained.
set -u

if   [ -x ./bazel-bin/lhd/lhd ]; then LHD=./bazel-bin/lhd/lhd
elif [ -x ./lhd/lhd ];           then LHD=./lhd/lhd
else echo "FAIL: lhd binary not found"; exit 3; fi

W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
fail() { echo "FAIL: $*"; exit 1; }

# add_sub: two independent functions — add depends on (a,b), sub on (c,d).
cat > "$W/add_sub.v" <<'EOF'
module add_sub(input  [7:0] a, input [7:0] b,
               input  [8:0] c, input [7:0] d,
               output [8:0] add, output signed [9:0] sub);
  assign add = a + b;   // depends on (a,b) only
  assign sub = c - d;   // depends on (c,d) only
endmodule
EOF
# test_defer: feed tmp.add back into c via `.[defer]` (acyclic at the netlist
# level: add does not depend on c), so out = tmp.sub = (a+b) - 3.
cat > "$W/test_defer.prp" <<'EOF'
const add_sub = import("lg:add_sub")
mod test_defer(a:u8, b:u8) -> (out:i10@[0]) {
  mut tmp = add_sub(a=a, b=b, c=tmp.add.[defer], d=3)
  out = tmp.sub
}
EOF
cat > "$W/gold.v" <<'EOF'
module \test_defer.test_defer (input [7:0] a, input [7:0] b, output signed [9:0] out);
  assign out = {1'b0, a} + {1'b0, b} - 10'sd3;   // (a + b) - 3
endmodule
EOF

$LHD compile "$W/add_sub.v" --reader slang --top add_sub --emit-dir "lg:$W/lgdb/" \
  --workdir "$W/w1" -q || fail "step 1 (slang add_sub -> lg) did not compile"

$LHD compile "$W/test_defer.prp" "lg:$W/lgdb/" --emit-dir "verilog:$W/out/" \
  --workdir "$W/w2" -q || fail "step 2 (defer feedback) did not compile"
grep -q "add_sub u_" "$W/out/test_defer.test_defer.v" || fail "top does not instantiate add_sub"

# LEC: top (with the same-cycle feedback) + the add_sub body == flat (a+b)-3.
cat "$W/out/test_defer.test_defer.v" "$W/add_sub.v" > "$W/impl.v"
$LHD check --impl "verilog:$W/impl.v" --ref "verilog:$W/gold.v" \
  --impl-top test_defer.test_defer --ref-top test_defer.test_defer --workdir "$W/wlec" -q \
  || fail "LEC: defer feedback netlist is not equivalent to (a+b)-3"

echo "defer_mix_test - PASS (cross-language .[defer] feedback LECs as (a+b)-3)"
exit 0
