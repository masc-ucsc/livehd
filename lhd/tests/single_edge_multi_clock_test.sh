#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# pass.single_edge with `multi_clock=true`: a design with TWO UNRELATED CLOCKS is
# normalized in its reference domain and EXPORTED with clock domains, instead of
# being refused as "2 clock nets and no known integer ratio between them".
#
# Before this option the pass refused every such design, which blocked the
# genuinely two-clock CORE-ET module (vpu_ctrl: 600 flops on clock_sec, 185 on
# clock_aon) from ever producing a certificate. The refusal was honest for the
# consumers of the time: a Verilog/encoder model with ONE implicit clock cannot
# say which edge a flop commits on. The verified-compiler certificate now can --
# `DesignCert.clocks` plus a clock ordinal on every FlopDesc/MemoryDesc, and a
# Lean step that takes an edge vector -- so the pass may leave a plain posedge
# flop on its own clock and let the consumer read its domain.
#
# The design is the minimal shape that exercises it:
#
#   posedge flop `a` on clk_a   -> reference domain, slot 0
#   negedge flop `n` on clk_a   -> reference domain, slot 1   <- forces P=2
#   posedge flop `b` on clk_b   -> SECOND domain: must be left exactly as it is
#
# THREE independent things are asserted.
#
# (1) POLICY. Without `multi_clock` the pass still refuses, by name
#     (`multi-clock-no-ratio`, naming both nets); with it the pass fires, lowers
#     the reference domain into P=2 and reports `2 clock domain(s) exported`.
#     The normalized emission holds no `negedge`, and STILL clocks `b` on
#     `posedge clk_b` -- the second domain was exported, not slotted.
#
# (2) TRACE-LEVEL VALIDATION against iverilog, at PERIOD BOUNDARIES of clk_a.
#     Source and normalized designs run side by side, the normalized clk_a at
#     twice the rate, clk_b IDENTICAL on both sides, and every output is compared.
#     Edge normalization is not cycle-preserving, so no cycle-accurate equivalence
#     checker can validate it; the binding cross-model rule is that "PROVEN
#     before AND after" cannot see a transformation applied identically to both
#     sides. The P=1 negative control below shows the harness discriminates.
#
# (3) THE CERTIFICATE. pass.lean in verified_compiler mode exports the
#     normalized graph with a TWO-entry clock table, `a`/`n`/the divider on
#     ordinal 0 (the reference, `clk_a`) and `b` on ordinal 1 (`clk_b`). This is
#     the data the Lean semantics reads; a certificate that named one clock, or
#     put `b` on ordinal 0, would make `b` commit on clk_a's edges.

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

# ---- the design --------------------------------------------------------------
cat > "$W/mclk.v" <<'VEOF'
module mclk(input clk_a, input clk_b, input reset, input [7:0] d,
            output [7:0] qp, output [7:0] qn, output [7:0] qb);
  reg [7:0] a, n, b;
  // ASYNCHRONOUS resets on purpose: slang/yosys fold a synchronous reset into
  // the flop's next-value mux, leaving no `reset_pin` for the synthesized phase
  // divider to copy, and a divider with only an `initial` value starts as X
  // under iverilog.  An async reset also exercises the certificate's new
  // `asyncReset := true` flag.
  always @(posedge clk_a or posedge reset) if (reset) a <= 8'd0; else a <= d;          // reference, slot 0
  always @(negedge clk_a or posedge reset) if (reset) n <= 8'd0; else n <= a;          // reference, slot 1: forces P=2
  always @(posedge clk_b or posedge reset) if (reset) b <= 8'd0; else b <= d + 8'd1;   // second domain: left alone
  assign qp = a;
  assign qn = n;
  assign qb = b;
endmodule
VEOF

rm -rf "$W/lg_src"
"$LHD" compile "$W/mclk.v" --reader yosys-slang --top mclk --emit-dir "lg:$W/lg_src" --workdir "$W/cw" \
  >"$W/compile.log" 2>&1 || { tail -8 "$W/compile.log"; fail "compile of mclk.v failed"; }

# ---- (1): policy ---------------------------------------------------------------
rm -rf "$W/lg_ctl"
if "$LHD" pass single_edge --top mclk "lg:$W/lg_src" --emit-dir "lg:$W/lg_ctl" --workdir "$W/pw0" >"$W/pass0.log" 2>&1; then
  fail "without multi_clock the pass must still REFUSE a two-clock design (it applied or skipped instead)"
fi
grep -q "multi-clock-no-ratio" "$W/pass0.log" || { tail -6 "$W/pass0.log"; fail "the default refusal is not the named multi-clock one"; }
grep -q "clk_a" "$W/pass0.log" && grep -q "clk_b" "$W/pass0.log" \
  || { grep -o '"message":"[^"]*' "$W/pass0.log" | head -1; fail "the refusal does not NAME both clock nets"; }
echo "ok: without multi_clock the design is refused by name, naming clk_a and clk_b"

rm -rf "$W/lg_norm"
"$LHD" pass single_edge --top mclk "lg:$W/lg_src" --emit-dir "lg:$W/lg_norm" --set multi_clock=true \
  --workdir "$W/pw" >"$W/pass.log" 2>&1 || { tail -8 "$W/pass.log"; fail "pass single_edge refused or failed with multi_clock=true"; }
grep -q "single-edge-applied" "$W/pass.log" || { tail -6 "$W/pass.log"; fail "the pass did not fire (a negedge flop should force P=2)"; }
grep -q "P=2 slots" "$W/pass.log" || { grep -o "P=[0-9]* slots[^\"]*" "$W/pass.log" | head -1; fail "the reference domain did not lower into a 2-slot time base"; }
grep -q "2 clock domain(s) exported" "$W/pass.log" \
  || { grep -o "P=[0-9]* slots[^\"]*" "$W/pass.log" | head -1; fail "the pass did not report the second clock domain as exported"; }
echo "ok: with multi_clock=true the pass fires: P=2 on the reference, 2 clock domains exported"

emit() { # <lgdir> <outdir>
  rm -rf "$2"
  "$LHD" compile "lg:$1" --top mclk --emit-dir "verilog:$2" --workdir "$W/vw_$(basename "$2")" \
    >"$W/e_$(basename "$2").log" 2>&1 \
    || { tail -6 "$W/e_$(basename "$2").log"; fail "verilog emission from $1 failed"; }
}
emit "$W/lg_src"  "$W/v_src"
emit "$W/lg_norm" "$W/v_norm"

cat "$W"/v_src/*.v > "$W/src.v"
sed 's/^module mclk(/module mclk_n(/' "$W"/v_norm/*.v > "$W/norm.v"

grep -q "negedge" "$W/src.v"  || fail "the SOURCE emission lost its negedge -- nothing independent is being compared"
grep -q "negedge" "$W/norm.v" && fail "the NORMALIZED emission still holds a negedge flop (a partial lowering)"
grep -q "posedge clk_b" "$W/norm.v" || fail "the NORMALIZED emission no longer clocks anything on clk_b: the second domain was slotted or rebound instead of exported"
echo "ok: normalized has no negedge and still commits state on posedge clk_b"

# ---- (3): the certificate names both domains ---------------------------------
rm -rf "$W/lean"
"$LHD" compile "lg:$W/lg_norm" --top mclk --workdir "$W/lw" --emit-dir "lean:$W/lean" \
  --set formal.lean.strict=true --set formal.lean.emit_cert=true --set formal.lean.mode=verified_compiler \
  >"$W/lean.log" 2>&1 || { tail -8 "$W/lean.log"; fail "pass.lean (verified_compiler) refused the normalized two-clock design"; }
CERT="$(ls "$W"/lean/*_Lgraph.lean 2>/dev/null | head -1)"
[ -n "$CERT" ] || fail "no certificate was emitted"
grep -qE 'clocks +:= #\[' "$CERT" || fail "the certificate has no clock table"
grep -q '{ name := "clk_a" }' "$CERT" || fail "the certificate does not name clk_a"
grep -q '{ name := "clk_b" }' "$CERT" || fail "the certificate does not name clk_b"
# ordinal 0 = the reference (most state: a, n and the divider); the ONE flop on
# ordinal 1 is `b`.  Count FlopDesc lines per ordinal.
n0=$(grep -c 'clock := 0, asyncReset' "$CERT")
n1=$(grep -c 'clock := 1, asyncReset' "$CERT")
[ "$n1" -eq 1 ] || { grep -o 'clock := [0-9]*' "$CERT" | sort | uniq -c; fail "expected exactly ONE flop on clock ordinal 1 (b), found $n1"; }
[ "$n0" -ge 3 ] || { grep -o 'clock := [0-9]*' "$CERT" | sort | uniq -c; fail "expected a, n and the phase divider on clock ordinal 0, found $n0"; }
grep -q 'clock := 1, asyncReset := true' "$CERT" || fail "the async-reset flop b is not marked asyncReset := true (its reset must act in a step where clk_b is quiet)"
grep -q 'asyncReset := false' "$CERT" || fail "the phase divider (a synchronous copy of the design's reset) is not marked asyncReset := false"
echo "ok: certificate declares clocks #[clk_a, clk_b]; $n0 flop(s) on ordinal 0, 1 on ordinal 1; async flags carried"

# ---- (2): trace-level validation against iverilog -----------------------------
if ! command -v iverilog >/dev/null 2>&1 || ! command -v vvp >/dev/null 2>&1; then
  echo "note: iverilog/vvp not found -- the INDEPENDENT trace validation was SKIPPED."
  echo "PASS: single_edge_multi_clock_test (structural legs only)"
  exit 0
fi

cat > "$W/tb.v" <<TBEOF
\`timescale 1ns/1ps
// HALF = the normalized clk_a half-period. 5 gives it TWO posedges per source
// period (P=2, correct); 10 gives it one, the P=1 time base -- the negative
// control below, which must FAIL.
\`ifndef HALF
\`define HALF 5
\`endif
module tb;
  reg clk_s = 0, clk_n = 0, clk_b = 0, reset = 1;
  reg [7:0] d = 0;
  wire [7:0] s_qp, s_qn, s_qb, n_qp, n_qn, n_qb;
  integer errs = 0, p;

  always #10      clk_s = ~clk_s;   // source clk_a:     rise 10+20p, fall 20+20p
  always #(\`HALF) clk_n = ~clk_n;   // normalized clk_a: slot0 15+20p, slot1 25+20p
  always #20      clk_b = ~clk_b;   // clk_b, IDENTICAL on both sides: rise 20+40k

  mclk   src(.clk_a(clk_s), .clk_b(clk_b), .reset(reset), .d(d), .qp(s_qp), .qn(s_qn), .qb(s_qb));
  mclk_n nrm(.clk_a(clk_n), .clk_b(clk_b), .reset(reset), .d(d), .qp(n_qp), .qn(n_qn), .qb(n_qb));

  task chk(input [63:0] nm, input [7:0] a, input [7:0] b);
    if (a !== b) begin
      errs = errs + 1;
      if (errs < 6) \$display("MISMATCH %0s @%0t source=%0d normalized=%0d", nm, \$time, a, b);
    end
  endtask

  initial begin
    // Stimulus changes at 6+20p, STABLE across every sampling edge on both sides
    // (clk_a rise 10+20p / fall 20+20p; normalized slot0 15+20p / slot1 25+20p;
    // clk_b rise 20+40k).
    reset = 1; d = 0;
    #26 d = 8'd37;
    #1  reset = 0;   // t=27: after every period-0 edge, so the divider starts period 1 at slot 0
    #11;             // t=38 = 18+20*1
    for (p = 1; p < 40; p = p + 1) begin
      chk("qp", s_qp, n_qp);       // reference posedge state, settled after both sides' slot-0 commit
      chk("qb", s_qb, n_qb);       // the SECOND domain, untouched by the lowering
      #8  d = (p * 8'd37 + 8'd11) & 8'hff;   // t=26+20p
      #2  chk("qn", s_qn, n_qn);   // t=28+20p: negedge state, settled after both falls
      #10;                         // t=38+20p
    end
    if (errs == 0) \$display("SINGLE_EDGE_MCLK_DIFF_OK");
    else           \$display("SINGLE_EDGE_MCLK_DIFF_FAIL errs=%0d", errs);
    \$finish;
  end
endmodule
TBEOF

run_iv() { # <label> <extra iverilog args...>
  local label=$1
  shift
  iverilog -g2012 "$@" -o "$W/$label.vvp" "$W/src.v" "$W/norm.v" "$W/tb.v" >"$W/iv_$label.log" 2>&1 \
    || { cat "$W/iv_$label.log"; fail "iverilog failed to elaborate ($label)"; }
  vvp "$W/$label.vvp" >"$W/vvp_$label.log" 2>&1
  cat "$W/vvp_$label.log"
}

out="$(run_iv p2)"
grep -q "SINGLE_EDGE_MCLK_DIFF_OK" <<<"$out" \
  || { echo "$out" | head -8; fail "the NORMALIZED design does not match the two-clock source under iverilog"; }
echo "ok: source vs normalized agree over 39 periods under iverilog, second domain included (independent oracle)"

# Negative control: the normalized side's clk_a in the P=1 time base. Its slot
# gates now fire every OTHER period and its lowered negedge flop lags, so this
# must FAIL -- or the comparison above proves nothing about the divider.
out="$(run_iv p1 -DHALF=10)"
grep -q "SINGLE_EDGE_MCLK_DIFF_FAIL" <<<"$out" \
  || fail "the differential harness still passed with the normalized clk_a at 1x -- it cannot detect a wrong time base"
echo "ok: forcing the normalized clk_a to a P=1 time base FAILS (the harness discriminates)"

echo "PASS: single_edge_multi_clock_test"
