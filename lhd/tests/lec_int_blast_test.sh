#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Gate for `formal.lec.int_blast` (cvc5 solve-bv-as-int): the BV encoding is
# translated to unbounded-integer arithmetic INSIDE cvc5, so the knob must not
# change any verdict — PROVEN stays PROVEN, and a REFUTED must still come back
# with a CONCRETE counterexample (the translation maps the model back to the
# original BV terms; a witness that degrades to UNKNOWN means the model
# round-trip broke). cones=false everywhere: the abc pre-pass is bit-level and
# would otherwise discharge these tiny miters before cvc5 ever sees them, and
# this test exists to exercise the int-blasted cvc5 path.

set -u

LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi

WORK="${TEST_TMPDIR:-/tmp/lec_int_blast}"
mkdir -p "$WORK/ref" "$WORK/impl"
fail=0

# ── 1. A bad mode is a usage error that names the accepted ones ──────────────
# (a real design file, so option validation — not file loading — is what fires;
# NB `dist` itself is an SV keyword, hence `distrib`)
cat > "$WORK/ref/distrib.v" <<'EOF'
module distrib (input wire [7:0] a, input wire [7:0] b, input wire [7:0] c,
                output wire [7:0] y);
  assign y = a * (b + c);
endmodule
EOF
out=$("$LHD" lec --ref "verilog:$WORK/ref/distrib.v" --impl "verilog:$WORK/ref/distrib.v" \
      --workdir "$WORK/w_bad" --set formal.lec.int_blast=potato 2>&1); rc=$?
if [ "$rc" -eq 0 ] || ! echo "$out" | grep -q "int_blast unknown 'potato'"; then
  echo "FAIL: int_blast=potato accepted (rc=$rc)"; echo "$out" | tail -2; fail=1
else echo "ok: bad mode refused by name"; fi

# ── 2. PROVEN survives the translation (comb, distributed multiply) ──────────
# a*(b+c) vs a*b+a*c is the arithmetic-rewrite shape int-blasting exists for.
cat > "$WORK/impl/distrib.v" <<'EOF'
module distrib (input wire [7:0] a, input wire [7:0] b, input wire [7:0] c,
                output wire [7:0] y);
  assign y = a * b + a * c;
endmodule
EOF
out=$("$LHD" lec --ref "verilog:$WORK/ref/distrib.v" --impl "verilog:$WORK/impl/distrib.v" \
      --workdir "$WORK/w_dist" --set formal.lec.int_blast=iand --set formal.lec.cones=false 2>&1); rc=$?
if [ "$rc" -ne 0 ] || ! echo "$out" | grep -q "PROVEN equivalent"; then
  echo "FAIL: distrib expected PROVEN under int_blast=iand (rc=$rc)"; echo "$out" | grep -aE "^lec: " | tail -2; fail=1
else echo "ok: distributed multiply PROVEN under int_blast"; fi

# ── 3. REFUTED keeps its concrete witness through the model round-trip ───────
cat > "$WORK/impl/mism.v" <<'EOF'
module mism (input wire [7:0] a, input wire [7:0] b, output wire [7:0] y);
  assign y = a - b;
endmodule
EOF
cat > "$WORK/ref/mism.v" <<'EOF'
module mism (input wire [7:0] a, input wire [7:0] b, output wire [7:0] y);
  assign y = a + b;
endmodule
EOF
out=$("$LHD" lec --ref "verilog:$WORK/ref/mism.v" --impl "verilog:$WORK/impl/mism.v" \
      --workdir "$WORK/w_mism" --set formal.lec.int_blast=iand --set formal.lec.cones=false \
      --set formal.witness=true 2>&1); rc=$?
if ! echo "$out" | grep -q "REFUTED"; then
  echo "FAIL: mism expected REFUTED under int_blast=iand (rc=$rc)"; echo "$out" | grep -aE "^lec: " | tail -2; fail=1
elif ! echo "$out" | grep -q "counterexample: diff"; then
  echo "FAIL: mism REFUTED but no concrete counterexample (model round-trip broke?)"; echo "$out" | tail -3; fail=1
else echo "ok: mism REFUTED with a concrete counterexample"; fi

# ── 4. int_blast=auto (the default): a BV timeout earns the int-blast retry ──
# formal.timeout=3 starves the BV solve of the multiply miter (measured: it
# cannot finish in 60s); the retry at the min_timeout floor proves it in <1s.
# No int_blast set — this pins the DEFAULT behavior.
out=$("$LHD" lec --ref "verilog:$WORK/ref/distrib.v" --impl "verilog:$WORK/impl/distrib.v" \
      --workdir "$WORK/w_auto" --set formal.lec.cones=false \
      --set formal.timeout=3 --set formal.min_timeout=10 2>&1); rc=$?
if [ "$rc" -ne 0 ] || ! echo "$out" | grep -q "PROVEN equivalent"; then
  echo "FAIL: auto default expected retry PROVEN (rc=$rc)"; echo "$out" | grep -aE "^lec: " | tail -2; fail=1
elif ! echo "$out" | grep -q "int-blast retry"; then
  echo "FAIL: PROVEN but not via the int-blast retry (BV should not finish at 3s)"; echo "$out" | grep -aE "^lec: " | tail -2; fail=1
else echo "ok: auto default converts a BV timeout via the int-blast retry"; fi

# ── 5. int_blast=off: same starved budget, NO retry — stays inconclusive ─────
out=$("$LHD" lec --ref "verilog:$WORK/ref/distrib.v" --impl "verilog:$WORK/impl/distrib.v" \
      --workdir "$WORK/w_off" --set formal.lec.int_blast=off --set formal.lec.cones=false \
      --set formal.timeout=3 --set formal.min_timeout=1 2>&1); rc=$?
if [ "$rc" -eq 0 ] || echo "$out" | grep -q "int-blast retry"; then
  echo "FAIL: int_blast=off must not retry nor pass (rc=$rc)"; echo "$out" | grep -aE "^lec: " | tail -2; fail=1
else echo "ok: int_blast=off stays inconclusive (no retry)"; fi

# ── 6. Sequential path (flop cut + induction) survives the translation ───────
cat > "$WORK/ref/cnt.v" <<'EOF'
module cnt (input wire clk, input wire rst, output reg [7:0] q);
  always @(posedge clk) q <= rst ? 8'b0 : q + 8'd1;
endmodule
EOF
out=$("$LHD" lec --ref "verilog:$WORK/ref/cnt.v" --impl "verilog:$WORK/ref/cnt.v" \
      --workdir "$WORK/w_cnt" --set formal.lec.int_blast=iand --set formal.lec.cones=false 2>&1); rc=$?
if [ "$rc" -ne 0 ] || ! echo "$out" | grep -q "PROVEN equivalent"; then
  echo "FAIL: cnt expected PROVEN under int_blast=iand (rc=$rc)"; echo "$out" | grep -aE "^lec: " | tail -2; fail=1
else echo "ok: sequential counter PROVEN under int_blast"; fi

exit $fail
