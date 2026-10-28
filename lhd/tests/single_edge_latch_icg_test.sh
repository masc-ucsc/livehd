#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A clock gate in front of a LATCH (pass.single_edge's `held_for_window` rule).
#
# CORE-ET's tech_generic/prim_write_commit_en is the textbook glitch-free gate
# feeding a latch rather than a flop:
#
#     always_latch if (!clk)          en_1p <= en;   // enable latch: SHUT while clk is high
#     always_latch if (clk && en_1p)  q     <= d;    // data latch:   OPEN while clk && en_1p
#
# The flop rule (enable latch closed AFTER the gated commit edge) asks the wrong
# question for a latch, which is open for a whole phase and commits when it
# CLOSES: it demanded an enable latch open while clk is high -- the glitchy gate
# -- and refused this one as `clock-gate-latch-phase` (CORE-ET B08, intpipe_top,
# 692 such elements). A latch's gate must be steady for its WHOLE WINDOW: the
# enable latch shut while the data latch is open, open while it is shut.
#
# NO BYPASS on that path. The data latch closes half a period after the enable
# latch did, so the model must read the enable latch's HELD Q (captured at the
# rise), not its transparent arm (the live `en`). The testbench below moves `en`
# while the enable latch is shut, which is exactly where the two differ, and the
# bypass variant is a NEGATIVE CONTROL that must fail.
#
# THE READER IS A LOW LATCH, ON PURPOSE. A posedge FLOP reading this latch
# samples it at the edge where it OPENS. Real hardware gives that flop the held
# value (clock-to-Q), which is what commit-at-closing-edge gives it too -- but a
# zero-delay simulator resolves that instant as a race, and Verilator resolves it
# in favour of the NEW value. A source-side race is not an oracle, so the
# differential uses the race-free master/slave reader instead.
#
# The external-simulator leg runs only with LHD_EXTERNAL_SIM=1 (see AGENTS.md):
# iverilog/vvp when present, else Verilator (--binary --timing). The default run
# keeps the structural, refusal and lec legs.

set -u

LHD="${LHD:-lhd/lhd}"
if [ ! -x "$LHD" ]; then
  if [ -x ./bazel-bin/lhd/lhd ]; then
    LHD=./bazel-bin/lhd/lhd
  else
    echo "FAIL: could not find the lhd binary in $(pwd)"
    exit 1
  fi
fi

W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT

fail() {
  echo "FAIL: $*"
  exit 1
}

# ---- designs ------------------------------------------------------------------
cat > "$W/wce.sv" <<'EOF'
module wce(input clk, input en, input [7:0] d, output [7:0] q, output [7:0] r);
  reg en_1p;                                  // the enable latch: shut while clk is high
  always_latch if (!clk) en_1p = en;
  reg [7:0] qo;                               // the gated data latch: open while clk && en_1p
  always_latch if (clk && en_1p) qo = d;
  reg [7:0] lo;                               // its reader on the OPPOSITE phase (master/slave)
  always_latch if (!clk) lo = qo + 8'd1;
  assign q = qo;
  assign r = lo;
endmodule
EOF
# Equivalent rewrite (structurally different, semantically identical): the vacuity guard.
sed "s/lo = qo + 8'd1;/lo = (qo + 8'd5) - 8'd4;/" "$W/wce.sv" > "$W/wce_eq.sv"
cmp -s "$W/wce.sv" "$W/wce_eq.sv" && fail "the equivalent rewrite did not apply"
# A REAL difference behind the gate: it must not be masked.
sed "s/qo = d;/qo = d + 8'd1;/" "$W/wce.sv" > "$W/wce_bad.sv"
cmp -s "$W/wce.sv" "$W/wce_bad.sv" && fail "the mutation did not apply"
# The SAME-PHASE gate: an enable latch open while the data latch is. Its window
# is decided by the LIVE enable over the high phase -- the flop rule's arm bypass,
# which was already accepted -- so it must NOT be counted as window-held.
sed "s/always_latch if (!clk) en_1p = en;/always_latch if (clk) en_1p = en;/" "$W/wce.sv" > "$W/wce_glitch.sv"
cmp -s "$W/wce.sv" "$W/wce_glitch.sv" && fail "the glitchy-gate variant did not apply"
# Rule 4: a reader that commits at the SAME edge the data latch closes on.
sed "s/always_latch if (!clk) lo = qo + 8'd1;/always @(negedge clk) lo <= qo + 8'd1;/" "$W/wce.sv" > "$W/wce_l1.sv"
cmp -s "$W/wce.sv" "$W/wce_l1.sv" && fail "the same-edge reader variant did not apply"

TOP=wce   # the nested-gate section below switches it to nwce
build() { # <name>
  rm -rf "$W/lg_$1"
  "$LHD" compile "$W/$1.sv" --reader slang --top "$TOP" --emit-dir "lg:$W/lg_$1" --workdir "$W/cw_$1" \
    >"$W/c_$1.log" 2>&1 || { tail -5 "$W/c_$1.log"; fail "compile of $1.sv failed"; }
}
for m in wce wce_eq wce_bad wce_glitch wce_l1; do build "$m"; done

normalize() { # <name> -> output on stdout, rc as the pass's
  rm -rf "$W/lg_$1_n"
  "$LHD" pass single_edge --top "$TOP" "lg:$W/lg_$1" --emit-dir "lg:$W/lg_$1_n" --workdir "$W/pw_$1" 2>&1
}

# ---- the gate is ACCEPTED, as a window-held gate, at P=2 ----------------------
out="$(normalize wce)"
[ $? -eq 0 ] || { echo "$out" | tail -5; fail "pass single_edge refused the textbook gate in front of a latch"; }
grep -q "P=2 slots" <<<"$out" || { echo "$out" | tail -3; fail "the design did not normalize to P=2"; }
grep -q "1 window-held" <<<"$out" \
  || { echo "$out" | tail -3; fail "the gate was not folded as WINDOW-HELD (the latch rule did not fire)"; }
echo "ok: a clock gate in front of a latch normalizes (P=2, window-held)"

# ---- refusals that must stay refusals -------------------------------------------
out="$(normalize wce_glitch)"
[ $? -eq 0 ] || { echo "$out" | tail -3; fail "the same-phase gate, accepted before this rule, is now refused"; }
grep -q "0 window-held" <<<"$out" \
  || { echo "$out" | tail -3; fail "the same-phase gate was folded as WINDOW-HELD: its window follows the live enable, not a held one"; }
echo "ok: a same-phase gate keeps the bypass path (accepted, not window-held)"

# Rule 4's hazard -- a flop committing on the edge the latch CLOSES on samples
# its transparent D, not the held Q. By default it is REFUSED by name ...
out="$(normalize wce_l1)"
[ $? -ne 0 ] || fail "a reader committing at the latch's own closing edge was ACCEPTED by default (rule 4)"
grep -q "coincident-commit-edge" <<<"$out" || { echo "$out" | tail -3; fail "the same-edge reader was refused for the wrong reason"; }
echo "ok: by default a reader on the latch's own closing edge is refused (rule 4)"
# ... and with forward_same_edge=true it is admitted by GUARDED READER
# FORWARDING (decision 3A): that reader alone reads `E ? D : Q`. The
# differential below checks it against the source.
rm -rf "$W/lg_wce_l1_n"
out="$("$LHD" pass single_edge --top "$TOP" "lg:$W/lg_wce_l1" --emit-dir "lg:$W/lg_wce_l1_n" --workdir "$W/pw_wce_l1f" \
        --set forward_same_edge=true 2>&1)"
[ $? -eq 0 ] || { echo "$out" | grep -o '"message":"[^"]*' | head -2; fail "a flop on the latch's own closing edge was refused instead of forwarded"; }
grep -q "same-edge reader input(s) forwarded" <<<"$out" \
  || { echo "$out" | grep -o 'P=[0-9]* slots[^"]*' | head -1; fail "the same-edge reader was accepted WITHOUT forwarding"; }
echo "ok: a flop on the latch's own closing edge is forwarded the latch's value at the edge (3A)"

# ---- lhd lec: real verdicts, both directions --------------------------------------
lec_verdict() { # <impl> <ref> <tag>
  local out w
  out="$("$LHD" lec --set formal.effort=proof --impl "lg:$W/lg_$1" --ref "lg:$W/lg_$2" --top wce \
          --workdir "$W/lw_$3" 2>&1)"
  echo "$out" > "$W/lec_$3.log"
  w=$(grep -oE "^lec: '[^']*' [A-Z]+-[a-z]+" <<<"$out" | tail -1 | awk '{print $NF}')
  case "$w" in
    PROVEN-full | PROVEN-bounded) echo PROVEN ;;
    REFUTED-cex) echo REFUTED ;;
    *) echo "UNDECIDED($w)" ;;
  esac
}
v=$(lec_verdict wce_eq wce eq)
[ "$v" = PROVEN ] || { tail -5 "$W/lec_eq.log"; fail "the gated-latch design does not prove against an equivalent rewrite (got $v)"; }
echo "ok: lec PROVES the gated-latch design against an equivalent rewrite"
v=$(lec_verdict wce_bad wce bad)
[ "$v" = REFUTED ] || { tail -5 "$W/lec_bad.log"; fail "a real difference behind the gate was not REFUTED (got $v) -- the gate masks it"; }
echo "ok: lec REFUTES a real difference behind the gate (not vacuous)"

# ---- NESTED GATES: a gate whose clock is itself a gated clock ----------------------
# CORE-ET's CSR file: prim_write_commit_en pairs clocked by `clock_wb`, the output
# of a prim_clk_gate that CSR flops also clock on -- so `clock_wb` is a clock ROOT
# and the gate walk used to stop there, counting a second clock (B08: "3 clock
# nets" for one). resolve_icg now flattens a gate of a gated clock, and the inner
# enable latch -- shut at clk=1 only while the outer gate is on, which is the only
# time the data latch is open -- passes the window rule conditioned on the cone's
# other enables.
cat > "$W/nwce.sv" <<'NWCE_EOF'
module nwce(input clk, input en_wb, input en, input [7:0] d, output [7:0] q, output [7:0] r, output [7:0] f);
  reg g;                                      // the OUTER gate's enable latch (prim_clk_gate)
  always_latch if (!clk) g = en_wb;
  wire gclk = clk & g;                        // the gated clock ...
  reg [7:0] fr;                               // ... that a flop clocks on, which makes it a clock ROOT
  always @(posedge gclk) fr <= d;
  reg en_1p;                                  // the INNER gate's enable latch, on the GATED clock
  always_latch if (!gclk) en_1p = en;
  reg [7:0] qo;                               // the data latch: open while gclk && en_1p
  always_latch if (gclk && en_1p) qo = d;
  reg [7:0] lo;                               // race-free reader on the opposite phase
  always_latch if (!clk) lo = qo + 8'd1;
  assign q = qo;
  assign r = lo;
  assign f = fr;
endmodule
NWCE_EOF
TOP=nwce
build nwce
out="$(normalize nwce)"
[ $? -eq 0 ] || { echo "$out" | tail -3; fail "a latch pair on an already-gated clock was refused (the gate walk stopped at the inner gate)"; }
grep -q "clock nets" <<<"$out" && { echo "$out" | tail -3; fail "the nested gate was counted as a second clock"; }
grep -q "P=2 slots" <<<"$out" && grep -q "1 window-held" <<<"$out" \
  || { echo "$out" | tail -3; fail "the nested gate did not normalize as ONE clock at P=2 with the latch window-held"; }
echo "ok: a gate of a gated clock is one clock (P=2, window-held through both gates)"
TOP=wce

# ---- the transformation itself, against an independent simulator ------------------
if [ -z "${LHD_EXTERNAL_SIM:-}" ]; then
  echo "note: external-simulator leg skipped (set LHD_EXTERNAL_SIM=1)"
  echo "PASS: single_edge_latch_icg_test (structural, refusal and lec legs)"
  exit 0
fi
SIM=""
if command -v iverilog >/dev/null 2>&1 && command -v vvp >/dev/null 2>&1; then
  SIM=iverilog
elif command -v verilator >/dev/null 2>&1; then
  SIM=verilator
else
  fail "LHD_EXTERNAL_SIM is set but neither iverilog/vvp nor verilator is on PATH"
fi

emit() { # <lgdir> <outdir>
  rm -rf "$2"
  "$LHD" compile "lg:$1" --top "$TOP.$TOP" --emit-dir "verilog:$2" --workdir "$W/vw_$(basename "$2")" \
    >"$W/e_$(basename "$2").log" 2>&1 || { tail -5 "$W/e_$(basename "$2").log"; fail "verilog emission from $1 failed"; }
}
emit "$W/lg_wce" "$W/v_src"
emit "$W/lg_wce_n" "$W/v_norm"
cat "$W"/v_src/*.v > "$W/src.v"
sed 's/^module wce(/module wce_n(/' "$W"/v_norm/*.v > "$W/norm.v"
grep -q "always_latch" "$W/src.v" || fail "the SOURCE emission lost its latches -- nothing independent is compared"
grep -q "always_latch" "$W/norm.v" && fail "the NORMALIZED emission still holds a latch (a partial lowering)"

cat > "$W/tb.v" <<'EOF'
`timescale 1ns/1ps
// The normalized side is clocked TWICE per source period, its posedges ON the
// source edges: slot 0 at the rise (10+20p), slot 1 at the fall (20+20p).
// NHALF=10/NSTART=10 is the P=1 time base -- a negative control.
`ifndef NSTART
`define NSTART 5
`endif
`ifndef NHALF
`define NHALF 5
`endif
module tb;
  reg clk_s = 0, clk_n = 0, en = 1;
  reg [7:0] d = 8'd3;
  wire [7:0] qs, rs, qn, rn;
  integer p, errs = 0, checks = 0;
  always #10 clk_s = ~clk_s;
  initial begin #(`NSTART); forever #(`NHALF) clk_n = ~clk_n; end
  wce   s(.clk(clk_s), .en(en), .d(d), .q(qs), .r(rs));
  wce_n n(.clk(clk_n), .en(en), .d(d), .q(qn), .r(rn));
  task chk(input [7:0] nm, input [7:0] a, input [7:0] b);
    begin
      checks = checks + 1;
      if (p >= 3 && a !== b) begin errs = errs + 1; if (errs < 6) $display("MISMATCH %s p=%0d src=%0d norm=%0d", nm, p, a, b); end
    end
  endtask
  initial begin
    // Each latch is sampled only where it is OPAQUE (observation visibility).
    for (p = 0; p < 60; p = p + 1) begin
      #(4 + 20*p - $time);              // LOW: the value the enable latch will hold at the rise
      en = (p % 3) != 0;
      d  = (8'd17 * p + 8'd5) & 8'hff;
      #(12 + 20*p - $time);             // HIGH: the low reader is opaque
      chk("r", rs, rn);
      #(13 + 20*p - $time);             // HIGH: the data latch follows d ...
      d  = (8'd29 * p + 8'd11) & 8'hff;
      #(15 + 20*p - $time);             // ... while `en` moves with the enable latch SHUT
      if (p % 2 == 0) en = !en;
      #(22 + 20*p - $time);             // LOW: the data latch is opaque
      chk("q", qs, qn);
    end
    if (errs == 0) $display("WCE_DIFF_OK checks=%0d", checks); else $display("WCE_DIFF_FAIL errs=%0d", errs);
    $finish;
  end
endmodule
EOF

SRC_V="$W/src.v"
TB_V="$W/tb.v"
run_sim() { # <label> <normalized.v> [defines...]   (reads SRC_V / TB_V)
  local label=$1 norm=$2
  shift 2
  if [ "$SIM" = iverilog ]; then
    iverilog -g2012 "$@" -o "$W/$label.vvp" "$SRC_V" "$norm" "$TB_V" >"$W/sim_$label.log" 2>&1 \
      || { cat "$W/sim_$label.log"; fail "iverilog failed to elaborate ($label)"; }
    vvp "$W/$label.vvp" 2>&1
  else
    verilator --binary --timing -Wno-fatal -Wno-lint -Wno-style --top-module tb "$@" -Mdir "$W/obj_$label" \
      "$SRC_V" "$norm" "$TB_V" >"$W/sim_$label.log" 2>&1 \
      || { tail -5 "$W/sim_$label.log"; fail "verilator failed to build ($label)"; }
    "$W/obj_$label/Vtb" 2>&1
  fi
}

out="$(run_sim pos "$W/norm.v")"
grep -q "WCE_DIFF_OK" <<<"$out" \
  || { echo "$out" | grep -E 'MISMATCH|WCE' | head -6; fail "the normalized design does not match the real gated-latch source under $SIM"; }
echo "ok: source gated latch and normalized flops agree over 60 periods under $SIM"

# NEGATIVE CONTROL 1: the BYPASS -- read the enable latch's live input instead of
# its held Q. Derived from the emission and asserted to have applied (a sed that
# silently matches nothing tests nothing).
grep -q "if ((en_1p & single_edge_phase)) begin" "$W/norm.v" \
  || { grep -n "single_edge_phase" "$W/norm.v" | head; fail "the slot-1 commit is not spelled as expected; the bypass control cannot be derived"; }
sed 's/if ((en_1p & single_edge_phase)) begin/if ((en \& single_edge_phase)) begin/' "$W/norm.v" > "$W/norm_bypass.v"
cmp -s "$W/norm.v" "$W/norm_bypass.v" && fail "the bypass mutation did not apply"
out="$(run_sim byp "$W/norm_bypass.v")"
grep -q "WCE_DIFF_FAIL" <<<"$out" \
  || fail "the BYPASS variant still passed -- the harness cannot tell the held enable from the live one"
echo "ok: reading the enable latch's live input instead of its held Q FAILS (the no-bypass rule is load-bearing)"

# NEGATIVE CONTROL 2: the P=1 time base.
out="$(run_sim p1 "$W/norm.v" -DNHALF=10 -DNSTART=10)"
grep -q "WCE_DIFF_FAIL" <<<"$out" || fail "the harness still passed in the P=1 time base -- it cannot see the phase divider"
echo "ok: clocking the normalized side once per period FAILS (the harness discriminates)"

# ---- the FORWARDED same-edge reader (3A), against the same harness --------------------
emit "$W/lg_wce_l1" "$W/v_l1src"
emit "$W/lg_wce_l1_n" "$W/v_l1norm"
cat "$W"/v_l1src/*.v > "$W/l1src.v"
sed 's/^module wce(/module wce_n(/' "$W"/v_l1norm/*.v > "$W/l1norm.v"
grep -q "always_latch" "$W/l1norm.v" && fail "the NORMALIZED same-edge design still holds a latch"
SRC_V="$W/l1src.v"
out="$(run_sim l1pos "$W/l1norm.v")"
grep -q "WCE_DIFF_OK" <<<"$out" \
  || { echo "$out" | grep -E 'MISMATCH|WCE' | head -6; fail "the forwarded same-edge reader does not match its source under $SIM"; }
echo "ok: the forwarded same-edge reader agrees with its source over 60 periods under $SIM"
# NEGATIVE CONTROL: the forwarding REMOVED -- the reader's cloned `qo + 1`
# rewired back to the latch's held Q (what the lowering gave it before 3A).
# Derived from the emission and asserted to have applied.
sed -E "s/\\\$unsigned\(mux_[0-9]+ \+ \(9'h1\)\)/\$unsigned(qo + (9'h1))/" "$W/l1norm.v" > "$W/l1norm_stale.v"
cmp -s "$W/l1norm.v" "$W/l1norm_stale.v" \
  && { grep -n 'unsigned' "$W/l1norm.v" | head -3; fail "the stale-read mutation did not apply"; }
out="$(run_sim l1stale "$W/l1norm_stale.v")"
grep -q "WCE_DIFF_FAIL" <<<"$out" || fail "the reader reading the latch's HELD Q still passed -- the forwarding is not load-bearing"
echo "ok: the same reader reading the latch's held Q instead FAILS (the forwarding is load-bearing)"
SRC_V="$W/src.v"

# ---- the NESTED gate, against the same simulator ------------------------------------
TOP=nwce
emit "$W/lg_nwce" "$W/v_nsrc"
emit "$W/lg_nwce_n" "$W/v_nnorm"
cat "$W"/v_nsrc/*.v > "$W/nsrc.v"
sed 's/^module nwce(/module nwce_n(/' "$W"/v_nnorm/*.v > "$W/nnorm.v"
grep -q "always_latch" "$W/nnorm.v" && fail "the NORMALIZED nested design still holds a latch"
cat > "$W/ntb.v" <<'NTB_EOF'
`timescale 1ns/1ps
`ifndef NSTART
`define NSTART 5
`endif
`ifndef NHALF
`define NHALF 5
`endif
module tb;
  reg clk_s = 0, clk_n = 0, en = 1, en_wb = 1;
  reg [7:0] d = 8'd3;
  wire [7:0] qs, rs, fs, qn, rn, fn;
  integer p, errs = 0, checks = 0;
  always #10 clk_s = ~clk_s;
  initial begin #(`NSTART); forever #(`NHALF) clk_n = ~clk_n; end
  nwce   s(.clk(clk_s), .en_wb(en_wb), .en(en), .d(d), .q(qs), .r(rs), .f(fs));
  nwce_n n(.clk(clk_n), .en_wb(en_wb), .en(en), .d(d), .q(qn), .r(rn), .f(fn));
  task chk(input [7:0] nm, input [7:0] a, input [7:0] b);
    begin
      checks = checks + 1;
      if (p >= 3 && a !== b) begin errs = errs + 1; if (errs < 6) $display("MISMATCH %s p=%0d src=%0d norm=%0d", nm, p, a, b); end
    end
  endtask
  initial begin
    for (p = 0; p < 80; p = p + 1) begin
      #(4 + 20*p - $time);              // LOW: what both enable latches hold at the rise
      en_wb = (p % 4) != 1;
      en    = (p % 3) != 0;
      d     = (8'd17 * p + 8'd5) & 8'hff;
      #(12 + 20*p - $time);             // HIGH: the low reader is opaque; the flop has sampled
      chk("r", rs, rn);
      chk("f", fs, fn);
      #(13 + 20*p - $time);             // HIGH: the data latch follows d ...
      d     = (8'd29 * p + 8'd11) & 8'hff;
      #(15 + 20*p - $time);             // ... while BOTH enables move with their latches shut
      if (p % 2 == 0) en = !en;
      if (p % 5 == 0) en_wb = !en_wb;
      #(22 + 20*p - $time);             // LOW: the data latch is opaque
      chk("q", qs, qn);
    end
    if (errs == 0) $display("WCE_DIFF_OK checks=%0d", checks); else $display("WCE_DIFF_FAIL errs=%0d", errs);
    $finish;
  end
endmodule
NTB_EOF
SRC_V="$W/nsrc.v"
TB_V="$W/ntb.v"
out="$(run_sim npos "$W/nnorm.v")"
grep -q "WCE_DIFF_OK" <<<"$out" \
  || { echo "$out" | grep -E 'MISMATCH|WCE' | head -6; fail "the normalized NESTED design does not match its source under $SIM"; }
echo "ok: a latch pair behind a gate of a gated clock agrees with its source over 80 periods under $SIM"
# THE SLOT-1 COMMIT, by its TERMS rather than by one spelling. The guard is a
# chain of `and1` folds onto ONE commutative And node, so the operand order in
# the emitted text is the graph's driver order for that sink, not a property
# single_edge promises -- it has flipped (`g & en_1p` -> `en_1p & g`) with no
# change to the enable walk or the fold. What the rule actually claims is that
# the commit reads the phase and BOTH gates' HELD enables, so check that, and
# derive the mutants from the line as emitted so order cannot make them no-ops.
CMT="$(grep -oE 'if \(\(single_edge_phase( & [A-Za-z_][A-Za-z_0-9]*)+\)\) begin' "$W/nnorm.v" | head -1)"
[ -n "$CMT" ] \
  || { grep -n "single_edge_phase" "$W/nnorm.v" | head; fail "no slot-1 commit gated on the phase divider was emitted"; }
grep -qE '\ben_1p\b' <<<"$CMT" && grep -qE '\bg\b' <<<"$CMT" \
  || { echo "$CMT"; fail "the nested slot-1 commit does not read BOTH held enables (en_1p and g)"; }
# NEGATIVE CONTROLS: each held read replaced by its gate latch's live input.
mutate() { # <replacement line> -> the design with the commit line replaced
  awk -v old="$CMT" -v new="$1" '
    { p = index($0, old)
      if (p > 0) { $0 = substr($0, 1, p - 1) new substr($0, p + length(old)) }
      print }' "$W/nnorm.v"
}
mutate "$(sed -E 's/\ben_1p\b/en/'    <<<"$CMT")" > "$W/nnorm_inner.v"
mutate "$(sed -E 's/\bg\b/en_wb/'     <<<"$CMT")" > "$W/nnorm_outer.v"
cmp -s "$W/nnorm.v" "$W/nnorm_inner.v" && fail "the inner-bypass mutation did not apply"
cmp -s "$W/nnorm.v" "$W/nnorm_outer.v" && fail "the outer-bypass mutation did not apply"
out="$(run_sim ninner "$W/nnorm_inner.v")"
grep -q "WCE_DIFF_FAIL" <<<"$out" || fail "reading the INNER enable latch's live input still passed"
out="$(run_sim nouter "$W/nnorm_outer.v")"
grep -q "WCE_DIFF_FAIL" <<<"$out" || fail "reading the OUTER enable latch's live input still passed"
echo "ok: replacing either held enable by its live input FAILS (both held reads are load-bearing)"

echo "PASS: single_edge_latch_icg_test ($SIM)"
