#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A DATA-gated latch whose window still reads a CLOCK is refused
# (`latch-window-reads-clock`).
#
# pass.single_edge keeps a data-gated latch's enable through the rewrite and
# evaluates it on every sub-step. Every window shape the classifier understands
# -- a clock phase, a gate cone -- is filed as Clock-role first, so a data-gated
# window that still reaches a clock input reads it through logic the pass could
# not resolve; lowering it would read the clock AS DATA. The case that exposed
# it: lhd_abc_latch_test_usyn's mapped `abc_latch_gated`, whose gate is an AND2
# cell under a wrapper module. The netlist side normalized that way and was
# REFUTED against its correctly normalized RTL; refused, `lhd lec` declines edge
# normalization on both sides and proves through the phase schedule.
#
#   wrc   the gate inside a wrapper that holds a nested instance (not inlined
#         by the pass's gate-cell pre-step): REFUSED, naming the clock
#   wrci  the same gate written in the top (abc_latch_gated's RTL), so the gate
#         cone is visible: ACCEPTED, the gate folded -- the refusal is about
#         opacity. (The gate-cell pre-step does not help wrc: it inlines a cell
#         that drives a flop or memory CLOCK pin, and a latch's gate is its
#         enable.)
#   wrs   a window from a STATEFUL instance on the same clock: a value, not
#         the clock -- ACCEPTED

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

cat > "$W/wrc.sv" <<'EOF'
module and2c(input A, input B, output Y);
  assign Y = A & B;
endmodule
module gate_w(input a, input b, output y);
  and2c c(.A(a), .B(b), .Y(y));
endmodule
module wrc(input clk, input en, input [3:0] d, output [3:0] q);
  logic en_l;
  always_latch if (!clk) en_l = en;
  wire gclk;
  gate_w u(.a(clk), .b(en_l), .y(gclk));
  logic [3:0] g;
  always_latch if (gclk) g = d;
  assign q = g;
endmodule
EOF
# The same gate written in the top: the gate cone is visible to the classifier.
sed -e 's/^  gate_w u(.a(clk), .b(en_l), .y(gclk));/  assign gclk = clk \& en_l;/' -e 's/^module wrc(/module wrci(/' "$W/wrc.sv" > "$W/wrci.sv"
grep -q 'assign gclk = clk & en_l;' "$W/wrci.sv" || fail "the visible-gate variant did not apply"

cat > "$W/wrs.sv" <<'EOF'
module reg_w(input clk, input a, output logic y);
  always_ff @(posedge clk) y <= a;
endmodule
module wrs(input clk, input en, input [3:0] d, output [3:0] q);
  wire w;
  reg_w u(.clk(clk), .a(en), .y(w));
  logic [3:0] g;
  always_latch if (w) g = d;
  assign q = g;
endmodule
EOF

build() { # <name>
  rm -rf "$W/lg_$1"
  "$LHD" compile "$W/$1.sv" --reader slang --top "$1" --emit-dir "lg:$W/lg_$1" --workdir "$W/cw_$1" \
    >"$W/c_$1.log" 2>&1 || { grep -o '"message":"[^"]*' "$W/c_$1.log" | head -3; fail "compile of $1.sv failed"; }
}
for m in wrc wrci wrs; do build "$m"; done

normalize() { # <name>  -> pass output on stdout, rc as the pass's
  rm -rf "$W/lgn_$1"
  "$LHD" pass single_edge --top "$1" "lg:$W/lg_$1" --emit-dir "lg:$W/lgn_$1" --workdir "$W/pw_$1" 2>&1
}

out="$(normalize wrc)"
[ $? -ne 0 ] || { echo "$out" | tail -3; fail "a latch gated through an opaque instance was ACCEPTED (its enable would read clk as data)"; }
grep -q '"code":"latch-window-reads-clock"' <<<"$out" \
  || { echo "$out" | grep -o '"message":"[^"]*' | head -2; fail "the opaque gate was refused for the wrong reason"; }
grep -q 'reads clock `clk`' <<<"$out" || fail "the refusal does not name the clock it reads"
echo "ok: a latch window that reads clk through an opaque instance is refused (latch-window-reads-clock)"

out="$(normalize wrci)"
[ $? -eq 0 ] || { echo "$out" | grep -o '"message":"[^"]*' | head -2; fail "the same gate, written in the top, was refused"; }
grep -q 'latch-window-reads-clock' <<<"$out" && fail "the visible gate tripped the clock-read refusal"
grep -qE '[1-9][0-9]* gated clock\(s\) folded' <<<"$out" || { echo "$out" | tail -2; fail "the visible gate was not folded"; }
echo "ok: the same gate written in the top is folded (the refusal is about opacity)"

out="$(normalize wrs)"
[ $? -eq 0 ] || { echo "$out" | grep -o '"message":"[^"]*' | head -2; fail "a window driven by a STATEFUL instance was refused"; }
grep -q 'latch-window-reads-clock' <<<"$out" && fail "a stateful instance's output was taken for a clock read"
echo "ok: a window driven by a stateful instance on the same clock is a value, not the clock (accepted)"

echo "PASS: single_edge_opaque_gate_test"
