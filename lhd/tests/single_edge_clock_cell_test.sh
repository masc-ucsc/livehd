#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# pass.single_edge folds a gate that reaches state as a `Clock_cell` (decision #7).
#
# Pyrope makes its clock gates as Clock_cell nodes: `Clock(clock_pin=, enable=)`,
# a chain of them, the activation gate tolg adds for a stateful call under a
# runtime branch, and a gated `__memory` clock. The fold already read a
# Clock_cell (latch_contract::clock_op_of); what was missing was the TRIGGER
# (it counted only the inline `clk & en` idiom, so the pass reported "skipped"
# and pass.lean then refused the unfolded gate) and the MEMORY scan (it knew only
# the inline idiom, gave a port with no clock pin of its own the reference clock
# instead of the cell's clock, and never folded a gated registered read).
#
# `lhd pass single_edge` folds them by default (`clock_cell_gates`); `lhd lec`
# keeps its own Clock_cell model and is unchanged.
#
# Per design: the pass folds every gate (no Clock_cell is left on a clock pin)
# and the export accepts the result. With LHD_EXTERNAL_SIM=1, iverilog runs the
# SOURCE (cgen emits each Clock_cell as its real enable latch and AND) against
# the FOLDED design cycle by cycle; the folded design compared with an UNGATED
# source must differ, or the comparison could not see a gate.
# Paired control: `--set clock_cell_gates=false` restores the skip.

set -u

LHD="${LHD:-lhd/lhd}"
if [ ! -x "$LHD" ]; then
  if [ -x ./bazel-bin/lhd/lhd ]; then LHD=./bazel-bin/lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi
W="${TEST_TMPDIR:-$(mktemp -d)}"
mkdir -p "$W"
fail() { echo "FAIL: $*"; exit 1; }

# ---- the designs ---------------------------------------------------------------
cat > "$W/chain.prp" <<'EOF'
pub mod chain(clk:Clock, a:U1, b:U1, d:U8) -> (q0:U8@[], q1:U8@[], q2:U8@[]) {
  const c1 = Clock(clock_pin=clk, enable=a != 0)
  const c2 = Clock(clock_pin=c1, enable=(a & b) != 0)
  const c3 = Clock(clock_pin=c1, enable=b != 0)
  reg r0:U8:[clock_pin=c2] = 0
  reg r1:U8:[clock_pin=c3] = 0
  reg r2:U8:[clock_pin=c1] = 0
  r0 = d
  r1 = (d + 1)#[0..<8]
  r2 = (d + 2)#[0..<8]
  q0 = r0
  q1 = r1
  q2 = r2
}
EOF
# One `clock_pin` for the whole memory: it lands on port 0 (the registered read),
# and the write port inherits it, as cgen reads a port with no clock of its own.
cat > "$W/gmem.prp" <<'EOF'
pub mod gmem(clk:Clock, g:Bool, we:Bool, re:Bool, wa:U2, ra:U2, wd:U8) -> (q:U8@[1]) {
  const gclk = Clock(clock_pin=clk, enable=g)
  mut res = __memory(
    addr      = (ra, wa),
    bits      = 8,
    size      = 4,
    din       = (0, wd),
    enable    = (re, we),
    clock_pin = gclk,
    fwd       = false,
    `type`    = 1,
    wensize   = 1,
    rdport    = (1, 0),
  )
  q = res[0]
}
EOF
# A stateful call under a runtime branch: tolg gates the callee's clock.
cat > "$W/act.prp" <<'EOF'
mod inner(a:U8) -> (out:U8@[0]) {
  reg q:U8 = 0
  q = q ^ a
  out = q
}
pub mod act(en:Bool, a:U8) -> (out:U8@[0]) {
  out = 0
  if en {
    out = inner(a)
  }
}
EOF

run_design() { # <name> <expected folded gates>
  local t=$1 gates=$2
  "$LHD" compile "$W/$t.prp" --top "$t" --set compile.flatten=true --emit-dir "lg:$W/${t}_src" \
    --workdir "$W/${t}_cw" > "$W/${t}_compile.log" 2>&1 || { tail -5 "$W/${t}_compile.log"; fail "$t: compile"; }
  "$LHD" pass single_edge -d pass.single_edge --top "$t" "lg:$W/${t}_src" --emit-dir "lg:$W/${t}_norm" \
    --workdir "$W/${t}_pw" > "$W/${t}_pass.log" 2>&1 || { tail -4 "$W/${t}_pass.log"; fail "$t: pass single_edge refused"; }
  grep -q "P=1 slots" "$W/${t}_pass.log" || { grep -o '"message":"[^"]*' "$W/${t}_pass.log" | tail -1; fail "$t: the pass did not run"; }
  grep -q "$gates gated clock(s) folded" "$W/${t}_pass.log" \
    || { grep -o 'P=[0-9][^"]*' "$W/${t}_pass.log" | head -1; fail "$t: expected $gates gate(s) folded"; }
  "$LHD" compile "lg:$W/${t}_norm" --top "$t" --emit-dir "lean:$W/${t}_lean" --set formal.lean.mode=verified_compiler \
    --workdir "$W/${t}_lw" > "$W/${t}_lean.log" 2>&1 \
    || { grep -o '"message":"[^"]*' "$W/${t}_lean.log" | head -2; fail "$t: the export refused the folded design"; }
  for side in src norm; do
    "$LHD" compile "lg:$W/${t}_$side" --top "$t" --emit-dir "verilog:$W/${t}_v$side" --workdir "$W/${t}_vw$side" \
      > "$W/${t}_e$side.log" 2>&1 || { tail -4 "$W/${t}_e$side.log"; fail "$t: verilog emission ($side)"; }
  done
  grep -q "Clock_cell\|clock_cell_[0-9]*__en_latched" "$W/${t}_vsrc"/*.v || fail "$t: the source has no clock gate to fold"
  grep -q "__en_latched" "$W/${t}_vnorm"/*.v && fail "$t: the folded design still has a clock gate"
  echo "ok: $t -- $gates gate(s) folded into enables, and the export accepts it"
}
run_design chain 3
run_design gmem 2
run_design act 1

# ---- the control: the option off restores the skip ----------------------------
"$LHD" pass single_edge -d pass.single_edge --top chain "lg:$W/chain_src" --emit-dir "lg:$W/chain_off" --set clock_cell_gates=false \
  --workdir "$W/chain_off_w" > "$W/chain_off.log" 2>&1 || fail "the option-off run failed"
grep -q "single-edge-skipped" "$W/chain_off.log" || fail "with clock_cell_gates=false the pass still folded"
echo "ok: --set clock_cell_gates=false skips the design, as before"

# ---- the external-simulator leg ----------------------------------------------
if [ -z "${LHD_EXTERNAL_SIM:-}" ]; then
  echo "note: external-simulator leg skipped (set LHD_EXTERNAL_SIM=1)"
  echo "PASS: single_edge_clock_cell_test (structural legs only)"
  exit 0
fi
command -v iverilog >/dev/null 2>&1 && command -v vvp >/dev/null 2>&1 \
  || fail "LHD_EXTERNAL_SIM is set but iverilog/vvp are not on PATH"
RTL_INC="ware/rtl"
[ -d "$RTL_INC" ] || RTL_INC="$(cd "$(dirname "$0")/../.." && pwd)/ware/rtl"
[ -d "$RTL_INC" ] || fail "could not locate ware/rtl for the cgen memory primitives"

# <name> <clock> <inputs: name:width ...> -- <outputs: name:width ...>
# Inputs change at the negedge; outputs are compared just before each posedge,
# from the first cycle after the 2-cycle reset (gates held open during it). A
# memory is filled first: cycles 0-3 write entry k with every gate open.
diff_design() {
  local t=$1 clk=$2; shift 2
  local ins=() outs=()
  while [ "$1" != "--" ]; do ins+=("$1"); shift; done
  shift
  outs=("$@")
  local src norm
  src=$(ls "$W/${t}_vsrc"/*.v); norm=$(ls "$W/${t}_vnorm"/*.v)
  sed -e "s/^module $t(/module ${t}_n(/" -e '/^`include "cgen_memory_/d' "$norm" > "$W/${t}_n.v"
  # The control: the source with every gated clock replaced by the raw clock.
  sed -E "s/always @\(posedge clock_cell_[0-9]+ \)/always @(posedge $clk )/; s/^\.clk\(clock_cell_[0-9]+\)/.clk($clk)/" \
    "$src" | sed "s/^module $t(/module ${t}_u(/" | sed '/^`include "cgen_memory_/d' > "$W/${t}_u.v"
  cmp -s <(sed "s/^module ${t}_u(/module $t(/" "$W/${t}_u.v") <(sed '/^`include "cgen_memory_/d' "$src") \
    && fail "$t: the ungated control was not derived"
  local has_reset=0
  grep -qE '^ *,?input reset' "$src" && has_reset=1
  local decl="" conn="" stim="" cmp_s="" cmp_u=""
  for io in "${ins[@]}"; do
    local n=${io%%:*} w=${io##*:}
    decl+=" reg [$((w - 1)):0] $n = 0;"
    conn+=", .$n($n)"
    case $n in
      we) stim+=" $n = (c < 4) ? 1 : \$random;" ;;
      wa) stim+=" $n = (c < 4) ? c : \$random;" ;;
      g|a|b|en) stim+=" $n = (c < 4) ? 1 : \$random;" ;;
      *) stim+=" $n = \$random;" ;;
    esac
  done
  for io in "${outs[@]}"; do
    local n=${io%%:*} w=${io##*:}
    decl+=" wire [$((w - 1)):0] s_$n, n_$n, u_$n;"
    cmp_s+=" if (s_$n !== n_$n) begin errs = errs + 1; if (errs < 4) \$display(\"MISMATCH $n cycle %0d source=%0d folded=%0d\", c, s_$n, n_$n); end"
    cmp_u+=" if (u_$n !== n_$n) ctl = ctl + 1;"
  done
  local rst_decl="" rst_conn="" rst_set=""
  if [ $has_reset -eq 1 ]; then
    rst_decl=" reg reset = 1;"; rst_conn=", .reset(reset)"; rst_set=" reset = (c < 2);"
  fi
  local port_s="" port_n="" port_u=""
  for io in "${outs[@]}"; do
    local n=${io%%:*}
    port_s+=", .$n(s_$n)"; port_n+=", .$n(n_$n)"; port_u+=", .$n(u_$n)"
  done
  cat > "$W/${t}_tb.v" <<EOF
\`timescale 1ns/1ps
module tb;
  reg $clk = 0;$rst_decl$decl
  integer c, errs = 0, ctl = 0;
  $t     s(.$clk($clk)$rst_conn$conn$port_s);
  ${t}_n n(.$clk($clk)$rst_conn$conn$port_n);
  ${t}_u u(.$clk($clk)$rst_conn$conn$port_u);
  initial begin
    for (c = 0; c < 64; c = c + 1) begin
      #1;$rst_set$stim
      #3;
      if (c >= 5) begin$cmp_s$cmp_u end
      #1 $clk = 1;
      #5 $clk = 0;
    end
    if (errs == 0) \$display("CLOCK_CELL_DIFF_OK ctl=%0d", ctl);
    else           \$display("CLOCK_CELL_DIFF_FAIL errs=%0d", errs);
    \$finish;
  end
endmodule
EOF
  iverilog -g2012 -I "$RTL_INC" -o "$W/${t}.vvp" "$src" "$W/${t}_n.v" "$W/${t}_u.v" "$W/${t}_tb.v" > "$W/${t}_iv.log" 2>&1 \
    || { cat "$W/${t}_iv.log"; fail "$t: iverilog"; }
  local out
  out=$(vvp "$W/${t}.vvp")
  grep -q "CLOCK_CELL_DIFF_OK" <<<"$out" || { echo "$out" | head -5; fail "$t: the folded design does not match the gated source"; }
  local nctl
  nctl=$(sed -n 's/.*CLOCK_CELL_DIFF_OK ctl=\([0-9]*\).*/\1/p' <<<"$out")
  [ "${nctl:-0}" -gt 0 ] || fail "$t: the UNGATED control matched the folded design -- the comparison cannot see a gate"
  echo "ok: $t -- source and folded agree over 59 cycles under iverilog; the ungated control differs on $nctl output-cycle(s)"
}
diff_design chain clk a:1 b:1 d:8 -- q0:8 q1:8 q2:8
diff_design gmem clk g:1 we:1 re:1 wa:2 ra:2 wd:8 -- q:8
diff_design act clock en:1 a:8 -- out:8

echo "PASS: single_edge_clock_cell_test"
