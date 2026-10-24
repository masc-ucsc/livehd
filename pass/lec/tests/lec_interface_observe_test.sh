#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# REFUTED-interface and one-sided INPUTS (owner ruling 2026-10-14).
#
# A top port NAME on one side only is REFUTED-interface (exit 7) -- except an
# INPUT that is not observable there: one outside the cone of influence of
# everything the miter compares (the top outputs, and the state that reaches
# them). Such an input is ignored and noted in the detail ("input 'clk' only in
# ref, not observable: ignored"). The check used to accept only an input with
# NO fanout at all, so a bedrock-style `clk` "used only for assertions" -- it
# feeds a checker instance with no outputs, nested inside an instance whose
# outputs ARE used -- stopped nine lhdtrack designs (br_flow_join & co.) as
# REFUTED-interface, although each PROVES once the input is ignored.
#
# Dead (PROVEN-full, exit 0, with the note):
#   1. the bedrock shape: clk -> used instance -> outputless checker instance
#      holding an assertion; also with an EMPTY checker body
#   2. an immediate assertion in the top (an fproperty marker, no hardware)
#   3. per-output precision: clk reaches only the UNREAD output of an instance
#      whose other output drives the top output
#   4. a one-sided input reaching only a flop that reaches no output
#   5. the same input one-sided on the IMPL side
# Observable (REFUTED-interface, exit 7):
#   6. clk reaches the top output through an instance output that is read
#   7. `en` reaches the top output through an instance AND a flop
#   8. a one-sided OUTPUT (always a mismatch)
# The check is linear: a forward walk per input under a SHARED step budget,
# then one backward pass from every compared point for the rest. Cases 1-7
# are rerun with the forward budget at 0 (LHD_LEC_IFACE_FORWARD_BUDGET), so
# the backward pass must give the same answers. And it runs under the run's
# wall deadline: one reached there is TIMEOUT-time, exit 0 (the cva6 netlist
# spent >80 min in this pre-check before any deadline was armed).

set -u
LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi
W="${TEST_TMPDIR:-/tmp/lec_iface_observe_$$}"; mkdir -p "$W"; fail=0

# --- fixtures ---------------------------------------------------------------
cat > "$W/plain.v" <<'EOF'
module m(input [3:0] a, input [3:0] b, output [3:0] y);
  assign y = b + a;
endmodule
EOF
cat > "$W/plain_seq.v" <<'EOF'
module m(input clk, input [3:0] a, input [3:0] b, output reg [3:0] y);
  always @(posedge clk) y <= a + b;
endmodule
EOF

# 1. bedrock: clk is "used only for assertions"
cat > "$W/bedrock.v" <<'EOF'
module chk(input clk, input [3:0] v);
  always_comb assert ({clk, v} != 5'h1F || v == 4'hF);
endmodule
module inner(input clk, input [3:0] a, input [3:0] b, output [3:0] y);
  chk c(.clk(clk), .v(a));
  assign y = a + b;
endmodule
module m(input clk, input [3:0] a, input [3:0] b, output [3:0] y);
  inner u(.clk(clk), .a(a), .b(b), .y(y));
endmodule
EOF
cat > "$W/bedrock_empty.v" <<'EOF'
module chk(input clk, input [3:0] v);
endmodule
module inner(input clk, input [3:0] a, input [3:0] b, output [3:0] y);
  chk c(.clk(clk), .v(a));
  assign y = a + b;
endmodule
module m(input clk, input [3:0] a, input [3:0] b, output [3:0] y);
  inner u(.clk(clk), .a(a), .b(b), .y(y));
endmodule
EOF

# 2. an immediate assertion in the top
cat > "$W/assert_top.v" <<'EOF'
module m(input clk, input [3:0] a, input [3:0] b, output [3:0] y);
  always_comb assert ({clk, a} != {1'b0, b} || a == b);
  assign y = a + b;
endmodule
EOF

# 3. clk reaches only the instance output nobody reads
cat > "$W/unread_out.v" <<'EOF'
module two(input clk, input [3:0] a, input [3:0] b, output [3:0] s, output [3:0] t);
  assign s = a + b;
  assign t = a ^ {4{clk}};
endmodule
module m(input clk, input [3:0] a, input [3:0] b, output [3:0] y);
  wire [3:0] t;
  two u(.clk(clk), .a(a), .b(b), .s(y), .t(t));
endmodule
EOF

# 4. `en` reaches only state that no output reads
cat > "$W/dead_state.v" <<'EOF'
module m(input clk, input en, input [3:0] a, input [3:0] b, output reg [3:0] y);
  reg [3:0] dead;
  always @(posedge clk) begin
    dead <= en ? a : dead;
    y <= a + b;
  end
endmodule
EOF

# 6. the clk-dependent instance output IS read
cat > "$W/read_out.v" <<'EOF'
module two(input clk, input [3:0] a, input [3:0] b, output [3:0] s, output [3:0] t);
  assign s = a + b;
  assign t = {3'b0, clk};
endmodule
module m(input clk, input [3:0] a, input [3:0] b, output [3:0] y);
  wire [3:0] s, t;
  two u(.clk(clk), .a(a), .b(b), .s(s), .t(t));
  assign y = s ^ t;
endmodule
EOF

# 7. `en` -> instance -> flop -> output
cat > "$W/en_flop.v" <<'EOF'
module stage(input en, input [3:0] a, output [3:0] q);
  assign q = en ? a : 4'h0;
endmodule
module m(input clk, input en, input [3:0] a, input [3:0] b, output reg [3:0] y);
  wire [3:0] t;
  stage s(.en(en), .a(a), .q(t));
  always @(posedge clk) y <= t + b;
endmodule
EOF

# 8. a one-sided output
cat > "$W/extra_out.v" <<'EOF'
module m(input [3:0] a, input [3:0] b, output [3:0] y, output z);
  assign y = a + b;
  assign z = a[0];
endmodule
EOF

# --- helpers ----------------------------------------------------------------
run() {  # run <ref> <impl>; sets OUT and RC
  OUT=$("$LHD" lec --set formal.effort=proof --ref "verilog:$W/$1" --impl "verilog:$W/$2" --top m 2>&1)
  RC=$?
}
ck() { if eval "$2"; then echo "ok: $1"; else echo "FAIL: $1 (rc=$RC)"; echo "$OUT" | grep "^lec: " | tail -3; fail=1; fi; }
# ck evals its check in the caller's dynamic scope, so the checks below read
# the caller's locals (never ck's own positional parameters).
dead() {  # dead <label> <ref> <impl> <input> <side>
  local in="$4" side="$5"
  run "$2" "$3"
  ck "$1: PROVEN-full, exit 0"   '[ "$RC" -eq 0 ] && echo "$OUT" | grep -q "^lec: .m. PROVEN-full"'
  ck "$1: noted, not refused"    'echo "$OUT" | grep -q "^lec: .m. interface note: input .$in. only in $side, not observable: ignored"'
  ck "$1: never REFUTED"         '! echo "$OUT" | grep -q "^lec: .* REFUTED-"'
}
observable() {  # observable <label> <ref> <impl> <expected detail regex>
  local want="$4"
  run "$2" "$3"
  ck "$1: REFUTED-interface, exit 7" '[ "$RC" -eq 7 ] && echo "$OUT" | grep -q "^lec: .m. REFUTED-interface ($want"'
  ck "$1: never PROVEN"              '! echo "$OUT" | grep -qE "^lec: .* (PROVEN|TESTED)-"'
}
cases() {
  # --- dead one-sided inputs --------------------------------------------------
  dead "bedrock assertion-only clk$1"     bedrock.v       plain.v     clk ref
  dead "bedrock empty checker$1"          bedrock_empty.v plain.v     clk ref
  dead "top immediate assertion$1"        assert_top.v    plain.v     clk ref
  dead "unread instance output$1"         unread_out.v    plain.v     clk ref
  dead "input feeding dead state$1"       dead_state.v    plain_seq.v en  ref
  dead "impl-side assertion-only clk$1"   plain.v         bedrock.v   clk impl
  # --- observable one-sided inputs --------------------------------------------
  observable "read instance output$1" read_out.v  plain.v     "input .clk. only in ref (observable: $2"
  observable "input through a flop$1" en_flop.v   plain_seq.v "input .en. only in ref (observable: $2"
}

cases "" "it reaches an output there)"
observable "one-sided output"     extra_out.v plain.v     "output .z. only in ref"

# --- the backward pass alone (forward budget 0) must agree ------------------------
export LHD_LEC_IFACE_FORWARD_BUDGET=0
cases " [backward]" "it reaches a compared point there)"
unset LHD_LEC_IFACE_FORWARD_BUDGET

# --- the wall deadline bounds the pre-check: TIMEOUT-time, exit 0 ---------------
export LHD_LEC_IFACE_DEADLINE_MS=0
run read_out.v plain.v
ck "deadline in the interface check: TIMEOUT-time, exit 0" \
  '[ "$RC" -eq 0 ] && echo "$OUT" | grep -q "^lec: .m. TIMEOUT-time (.*hit formal.timeout=.* in the top-interface check"'
run plain.v plain.v
ck "matching interfaces never walk (no deadline to hit): PROVEN-full" \
  '[ "$RC" -eq 0 ] && echo "$OUT" | grep -q "^lec: .m. PROVEN-full"'
unset LHD_LEC_IFACE_DEADLINE_MS

if [ $fail -ne 0 ]; then echo "lec_interface_observe_test: FAILED"; exit 1; fi
echo "lec_interface_observe_test: PASSED"
exit 0
