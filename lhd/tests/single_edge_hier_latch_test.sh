#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# todo/livehd/2f-latch — TRACKER: a latch or negedge flop INSIDE A DEF.
#
# This is the shape that keeps lhdsuite's minion from dropping its `lec_trust`
# knob. Measured 2026-07-25: `minion_lec` proves 110/140 defs and TRUSTS 32, and
# running it with an EMPTY trust list refuses with
#
#   def `prim_rf_1r1w_preview_p2` holds 1 latch cell(s), 1 negedge flop(s),
#   and normalizing across a module boundary is not supported yet
#
# i.e. every one of those 32 trust entries exists because the latch lives one
# module level down. `pass.single_edge` rewrites a design's OWN body; it declines
# when a def it instantiates holds state it would have to re-time, because the
# phase counter is not port-threaded and a def slotted to P=2 inside a parent at
# P=1 would be compared in two different time bases.
#
# WHAT THIS FILE PINS TODAY: the refusal is HONEST and NAMES the def and what is
# in it. That matters — the same shape used to come back falsely PROVEN (the
# trigger only scanned the top body), and the diagnostic has to say which def so
# a `formal.lec.trust` entry can be written without guessing.
#
# WHAT WOULD CLOSE IT: normalize each reachable def IN PLACE, bottom-up. The P=1
# subset is the safe and valuable part — a def whose latches are all one commit
# class is a pure retype with no divider and therefore no cross-module timing
# question at all, and that covers most of minion's 32 (the `prim_write_commit_*`
# and RF-preview families). P>1 inside a def needs the phase counter port-threaded
# first (todo open question 6).
#
# The CONTRAST cases below are what make this a real test rather than a note:
# an instantiated CLOCK-GATE cell with a latch inside is NOT refused (it is
# inlined and folded — see single_edge_icg_test.sh), and a def with only ordinary
# posedge state is NOT refused either. So the refusal is narrow, not a blanket
# "any hierarchy".

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

build() { # <name> <src> <top>
  rm -rf "$W/lg_$1"
  "$LHD" compile "$2" --reader slang --top "$3" --emit-dir "lg:$W/lg_$1" --workdir "$W/cw_$1" \
    >"$W/c_$1.log" 2>&1 || { tail -5 "$W/c_$1.log"; fail "compile of $2 failed"; }
}

# ---- 1. a LATCH inside a def: refused, and the diagnostic names it -----------
cat > "$W/hlat.v" <<'EOF'
module leaf(input en, input [7:0] d, output [7:0] o);
  logic [7:0] l;
  always_latch if (en) l <= d;
  assign o = l;
endmodule

module dut(input clk, input en, input [7:0] d, output [7:0] q);
  wire [7:0] t;
  leaf u(.en(en), .d(d), .o(t));
  reg [7:0] f;
  always @(posedge clk) f <= t;
  assign q = f;
endmodule
EOF
build hlat "$W/hlat.v" dut

out="$("$LHD" lec --impl "lg:$W/lg_hlat" --ref "lg:$W/lg_hlat" --top dut --workdir "$W/l1" 2>&1)"
rc=$?
if [ $rc -eq 0 ]; then
  # If this ever passes, the gap is CLOSED -- say so loudly rather than silently
  # keeping a stale expectation, and check it proved for the right reason.
  grep -q "PROVEN" <<<"$out" || fail "a latch-in-a-def run exited 0 without a PROVEN verdict"
  echo "NOTE: a latch inside a def now PROVES -- the gap is closed."
  echo "      Drop this case, and re-check whether lhdsuite's minion lec_trust list is still needed."
else
  grep -q "hier-unsupported" <<<"$out" \
    || { echo "$out" | tail -5; fail "a latch inside a def failed for some OTHER reason than the named hierarchy refusal (a crash is not a refusal)"; }
  grep -qE "def .leaf. holds .*latch" <<<"$out" \
    || { echo "$out" | tail -5; fail "the hierarchy refusal does not NAME the def and what is in it -- a formal.lec.trust entry cannot be written without guessing"; }
  echo "ok: a latch inside a def is refused, and the diagnostic names the def and the latch"
fi

# ---- 2. ...and TRUST is the documented escape hatch, so it must still work ---
out="$("$LHD" lec --impl "lg:$W/lg_hlat" --ref "lg:$W/lg_hlat" --top dut --workdir "$W/l2" \
        --set formal.lec.trust=leaf 2>&1)"
[ $? -eq 0 ] \
  || { echo "$out" | tail -6; fail "trusting the latch-holding def did NOT let the design prove -- that is the escape hatch lhdsuite's minion depends on for all 32 of its latch defs"; }
echo "ok: trusting the latch-holding def lets the rest prove"

# ---- 3. CONTRAST: a def with only ordinary posedge state is NOT refused ------
cat > "$W/hflop.v" <<'EOF'
module leaf(input clk, input [7:0] d, output [7:0] o);
  reg [7:0] r;
  always @(posedge clk) r <= d;
  assign o = r;
endmodule

module dut(input clk, input [7:0] d, output [7:0] q);
  wire [7:0] t;
  leaf u(.clk(clk), .d(d), .o(t));
  reg [7:0] f;
  always @(posedge clk) f <= t;
  assign q = f;
endmodule
EOF
build hflop "$W/hflop.v" dut
"$LHD" lec --impl "lg:$W/lg_hflop" --ref "lg:$W/lg_hflop" --top dut --workdir "$W/l3" >"$W/l3.log" 2>&1
[ $? -eq 0 ] \
  || { tail -6 "$W/l3.log"; fail "a def holding ONLY ordinary posedge state was refused -- the hierarchy refusal is supposed to be narrow, not a blanket rejection of hierarchy"; }
echo "ok: a def with only ordinary posedge state is not refused"

# ---- 4. CONTRAST: an instantiated CLOCK-GATE cell is inlined, not refused ----
# Same "latch inside a def" on paper, but a clock-gate cell IS handled: it is
# inlined and folded into the flop's enable (single_edge_icg_test.sh). Without
# this case, someone could "fix" case 1 by refusing more broadly and silently
# break every real design with an ICG.
cat > "$W/hicg.v" <<'EOF'
module prim_clk_gate(input clk_i, input en_i, output clk_o);
  logic en_latch;
  always_latch if (!clk_i) en_latch <= en_i;
  assign clk_o = clk_i & en_latch;
endmodule

module dut(input clk, input en, input [7:0] d, output [7:0] q);
  logic gclk;
  prim_clk_gate u_cg(.clk_i(clk), .en_i(en), .clk_o(gclk));
  reg [7:0] f;
  always @(posedge gclk) f <= d;
  assign q = f;
endmodule
EOF
build hicg "$W/hicg.v" dut
"$LHD" lec --impl "lg:$W/lg_hicg" --ref "lg:$W/lg_hicg" --top dut --workdir "$W/l4" >"$W/l4.log" 2>&1
[ $? -eq 0 ] \
  || { tail -6 "$W/l4.log"; fail "an INSTANTIATED clock-gate cell was refused -- it holds a latch in a def too, but it is meant to be inlined and folded into the flop enable"; }
echo "ok: an instantiated clock-gate cell is still inlined and proven, not refused"

echo "PASS: single_edge_hier_latch_test"
