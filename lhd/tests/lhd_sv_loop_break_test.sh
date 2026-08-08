#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `break` inside an unrolled for-loop: the priority-select idiom every CPU uses
#
#   for (i = 0; i < N; i++) begin
#     sel = i; gnt = 1;
#     if (req[i]) break;      // first set bit wins
#   end
#
# Contract under test:
#   * the lowering is EQUIVALENT to the hand-written if/else-if priority chain
#     -- proven by LEC over the whole input space, not by eyeballing the output.
#     A dropped guard (later iterations overwriting the winner) or an off-by-one
#     break point refutes there;
#   * a loop WITHOUT a break is unchanged -- the flag machinery only appears when
#     the body actually contains one, so existing designs keep their emission;
#   * a `break` in a NESTED loop binds to the inner loop, not the outer;
#   * `continue` is still refused -- it skips the rest of ONE iteration, which
#     needs a per-iteration flag; lowering it as a break would silently drop
#     every later iteration, so it must fail loudly instead.
#
# This was the single feature blocking CVA6's cache_subsystem/tag_cmp.sv, whose
# port arbiter is exactly the loop above.

set -u

LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_sv_break_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# ---- break lowers to a priority select -------------------------------------
cat >"$W/prio.sv" <<'EOF'
module prio(input [3:0] req, output reg [1:0] sel, output reg gnt);
  integer i;
  always @* begin
    sel = 2'd0; gnt = 1'b0;
    for (i = 0; i < 4; i = i + 1) begin
      sel = i[1:0];
      gnt = 1'b1;
      if (req[i]) break;
    end
  end
endmodule
EOF

# the SAME function written without a break: the equivalence reference
cat >"$W/prio_ref.sv" <<'EOF'
module prio_ref(input [3:0] req, output reg [1:0] sel, output reg gnt);
  always @* begin
    gnt = 1'b1;
    if      (req[0]) sel = 2'd0;
    else if (req[1]) sel = 2'd1;
    else if (req[2]) sel = 2'd2;
    else             sel = 2'd3;
  end
endmodule
EOF

OUT="$W/prio.out"
"$LHD" compile verilog --top prio --emit-dir "lg:$W/op" --workdir "$W/wp" --diag-fmt pretty -- "$W/prio.sv" >"$OUT" 2>&1 \
  || fail "a loop with break must compile: $(cat "$OUT")"
"$LHD" compile verilog --top prio_ref --emit-dir "lg:$W/oref" --workdir "$W/wr" -- "$W/prio_ref.sv" >"$W/ref.out" 2>&1 \
  || fail "the reference must compile: $(cat "$W/ref.out")"

# The real check: same function, proven over every input.
OUT="$W/lec.out"
"$LHD" lec --impl "lg:$W/op" --ref "lg:$W/oref" --top prio --ref-top prio_ref --workdir "$W/wl" --diag-fmt pretty >"$OUT" 2>&1 \
  || fail "break lowering must be equivalent to the explicit priority chain: $(cat "$OUT")"
grep -q 'PROVEN' "$OUT" || fail "expected PROVEN equivalence: $(cat "$OUT")"
grep -qi 'refut' "$OUT" && fail "break lowering is NOT the priority select: $(cat "$OUT")"

# ---- a loop WITHOUT a break is untouched ------------------------------------
# The flag/guard machinery must be introduced only when a break is present:
# every other loop in every design keeps the emission it had.
cat >"$W/plain.sv" <<'EOF'
module plain(input [3:0] d, output reg [3:0] acc);
  integer i;
  always @* begin
    acc = 4'd0;
    for (i = 0; i < 4; i = i + 1) acc[i] = d[i];
  end
endmodule
EOF
OUT="$W/plain.out"
"$LHD" compile verilog --top plain --emit-dir "lg:$W/opl" --workdir "$W/wpl" --diag-fmt pretty -- "$W/plain.sv" >"$OUT" 2>&1 \
  || fail "a plain loop must still compile: $(cat "$OUT")"
grep -qi 'brk' "$OUT" && fail "a loop with no break must not gain break machinery: $(cat "$OUT")"

# ---- a break binds to the INNER loop ---------------------------------------
# The outer loop must keep running after the inner one breaks. Written so the
# result differs if the break escapes: every outer iteration contributes.
cat >"$W/nest.sv" <<'EOF'
module nest(input [1:0] stop, output reg [3:0] hits);
  integer o, n;
  always @* begin
    hits = 4'd0;
    for (o = 0; o < 2; o = o + 1) begin
      for (n = 0; n < 2; n = n + 1) begin
        hits[o*2 + n] = 1'b1;
        if (stop[o]) break;
      end
    end
  end
endmodule
EOF
OUT="$W/nest.out"
"$LHD" compile verilog --top nest --emit-dir "lg:$W/on" --workdir "$W/wn" --diag-fmt pretty -- "$W/nest.sv" >"$OUT" 2>&1 \
  || fail "nested loops with an inner break must compile: $(cat "$OUT")"

# stop=2'b01 breaks the inner loop of outer iteration 0 only, so hits[0] and
# BOTH bits of outer iteration 1 are set -> 4'b1101. If the break escaped to the
# outer loop, hits would be 4'b0001.
cat >"$W/nest_ref.sv" <<'EOF'
module nest_ref(input [1:0] stop, output reg [3:0] hits);
  always @* begin
    hits    = 4'd0;
    hits[0] = 1'b1;
    hits[1] = ~stop[0];
    hits[2] = 1'b1;
    hits[3] = ~stop[1];
  end
endmodule
EOF
"$LHD" compile verilog --top nest_ref --emit-dir "lg:$W/onr" --workdir "$W/wnr" -- "$W/nest_ref.sv" >"$W/nr.out" 2>&1 \
  || fail "the nested reference must compile: $(cat "$W/nr.out")"
OUT="$W/nestlec.out"
"$LHD" lec --impl "lg:$W/on" --ref "lg:$W/onr" --top nest --ref-top nest_ref --workdir "$W/wnl" --diag-fmt pretty >"$OUT" 2>&1 \
  || fail "an inner break must not escape to the outer loop: $(cat "$OUT")"
grep -q 'PROVEN' "$OUT" || fail "expected PROVEN for the nested case: $(cat "$OUT")"

# ---- continue is still refused, loudly -------------------------------------
cat >"$W/cont.sv" <<'EOF'
module cont(input [3:0] req, output reg [3:0] o);
  integer i;
  always @* begin
    o = 4'd0;
    for (i = 0; i < 4; i = i + 1) begin
      if (!req[i]) continue;
      o[i] = 1'b1;
    end
  end
endmodule
EOF
OUT="$W/cont.out"
"$LHD" compile verilog --top cont --emit-dir "lg:$W/oc" --workdir "$W/wc" --diag-fmt pretty -- "$W/cont.sv" >"$OUT" 2>&1
[ $? -ne 0 ] || fail "continue must still be refused, not silently lowered: $(cat "$OUT")"
grep -qi 'continue' "$OUT" || fail "the refusal must name continue: $(cat "$OUT")"

echo "PASS: break in an unrolled loop lowers to a priority select"
