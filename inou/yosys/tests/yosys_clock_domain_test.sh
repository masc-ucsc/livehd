#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# lgcheck must never PROVE a clock/edge difference.
#
# Its structural and induction engines (stages 1/1n/1r/1p/1m, `sat
# -tempinduct` in stage 2, stage 4) advance every flip-flop once per solver
# step regardless of the clock and edge driving it, so they PROVED a posedge
# register equal to its negedge twin, a register moved to another clock, a
# gated clock equal to the ungated one, and a mixed-edge pipeline equal to its
# edge-swapped twin. lgcheck now runs those engines only when every state
# element of both designs is clocked by the same edge of the same primary input
# (clock traced through buffers/inverters); everything else is decided by the
# clk2fflogic engines (stage 1c, bounded miter).
#
# Verdict discipline: rc 0 = proven, 1 = refuted, 2 = inconclusive.
#   bad       : never proven (the pairs here all refute today; 2 is tolerated)
#   bad_guard : never proven AND the guard must have skipped the clock-blind
#               engines (so the verdict does not depend on their luck)
#   proven    : legit equivalents (mapped netlists with buffered/inverted
#               clocks, QN flops, an ICG vs an enable register) must prove
set -u
W="${TEST_TMPDIR:-/tmp/lgcheck_clock_domain_$$}"
mkdir -p "$W"
LGCHECK="${LGCHECK:-$PWD/inou/yosys/lgcheck}"
YOSYS_ABS="${YOSYS_ABS:-$PWD/inou/yosys/yosys2}"
fail() { echo "FAIL: $*" >&2; exit 1; }

HDR='module dut(input clk, input clk2, input rst, input en, input [1:0] d, output [1:0] q);'

reg1() {  # reg1 <file> <always-head> <body>
  printf '%s\n  reg [1:0] f;\n  always @(%s) %s\n  assign q = f;\nendmodule\n' "$HDR" "$2" "$3" >"$W/$1.v"
}
reg1 pe      'posedge clk'  'f <= f ^ d;'
reg1 ne      'negedge clk'  'f <= f ^ d;'
reg1 pe2     'posedge clk2' 'f <= f ^ d;'
reg1 en_pe   'posedge clk'  'if (en) f <= f ^ d;'
reg1 en_ne   'negedge clk'  'if (en) f <= f ^ d;'
reg1 sr_pe   'posedge clk'  'if (rst) f <= 0; else f <= f ^ d;'
reg1 sr_ne   'negedge clk'  'if (rst) f <= 0; else f <= f ^ d;'
reg1 ar_pe   'posedge clk or posedge rst' 'if (rst) f <= 0; else f <= f ^ d;'
reg1 ar_ne   'negedge clk or posedge rst' 'if (rst) f <= 0; else f <= f ^ d;'
reg1 ar_nrst 'posedge clk or negedge rst' 'if (!rst) f <= 0; else f <= f ^ d;'
reg1 lat_p   '*' 'if (en) f = d;'
reg1 lat_n   '*' 'if (!en) f = d;'

# A second register moved to another clock.
printf '%s\n  reg [1:0] f, g;\n  always @(posedge clk) f <= f ^ d;\n  always @(posedge clk) g <= f + d;\n  assign q = g;\nendmodule\n' "$HDR" >"$W/one_clk.v"
sed 's/always @(posedge clk) g/always @(posedge clk2) g/' "$W/one_clk.v" >"$W/two_clk.v"
# A clock gated through an AND (glitchy, not an ICG).
printf '%s\n  reg [1:0] f;\n  wire g = clk & en;\n  always @(posedge g) f <= f ^ d;\n  assign q = f;\nendmodule\n' "$HDR" >"$W/gated.v"
# A latch-based ICG: equivalent to the enable register in cycle semantics.
printf '%s\n  reg [1:0] f;\n  reg en_l;\n  always @* if (!clk) en_l = en;\n  wire g = clk & en_l;\n  always @(posedge g) f <= f ^ d;\n  assign q = f;\nendmodule\n' "$HDR" >"$W/icg.v"
# Mixed edges: same multiset of (clock, edge) signatures, different timing.
printf '%s\n  reg [1:0] n, p;\n  always @(negedge clk) n <= d;\n  always @(posedge clk) p <= n;\n  assign q = p;\nendmodule\n' "$HDR" >"$W/mix_a.v"
printf '%s\n  reg [1:0] n, p;\n  always @(posedge clk) n <= d;\n  always @(negedge clk) p <= n;\n  assign q = p;\nendmodule\n' "$HDR" >"$W/mix_b.v"
# A memory whose write port changes edge.
printf '%s\n  reg [1:0] m [0:3];\n  reg [1:0] a;\n  always @(posedge clk) a <= a + 1;\n  always @(posedge clk) if (en) m[d] <= d ^ a;\n  assign q = m[a];\nendmodule\n' "$HDR" >"$W/mem_pe.v"
sed 's/always @(posedge clk) if (en)/always @(negedge clk) if (en)/' "$W/mem_pe.v" >"$W/mem_ne.v"

# Mapped netlists: a negedge register as a posedge cell on a buffered inverted
# clock, a QN-style inverted-state register, and an async-reset cell with an
# active-low reset pin on the inverted clock.
cat >"$W/cells_ne.v" <<V
module dffp(input CLK, input D, output reg Q);
  always @(posedge CLK) Q <= D;
endmodule
$HDR
  wire nclk = ~clk;
  wire bclk;
  assign bclk = nclk;
  wire [1:0] f;
  dffp f0(.CLK(bclk), .D(f[0] ^ d[0]), .Q(f[0]));
  dffp f1(.CLK(bclk), .D(f[1] ^ d[1]), .Q(f[1]));
  assign q = f;
endmodule
V
cat >"$W/cells_qn.v" <<V
module dffp(input CLK, input D, output reg Q);
  always @(posedge CLK) Q <= D;
endmodule
$HDR
  wire [1:0] fn;
  wire [1:0] f = ~fn;
  dffp f0(.CLK(clk), .D(~(f[0] ^ d[0])), .Q(fn[0]));
  dffp f1(.CLK(clk), .D(~(f[1] ^ d[1])), .Q(fn[1]));
  assign q = f;
endmodule
V
cat >"$W/cells_ar_ne.v" <<V
module dffr(input CLK, input RN, input D, output reg Q);
  always @(posedge CLK or negedge RN) if (!RN) Q <= 0; else Q <= D;
endmodule
$HDR
  wire [1:0] f;
  wire nclk = ~clk;
  wire rn = ~rst;
  dffr f0(.CLK(nclk), .RN(rn), .D(f[0] ^ d[0]), .Q(f[0]));
  dffr f1(.CLK(nclk), .RN(rn), .D(f[1] ^ d[1]), .Q(f[1]));
  assign q = f;
endmodule
V

LGCHECK_BUDGET="${LGCHECK_BUDGET:-120}"
# name:impl:ref:expect
cases=(
  pe_ne:ne:pe:bad_guard
  pe_clk2:pe2:pe:bad_guard
  dffe_pe_ne:en_ne:en_pe:bad_guard
  sdff_pe_ne:sr_ne:sr_pe:bad_guard
  adff_pe_ne:ar_ne:ar_pe:bad_guard
  latch_pol:lat_n:lat_p:bad_guard
  moved_reg:two_clk:one_clk:bad_guard
  gated:gated:pe:bad_guard
  mixed_edges:mix_b:mix_a:bad_guard
  mem_pe_ne:mem_ne:mem_pe:bad_guard
  async_vs_sync:ar_pe:sr_pe:bad
  arst_pol:ar_nrst:ar_pe:bad
  same:pe:pe:proven
  same_neg:ne:ne:proven
  same_mix:mix_a:mix_a:proven
  same_latch:lat_p:lat_p:proven
  same_mem:mem_pe:mem_pe:proven
  cells_inv_clk:cells_ne:ne:proven
  cells_qn:cells_qn:pe:proven
  cells_arst_inv_clk:cells_ar_ne:ar_ne:proven
  icg_vs_enable:icg:en_pe:proven
)
pids=()
for c in "${cases[@]}"; do
  IFS=: read -r name impl ref _ <<<"$c"
  mkdir -p "$W/$name"
  (cd "$W/$name" && LGCHECK_EQUIV_TIMEOUT="$LGCHECK_BUDGET" "$LGCHECK" \
    --yosys "$YOSYS_ABS" --top dut \
    --reference "$W/$ref.v" --implementation "$W/$impl.v") >"$W/$name.log" 2>&1 &
  pids+=($!)
done

i=0
bad=0
for c in "${cases[@]}"; do
  IFS=: read -r name _ _ want <<<"$c"
  wait "${pids[$i]}"
  rc=$?
  i=$((i + 1))
  ok=0
  case "$want:$rc" in
  bad:1 | bad:2 | proven:0) ok=1 ;;
  bad_guard:1 | bad_guard:2)
    grep -q "clock-blind engines .* skipped" "$W/$name.log" && ok=1
    ;;
  esac
  if [ "$ok" -eq 1 ]; then
    echo "ok: $name rc=$rc ($want)"
  else
    echo "---- $name.log"
    grep -E "clock domains|skipped|Successfully|FAIL|INCONCLUSIVE" "$W/$name.log" | tail -8
    echo "BAD: $name rc=$rc (expected $want)"
    bad=1
  fi
done
[ "$bad" -eq 0 ] || fail "lgcheck clock-domain soundness"
echo "PASS: lgcheck clock-domain soundness"
