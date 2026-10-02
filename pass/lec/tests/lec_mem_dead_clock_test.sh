#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# REGRESSION: a memory port whose clock is a CONSTANT never ticks -- the
# encoder must not commit it on every step, whether the design is multi-clock,
# single-clock, or under the single-root formal phase schedule.
#
# memory_clocks() skips a tied-off clock lane (the census never counts it as a
# clock), but the encoder then left that port's `port_edge` null and
# `with_edge()` fell back to the bare gate: the dead-clock write port committed
# on EVERY step. A yosys-read memory with `always_ff @(posedge dead)` writes was
# REFUTED against the same memory without that port (the write landed on a step
# no clock ticked), and PROVEN against the same memory with that port on the
# live clock. The port now never commits; with a LIVE enable the encoder
# refuses (UNKNOWN, rc 7) rather than guess the clock the author meant.
#
# The first fix only covered the MULTI-CLOCK encoder. Because the census leaves
# a tied-off clock out, a design whose only other clock is clk_a is SINGLE-clock
# and skipped the rule (false PROVEN vs `live`), and a single-clock design with
# a negedge flop takes the phase schedule, which handed the dead port sink 0's
# schedule (also false PROVEN). Three shapes cover the three encoder paths:
#   multi  -- a clk_b flop makes the design multi-clock
#   single -- clk_a is the only live clock
#   neg    -- clk_a only, plus a negedge flop (phase-inductive, single root)
#
# The native slang front end refuses a constant clock (`clock-const`), so the
# live-enable case is only reachable through a yosys reader's lg: library. The
# idle-port outcome (constant clock, enable held at 0 -> never commits, PROVEN)
# is the inou/prp/tests/equiv/lec/mem_dead_clk_idle.sv fixture group.

set -u

LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then
    LHD=./lhd/lhd
  else
    echo "FAIL: could not find the lhd binary in $(pwd)"
    exit 1
  fi
fi

W="${TEST_TMPDIR:-/tmp/lec_mem_dead_clock}"
rm -rf "$W"
mkdir -p "$W"
FAILED=0
fail() { echo "FAIL: $*"; FAILED=1; }

# <kind>_<shape>.sv: kind dead = port 1 on a constant clock, nop = no port 1,
# live = port 1 on clk_a.
for shape in multi single neg; do
  case $shape in
    multi)
      extra_io=', input logic clk_b, output logic [3:0] q'
      extra='  always_ff @(posedge clk_b) q <= wd1;'
      ;;
    single)
      extra_io=''
      extra=''
      ;;
    neg)
      extra_io=', output logic [3:0] n'
      extra='  always_ff @(negedge clk_a) n <= wd0;'
      ;;
  esac
  for kind in dead nop live; do
    case $kind in
      dead) port1="  logic dead;
  assign dead = 1'b0;
  always_ff @(posedge dead) if (we1) mem[wa1] <= wd1;" ;;
      nop) port1='' ;;
      live) port1='  always_ff @(posedge clk_a) if (we1) mem[wa1] <= wd1;' ;;
    esac
    cat >"$W/${kind}_$shape.sv" <<EOF
module top(input logic clk_a, input logic we0, input logic we1,
           input logic [1:0] wa0, input logic [1:0] wa1, input logic [3:0] wd0,
           input logic [3:0] wd1, input logic [1:0] ra, output logic [3:0] o$extra_io);
  logic [3:0] mem [4];
  always_ff @(posedge clk_a) if (we0) mem[wa0] <= wd0;
$port1
  assign o = mem[ra];
$extra
endmodule
EOF
  done
done

for shape in multi single neg; do
  for reader in yosys-verilog yosys-slang; do
    lg="$W/lg_${shape}_$reader"
    "$LHD" compile "$W/dead_$shape.sv" --reader "$reader" --top top --emit-dir "lg:$lg" \
      --workdir "$W/wc_${shape}_$reader" --set lhd.incremental=false >"$W/compile_${shape}_$reader.log" 2>&1 || {
      tail -5 "$W/compile_${shape}_$reader.log"
      fail "$reader could not read dead_$shape.sv"
      continue
    }
    for other in nop live; do
      tag="$shape/$reader vs $other"
      out=$("$LHD" lec --ref "lg:$lg" --impl "verilog:$W/${other}_$shape.sv" --ref-top top --impl-top top \
        --workdir "$W/wl_${shape}_${reader}_$other" --set lhd.incremental=false --set formal.timeout=20 2>&1)
      rc=$?
      verdict=$(echo "$out" | grep -E "^lec: 'top' " | tail -1)
      if echo "$verdict" | grep -q "REFUTED"; then
        fail "$tag: a constant-clocked write port committed (REFUTED): $verdict"
      elif echo "$verdict" | grep -q "PROVEN"; then
        fail "$tag: a constant-clocked write port committed (PROVEN against $other): $verdict"
      elif [ $rc -ne 7 ] || ! echo "$verdict" | grep -q "UNKNOWN"; then
        fail "$tag: expected a sound refusal (UNKNOWN, rc 7), got rc=$rc: $verdict"
      elif [ "$shape" = neg ] && echo "$out" | grep -q "phase schedule refused"; then
        # yosys-verilog's lg hands the phase schedule the constant clock as a
        # second ROOT: it refuses before the encoder sees the memory (sound).
        echo "ok: $tag: refused by the phase schedule (constant clock root)"
      elif ! echo "$out" | grep -q "clocked by a CONSTANT"; then
        fail "$tag: UNKNOWN for another reason than the dead-clock refusal: $verdict"
      else
        echo "ok: $tag: dead-clock write port with a live enable is refused"
      fi
    done
  done
done

if [ $FAILED -ne 0 ]; then
  exit 1
fi
echo "PASS: lec_mem_dead_clock_test"
