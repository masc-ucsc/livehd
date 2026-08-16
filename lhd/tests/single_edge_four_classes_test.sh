#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# todo/livehd/2f-latch M8 steps 3b + 3g — ALL FOUR COMMIT CLASSES IN ONE MODULE,
# validated against ICARUS VERILOG.
#
# Before this test no design in the tree held more than two of the four commit
# classes, so a slot table that quietly COLLAPSED two of them onto one slot
# passed everything:
#
#   posedge flop            -> (clk, RISE)   slot 0
#   negedge flop            -> (clk, FALL)   slot 1
#   transparent-LOW  latch  -> (clk, RISE)   slot 0   (open while clk==0, so it
#                                                      COMMITS at the rise)
#   transparent-HIGH latch  -> (clk, FALL)   slot 1   (open while clk==1, so it
#                                                      COMMITS at the fall)
#
# Note the pairing: a transparent-LOW latch commits on the SAME edge as a
# POSEDGE flop, and a transparent-HIGH latch on the same edge as a NEGEDGE one.
# That is the whole reason a slot is a function of the (net, edge) commit class
# rather than of "is it a latch". Fold the polarity backwards and the two
# latches swap slots — a persistent full-cycle error that looks like working
# hardware and that a symmetric before/after gate cannot see.
#
# TWO independent things are asserted.
#
# (3b) TRACE-LEVEL VALIDATION OF THE TRANSFORMATION ITSELF, against iverilog.
#      Edge normalization is NOT cycle-preserving — one source cycle becomes P
#      sub-steps — so no cycle-accurate equivalence checker can validate it,
#      `lhd lec` and lgcheck included. What can: run the SOURCE Verilog (real
#      `always_latch`, real `negedge`) and the NORMALIZED Verilog (posedge flops
#      plus a phase divider) side by side under Icarus, the normalized side
#      clocked P times per source period, and compare at PERIOD BOUNDARIES.
#      This is also the task's binding cross-model gating rule: "PROVEN before
#      AND after" cannot see a transformation that is wrong but applied
#      identically to both sides, so anything edge-shaped is gated against
#      iverilog, never against our own encoder on both sides.
#
#      Sampling obeys the observation-visibility rule, which is not optional
#      here: a real latch reads THROUGH while its window is open, so each latch
#      Q is sampled only in the half-period where that latch is OPAQUE. The two
#      latches are complementary, so no single instant works for both — which is
#      exactly why the rule exists rather than "just sample at the end".
#
# (3g) DISCRIMINATION, via `lhd lec`: collapsing any two classes must REFUTE,
#      and a semantically-identical rewrite must still PROVE (without that
#      second half, three refutations are also what a normalizer that mangles
#      every design produces).
#
# The design is written in PYROPE with explicit `clock_pin=ref clk`, which is
# the only way to get a genuinely clock-GATED latch whose gate net is the same
# net the flops run on. Driving it through the yosys reader instead does produce
# the four classes, but that reader names registers `$procdff$32` / `n12` with
# numbering that shifts between two variants of the same design, so the state
# pairing cannot match them and every LEC comes back as a per-side free-constant
# CEX — a property of the reader, not of the slot table.

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

# ---- the four-class design --------------------------------------------------
gen() { # <file> <transparent-low-gate> <negedge-flop-attrs> <transparent-high-gate>
  cat > "$1" <<EOF
pub mod four8(clk:bool, d:u8) -> (qp:u8@[1], qn:u8@[1], qh:u8@[0], ql:u8@[0]) {
  reg rp:u8:[clock_pin=ref clk] = 0
  reg rn:u8:[clock_pin=ref clk$3] = 0
  reg lh:u8:[latch=true]
  reg ll:u8:[latch=true]
  qp = rp
  qn = rn
  qh = lh
  ql = ll
  rp = d
  rn = d
  if $4 { lh = d }
  if $2 { ll = d }
}
EOF
}

#   base:    ll on !clk (transparent LOW), rn negedge, lh on clk (transparent HIGH)
gen "$W/four.prp"   '!clk' ', posclk=false' 'clk'
#   m_latch: the two LATCH classes collapsed (transparent-low written high)
gen "$W/m_latch.prp" 'clk'  ', posclk=false' 'clk'
#   m_flop:  the two FLOP classes collapsed (negedge written posedge)
gen "$W/m_flop.prp"  '!clk' ''               'clk'
#   m_swap:  the transparent-HIGH latch moved onto the other latch's phase
gen "$W/m_swap.prp"  '!clk' ', posclk=false' '!clk'

# An equivalent rewrite: structurally different (so the structural-identity
# shortcut cannot answer it), semantically identical. The VACUITY GUARD.
sed 's/^  rp = d$/  rp = (d + 7) - 7/' "$W/four.prp" > "$W/four_equiv.prp"

build() { # <name>
  rm -rf "$W/lg_$1"
  "$LHD" compile "$W/$1.prp" --top four8 --emit-dir "lg:$W/lg_$1" --workdir "$W/cw_$1" \
    >"$W/c_$1.log" 2>&1 || { tail -5 "$W/c_$1.log"; fail "compile of $1.prp failed"; }
}
for m in four four_equiv m_latch m_flop m_swap; do build "$m"; done

# ---- 3g: discrimination through lhd lec -------------------------------------
lec_verdict() { # <impl> <ref> <tag> [extra args...]
  local impl=$1 ref=$2 tag=$3
  shift 3
  local out
  out="$("$LHD" lec --impl "lg:$W/lg_$impl" --ref "lg:$W/lg_$ref" --top four8 \
          --workdir "$W/lw_$tag" "$@" 2>&1)"
  echo "$out" > "$W/lec_$tag.log"
  grep -oE "'[^']*' (PROVEN|REFUTED|UNKNOWN)" <<<"$out" | grep -oE "PROVEN|REFUTED|UNKNOWN" | head -1
}

v=$(lec_verdict four_equiv four eq)
[ "$v" = "PROVEN" ] \
  || { tail -6 "$W/lec_eq.log"; fail "the four-class design is not equivalent to its own semantically-identical rewrite (got $v) — the normalizer is mangling it, so every refutation below is vacuous"; }
grep -q "pass.single_edge slots:2" "$W/lec_eq.log" \
  || { grep -o "pass.single_edge[^\"]*" "$W/lec_eq.log" | head -2; fail "the four-class design did not normalize into a 2-slot time base"; }
echo "ok: four commit classes coexist at P=2, and an equivalent rewrite PROVES"

for m in "m_latch|the two LATCH classes collapsed (transparent-low written as transparent-high)" \
         "m_flop|the two FLOP classes collapsed (negedge written as posedge)" \
         "m_swap|a latch moved onto the other latch's phase (both open at once)"; do
  name=${m%%|*}
  desc=${m#*|}
  v=$(lec_verdict "$name" four "$name")
  [ "$v" = "REFUTED" ] \
    || { tail -6 "$W/lec_$name.log"; fail "$desc came back $v, not REFUTED — the slot table collapses two commit classes"; }
  echo "ok: $desc is REFUTED"
done

# ---- 3b: trace-level validation against iverilog ----------------------------
if ! command -v iverilog >/dev/null 2>&1 || ! command -v vvp >/dev/null 2>&1; then
  echo "note: iverilog/vvp not found — the INDEPENDENT trace validation was SKIPPED."
  echo "      Everything above compares our own encoder against itself, which by"
  echo "      the cross-model gating rule cannot validate the transformation."
  echo "PASS: single_edge_four_classes_test (lec legs only)"
  exit 0
fi

rm -rf "$W/lg_four_n"
"$LHD" pass single_edge --top four8 "lg:$W/lg_four" --emit-dir "lg:$W/lg_four_n" \
  --workdir "$W/pw" >"$W/pass.log" 2>&1 || { tail -5 "$W/pass.log"; fail "pass single_edge failed"; }
grep -q "single-edge-applied" "$W/pass.log" || { tail -5 "$W/pass.log"; fail "the pass did not fire on the four-class design"; }

emit() { # <lgdir> <outdir>
  rm -rf "$2"
  "$LHD" compile "lg:$1" --top four8 --emit-dir "verilog:$2" --workdir "$W/vw_$(basename "$2")" \
    >"$W/e_$(basename "$2").log" 2>&1 \
    || { tail -5 "$W/e_$(basename "$2").log"; fail "verilog emission from $1 failed"; }
}
emit "$W/lg_four"   "$W/v_src"
emit "$W/lg_four_n" "$W/v_norm"

cat "$W"/v_src/*.v > "$W/src.v"
# Rename the normalized copy so both can be instantiated in one testbench.
sed 's/^module four8(/module four8_n(/' "$W"/v_norm/*.v > "$W/norm.v"

grep -q "always_latch" "$W/src.v" || fail "the SOURCE emission lost its always_latch — nothing independent is being compared"
grep -q "negedge"      "$W/src.v" || fail "the SOURCE emission lost its negedge"
grep -q "always_latch" "$W/norm.v" && fail "the NORMALIZED emission still holds a latch (a partial lowering)"
grep -q "negedge"      "$W/norm.v" && fail "the NORMALIZED emission still holds a negedge flop (a partial lowering)"

cat > "$W/tb.v" <<'EOF'
`timescale 1ns/1ps
// HALF = the normalized clock's half-period. 5 gives it TWO posedges per source
// period (P=2, correct); 10 gives it one, which is the P=1 time base and is the
// negative control below.
`ifndef HALF
`define HALF 5
`endif
module tb;
  reg clk_s = 0, clk_n = 0, reset = 1;
  reg [7:0] d = 0;
  wire [7:0] sp, sn, sh, sl, np, nn, nh, nl;
  integer errs = 0, p;

  always #10      clk_s = ~clk_s;   // source:     rise 10+20p, fall 20+20p
  always #(`HALF) clk_n = ~clk_n;   // normalized: slot0 15+20p, slot1 25+20p

  four8   src(.clk(clk_s), .d(d), .qp(sp), .qn(sn), .qh(sh), .ql(sl), .reset(reset));
  four8_n nrm(.clk(clk_n), .d(d), .qp(np), .qn(nn), .qh(nh), .ql(nl), .reset(reset));

  task chk(input [63:0] nm, input [7:0] a, input [7:0] b);
    if (a !== b) begin
      errs = errs + 1;
      if (errs < 6) $display("MISMATCH %0s @%0t source=%0d normalized=%0d", nm, $time, a, b);
    end
  endtask

  initial begin
    // Stimulus changes at 6+20p, so it is STABLE across every sampling edge of
    // the period on both sides (source rise 10+20p and fall 20+20p; normalized
    // slot0 15+20p and slot1 25+20p). Without that the two sides legitimately
    // sample different inputs and the comparison is meaningless.
    reset = 1; d = 0;
    #26 d = 8'd37;   // t=26: period 1's stimulus, before the source rise at 30
    #1  reset = 0;   // t=27: after EVERY period-0 edge on both sides, so the
                     //       phase divider starts period 1 at slot 0
    #11;             // t=38 = 18+20*1
    for (p = 1; p < 40; p = p + 1) begin
      // Observation-visibility rule: sample each latch only where it is OPAQUE.
      // clk_s is HIGH here, so the transparent-LOW latch is closed.
      chk("ql", sl, nl);
      chk("qp", sp, np);
      #8  d = (p * 8'd37 + 8'd11) & 8'hff;   // t=26+20p
      #2  chk("qh", sh, nh);                 // t=28+20p: clk_s LOW, so the
          chk("qn", sn, nn);                 //   transparent-HIGH latch is closed
      #10;                                   // t=38+20p
    end
    if (errs == 0) $display("SINGLE_EDGE_DIFF_OK");
    else           $display("SINGLE_EDGE_DIFF_FAIL errs=%0d", errs);
    $finish;
  end
endmodule
EOF

run_iv() { # <label> <extra iverilog args...>
  local label=$1
  shift
  iverilog -g2012 "$@" -o "$W/$label.vvp" "$W/src.v" "$W/norm.v" "$W/tb.v" >"$W/iv_$label.log" 2>&1 \
    || { cat "$W/iv_$label.log"; fail "iverilog failed to elaborate ($label)"; }
  vvp "$W/$label.vvp" >"$W/vvp_$label.log" 2>&1
  cat "$W/vvp_$label.log"
}

out="$(run_iv p2)"
grep -q "SINGLE_EDGE_DIFF_OK" <<<"$out" \
  || { echo "$out" | head -8; fail "the NORMALIZED design does not match the real always_latch/negedge source under iverilog — edge normalization is not semantics-preserving"; }
echo "ok: source vs normalized agree over 39 periods under iverilog (independent oracle)"

# Negative control 1: run the normalized side in the P=1 time base. It must FAIL,
# or the comparison above proves nothing about the phase divider.
out="$(run_iv p1 -DHALF=10)"
grep -q "SINGLE_EDGE_DIFF_FAIL" <<<"$out" \
  || fail "the differential harness still passed with the normalized side clocked at 1x — it cannot detect a wrong time base"
echo "ok: forcing the normalized side to a P=1 time base FAILS (the harness discriminates)"

# Negative control 2: swap the two slot predicates in the normalized Verilog. A
# slot table that put the transparent-low latch with the negedge flop (and vice
# versa) would produce exactly this netlist, so the harness must reject it.
mapfile -t slot_predicates < <(
  grep -oE '^if \([A-Za-z_][A-Za-z0-9_]*\) begin' "$W/norm.v" \
    | sed -E 's/^if \(([^)]*)\) begin$/\1/' \
    | awk '!seen[$0]++ { print; if (++count == 2) exit }'
)
slot0=${slot_predicates[0]:-}
slot1=${slot_predicates[1]:-}
[ -n "$slot0" ] && [ -n "$slot1" ] || fail "could not find the two slot predicates in the normalized Verilog"
sed "s/^if ($slot0) begin/if (__T__) begin/; s/^if ($slot1) begin/if ($slot0) begin/; s/^if (__T__) begin/if ($slot1) begin/" \
  "$W/norm.v" > "$W/norm_swap.v"
cp "$W/norm.v" "$W/norm.v.keep"; cp "$W/norm_swap.v" "$W/norm.v"
out="$(run_iv swap)"
cp "$W/norm.v.keep" "$W/norm.v"
grep -q "SINGLE_EDGE_DIFF_FAIL" <<<"$out" \
  || fail "swapping the two slot predicates still passed — the harness cannot see a collapsed/inverted slot assignment"
echo "ok: swapping the two slot predicates FAILS (the slot assignment is load-bearing)"

echo "PASS: single_edge_four_classes_test"
