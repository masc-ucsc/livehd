#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# CORE-ET's latch primitives, read the way every CORE-ET and CVA6 row is read
# (yosys-slang, `yosys.setundef=zero`), normalize WITHOUT the clock read as data.
#
# Under that reader `!clk` arrives as `Mux(Ror(clk), 1, 0)` and a latch's
# don't-care hold arm as the KNOWN constant 0, so `if (!clk_i) en_1p <= en_i`
# is `enable = !Ror(clk)`, `din = Ror(clk) ? 0 : en`. pass.single_edge stopped
# its phase walk at the Ror (filing the latch DATA-gated, its enable kept) and
# peeled only a Q-feedback hold arm (leaving `Ror(clk) ? 0 : en` in din), so the
# certificate read the clock as data on live state: CORE-ET B01 (24 consumers),
# B03 and B11. Both are fixed, and the preview register file's latch -- open
# while a GATED clock is low, read only by a flop on that gated clock's falling
# edge -- is now admitted as masked while gated off.
#
#   sul        prim_write_commit_en + prim_write_preview_en at widths 1 and 4
#              (a wide latch is bit-blasted: each bit's din is a BIT SELECT
#              of one hold mux), prim_clk_gate + prim_rf_single_1r1w_par_preview,
#              and prim_phase_pair's tied enable latch on the gated clock
#              (both hold-mux arms constant)
#   sul_open   the preview latch also drives an output: transparent while
#              gated off and OBSERVED -- refused (gated-latch-open-when-off)
#   sul_rise   its reader on the gated clock's RISING edge, where the latch
#              closes: not a masked read -- refused
#
# The external-simulator leg (LHD_EXTERNAL_SIM=1, iverilog/vvp) compares the
# ORIGINAL RTL against the normalized emission, with two negative controls.

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

cat > "$W/sul.sv" <<'EOF'
module sul(input clk, input en, input en_g, input nxt, input [7:0] d,
           output qa, output ra, output qb, output [7:0] rq,
           output [3:0] qw, output [3:0] qv, output qk);
  logic en_1p;                                  // prim_write_commit_en: enable latch shut while clk is high
  always_latch if (!clk) en_1p <= en;
  logic qa_l;                                   // ... data latch open while clk && en_1p
  always_latch if (clk && en_1p) qa_l <= d[0];
  logic ra_l;                                   // race-free reader on the opposite phase
  always_latch if (!clk) ra_l <= qa_l;
  logic en_2p;                                  // prim_write_preview_en: enable latch open while clk is high
  always_latch if (clk) en_2p <= en;
  logic qb_l;                                   // ... data latch open while !clk && en_2p
  always_latch if (!clk && en_2p) qb_l <= d[1];
  logic el;                                     // prim_clk_gate
  always_latch if (!clk) el <= en_g;
  wire gclk = clk & el;
  logic pl;                                     // preview latch: open while the GATED clock is low
  always_latch if (!gclk) pl <= nxt;
  logic [7:0] dq;                               // its only reader: the gated clock's falling edge
  always_ff @(negedge gclk) if (pl) dq <= d;
  logic [7:0] rf;
  always_ff @(posedge gclk) rf <= dq;
  logic [3:0] qw_l;                             // the commit pair at WIDTH 4: the reader bit-blasts it,
  always_latch if (clk && en_1p) qw_l <= d[7:4];  // each bit's din a bit select of one hold mux
  logic [3:0] qv_l;                             // the preview pair at width 4
  always_latch if (!clk && en_2p) qv_l <= d[7:4];
  logic hk;                                     // prim_phase_pair on a GATED clock: a TIED enable
  always_latch if (!gclk) hk <= 1'b1;           // (`Ror(gclk) ? 0 : 1`, both arms constant)
  logic qk_l;                                   // read only through the gate itself
  always_latch if (gclk && hk) qk_l <= d[2];
  assign qa = qa_l;
  assign ra = ra_l;
  assign qb = qb_l;
  assign rq = rf;
  assign qw = qw_l;
  assign qv = qv_l;
  assign qk = qk_l;
endmodule
EOF
sed -e 's/^module sul(/module sul_open(/' -e "s/^  assign rq = rf;/  assign rq = rf ^ {7'b0, pl};/" "$W/sul.sv" > "$W/sul_open.sv"
grep -q "rf ^ {7'b0, pl}" "$W/sul_open.sv" || fail "the observed-latch variant did not apply"
sed -e 's/^module sul(/module sul_rise(/' -e 's/always_ff @(negedge gclk) if (pl) dq <= d;/always_ff @(posedge gclk) if (pl) dq <= d;/' \
  "$W/sul.sv" > "$W/sul_rise.sv"
grep -q 'always_ff @(posedge gclk) if (pl) dq <= d;' "$W/sul_rise.sv" || fail "the rising-reader variant did not apply"

# Compiled exactly as the CORE-ET rows are, which also runs the latch contract.
build() { # <name>
  rm -rf "$W/lg_$1"
  "$LHD" compile "$W/$1.sv" --reader yosys-slang --top "$1" --set yosys.setundef=zero --emit-dir "lg:$W/lg_$1" \
    --workdir "$W/cw_$1" >"$W/c_$1.log" 2>&1 \
    || { grep -o '"message":"[^"]*' "$W/c_$1.log" | head -3; fail "compile of $1.sv failed"; }
}
for m in sul sul_open sul_rise; do build "$m"; done
echo "ok: the three designs compile (the latch contract accepts them)"

normalize() { # <name>  -> pass output on stdout, rc as the pass's
  rm -rf "$W/lgn_$1"
  "$LHD" pass single_edge --top "$1" "lg:$W/lg_$1" --emit-dir "lg:$W/lgn_$1" --workdir "$W/pw_$1" 2>&1
}

# ---- accepted, and the certificate does not read the clock -------------------------
out="$(normalize sul)"
[ $? -eq 0 ] || { echo "$out" | grep -o '"message":"[^"]*' | head -3; fail "pass single_edge refused the CORE-ET latch primitives"; }
grep -q "P=2 slots" <<<"$out" || { echo "$out" | grep -o 'P=[0-9]* slots[^"]*' | head -1; fail "the design did not normalize to P=2"; }
rm -rf "$W/lean"
"$LHD" compile "lg:$W/lgn_sul" --top sul --workdir "$W/lw" --emit-dir "lean:$W/lean" \
  --set formal.lean.mode=verified_compiler >"$W/lean.log" 2>&1 \
  || { grep -o '"message":"[^"]*' "$W/lean.log" | grep -v DEFERRED | head -2; fail "pass.lean refused the normalized design"; }
grep -q '{"name": "clk", "ordinal": [0-9]*, "width": 1, "used_by_model": false}' "$W/lean/sul_io.json" \
  || { grep '"clk"' "$W/lean/sul_io.json"; fail "the certificate still reads clk as data"; }
echo "ok: normalized at P=2, and the certificate does not read clk (used_by_model false)"

# ---- refusals that must stay refusals -------------------------------------------------
out="$(normalize sul_open)"
[ $? -ne 0 ] || fail "a gated-clock latch OBSERVED while gated off was accepted"
grep -q '"code":"gated-latch-open-when-off"' <<<"$out" \
  || { echo "$out" | grep -o '"message":"[^"]*' | head -2; fail "the observed latch was refused for the wrong reason"; }
echo "ok: the preview latch read by an output is refused (open while gated off)"

out="$(normalize sul_rise)"
[ $? -ne 0 ] || fail "a reader sampling the preview latch on the edge it closes was accepted"
grep -q '"code":"gated-latch-open-when-off"' <<<"$out" \
  || { echo "$out" | grep -o '"message":"[^"]*' | head -2; fail "the rising-edge reader was refused for the wrong reason"; }
echo "ok: a reader on the gated clock's rising edge is not a masked read (refused)"

# ---- the transformation itself, against an independent simulator ------------------
if [ -z "${LHD_EXTERNAL_SIM:-}" ]; then
  echo "note: external-simulator leg skipped (set LHD_EXTERNAL_SIM=1)"
  echo "PASS: single_edge_setundef_latch_test (structural and refusal legs)"
  exit 0
fi
command -v iverilog >/dev/null 2>&1 && command -v vvp >/dev/null 2>&1 \
  || fail "LHD_EXTERNAL_SIM is set but iverilog/vvp are not on PATH"

rm -rf "$W/v_norm"
"$LHD" compile "lg:$W/lgn_sul" --top sul --emit-dir "verilog:$W/v_norm" --workdir "$W/vw" >"$W/e.log" 2>&1 \
  || { tail -5 "$W/e.log"; fail "verilog emission of the normalized design failed"; }
sed 's/^module sul(/module sul_n(/' "$W"/v_norm/*.v > "$W/norm.v"
grep -q "^module sul_n(" "$W/norm.v" || fail "the normalized module was not renamed"
grep -q "always_latch" "$W/norm.v" && fail "the NORMALIZED emission still holds a latch"
# A source with the gate removed: what dropping the gate would compute.
sed 's/^  wire gclk = clk & el;/  wire gclk = clk;/' "$W/sul.sv" > "$W/sul_nogate.sv"
cmp -s "$W/sul.sv" "$W/sul_nogate.sv" && fail "the gate-less variant did not apply"

cat > "$W/tb.v" <<'EOF'
`timescale 1ns/1ps
// Source edges: rise at 10+20p, fall at 20+20p. The normalized side is clocked
// twice per period, ON those edges: slot 0 at the rise, slot 1 at the fall.
// NHALF=10/NSTART=10 is the P=1 time base -- a negative control.
`ifndef NSTART
`define NSTART 5
`endif
`ifndef NHALF
`define NHALF 5
`endif
module tb;
  reg clk_s = 0, clk_n = 0, en = 1, en_g = 1, nxt = 1;
  reg [7:0] d = 8'd3;
  wire qas, ras, qbs, qan, ran, qbn, qks, qkn;
  wire [7:0] rqs, rqn;
  wire [3:0] qws, qwn, qvs, qvn;
  integer p, errs = 0, checks = 0;
  always #10 clk_s = ~clk_s;
  initial begin #(`NSTART); forever #(`NHALF) clk_n = ~clk_n; end
  sul   s(.clk(clk_s), .en(en), .en_g(en_g), .nxt(nxt), .d(d), .qa(qas), .ra(ras), .qb(qbs), .rq(rqs),
          .qw(qws), .qv(qvs), .qk(qks));
  sul_n n(.clk(clk_n), .en(en), .en_g(en_g), .nxt(nxt), .d(d), .qa(qan), .ra(ran), .qb(qbn), .rq(rqn),
          .qw(qwn), .qv(qvn), .qk(qkn));
  task chk(input [15:0] nm, input [7:0] a, input [7:0] b);
    begin
      checks = checks + 1;
      if (p >= 6 && a !== b) begin errs = errs + 1; if (errs < 8) $display("MISMATCH %s p=%0d src=%0d norm=%0d", nm, p, a, b); end
    end
  endtask
  initial begin
    // Inputs move only between edges; every latch is compared where it is OPAQUE.
    for (p = 0; p < 120; p = p + 1) begin
      #(3 + 20*p - $time);              // LOW: the low-open latches follow
      en   = (p < 3) || ((p % 3) != 2);
      en_g = (p < 3) || ((p % 4) != 3);
      nxt  = ((p * 5) % 7) < 4;
      d    = (8'd37 * p + 8'd9) & 8'hff;
      #(6 + 20*p - $time);              // LOW: ... and keep following
      if (p % 2) nxt = !nxt;
      d    = (8'd53 * p + 8'd1) & 8'hff;
      #(8 + 20*p - $time);              // LOW: qa is shut since the fall
      chk("qa", {7'b0, qas}, {7'b0, qan});
      chk("rq", rqs, rqn);
      chk("qw", {4'b0, qws}, {4'b0, qwn});
      chk("qk", {7'b0, qks}, {7'b0, qkn});
      #(13 + 20*p - $time);             // HIGH: qa follows d; pl follows nxt while gated off
      d    = (8'd29 * p + 8'd11) & 8'hff;
      nxt  = ((p * 3) % 5) < 2;
      #(16 + 20*p - $time);             // HIGH: en_2p follows; en_1p and el are shut
      if (p % 2 == 0) en = !en;
      if (p % 5 == 0) en_g = !en_g;
      #(18 + 20*p - $time);             // HIGH: ra and qb are shut since the rise
      chk("ra", {7'b0, ras}, {7'b0, ran});
      chk("qb", {7'b0, qbs}, {7'b0, qbn});
      chk("qv", {4'b0, qvs}, {4'b0, qvn});
      chk("rq", rqs, rqn);
    end
    if (errs == 0) $display("SUL_DIFF_OK checks=%0d", checks); else $display("SUL_DIFF_FAIL errs=%0d", errs);
    $finish;
  end
endmodule
EOF

run_iv() { # <label> <source.sv> [defines...]
  local label=$1 src=$2
  shift 2
  iverilog -g2012 "$@" -o "$W/$label.vvp" "$src" "$W/norm.v" "$W/tb.v" >"$W/iv_$label.log" 2>&1 \
    || { cat "$W/iv_$label.log"; fail "iverilog failed to elaborate ($label)"; }
  vvp "$W/$label.vvp" 2>&1
}
out="$(run_iv pos "$W/sul.sv")"
grep -q "SUL_DIFF_OK" <<<"$out" \
  || { echo "$out" | grep -E 'MISMATCH|SUL_' | head -8; fail "the normalized design does not match the RTL under iverilog"; }
echo "ok: RTL and normalized agree over 120 periods under iverilog ($(grep -o 'checks=[0-9]*' <<<"$out"))"

# NEGATIVE CONTROL 1: the P=1 time base.
out="$(run_iv p1 "$W/sul.sv" -DNHALF=10 -DNSTART=10)"
grep -q "SUL_DIFF_FAIL" <<<"$out" || fail "the harness still passed in the P=1 time base -- it cannot see the phase divider"
echo "ok: clocking the normalized side once per period FAILS"

# NEGATIVE CONTROL 2: the same comparison against a source with the gate removed.
out="$(run_iv nogate "$W/sul_nogate.sv")"
grep -q "SUL_DIFF_FAIL" <<<"$out" || fail "the harness also passed against a gate-less source -- it cannot see the gate"
echo "ok: the same comparison against a gate-less source FAILS (the gate is load-bearing)"

echo "PASS: single_edge_setundef_latch_test"
