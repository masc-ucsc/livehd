#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# REGRESSION: in a MULTI-CLOCK design, a memory port whose clock is a CONSTANT
# never ticks -- the encoder must not commit it on every step.
#
# memory_clocks() skips a tied-off clock lane (the census never counts it as a
# clock), but the encoder then left that port's `port_edge` null and
# `with_edge()` fell back to the bare gate: the dead-clock write port committed
# on EVERY step. A yosys-read memory with `always_ff @(posedge dead)` writes was
# REFUTED against the same memory without that port (the write landed on a step
# no clock ticked). The port now never commits; with a LIVE enable the encoder
# refuses (UNKNOWN, rc 7) rather than guess the clock the author meant.
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

# Port 1 of `mem` is clocked by a constant; clk_b's flop makes it multi-clock.
cat > "$W/dead.sv" <<'EOF'
module top(input logic clk_a, input logic clk_b, input logic we0, input logic we1,
           input logic [1:0] wa0, input logic [1:0] wa1, input logic [3:0] wd0,
           input logic [3:0] wd1, input logic [1:0] ra, output logic [3:0] o, output logic [3:0] q);
  logic [3:0] mem [4];
  logic dead;
  assign dead = 1'b0;
  always_ff @(posedge clk_a) if (we0) mem[wa0] <= wd0;
  always_ff @(posedge dead) if (we1) mem[wa1] <= wd1;
  assign o = mem[ra];
  always_ff @(posedge clk_b) q <= wd1;
endmodule
EOF
# The same machine without the dead port.
cat > "$W/nop.sv" <<'EOF'
module top(input logic clk_a, input logic clk_b, input logic we0, input logic we1,
           input logic [1:0] wa0, input logic [1:0] wa1, input logic [3:0] wd0,
           input logic [3:0] wd1, input logic [1:0] ra, output logic [3:0] o, output logic [3:0] q);
  logic [3:0] mem [4];
  always_ff @(posedge clk_a) if (we0) mem[wa0] <= wd0;
  assign o = mem[ra];
  always_ff @(posedge clk_b) q <= wd1;
endmodule
EOF

for reader in yosys-verilog yosys-slang; do
  "$LHD" compile "$W/dead.sv" --reader "$reader" --top top --emit-dir "lg:$W/lg_$reader" \
    --workdir "$W/wc_$reader" --set lhd.incremental=false >"$W/compile_$reader.log" 2>&1 || {
    tail -5 "$W/compile_$reader.log"
    fail "$reader could not read dead.sv"
    continue
  }
  out=$("$LHD" lec --ref "lg:$W/lg_$reader" --impl "verilog:$W/nop.sv" --ref-top top --impl-top top \
    --workdir "$W/wl_$reader" --set lhd.incremental=false --set formal.timeout=20 2>&1)
  rc=$?
  verdict=$(echo "$out" | grep -E "^lec: 'top' " | tail -1)
  if echo "$verdict" | grep -q "REFUTED"; then
    fail "$reader: a constant-clocked write port committed every step (REFUTED): $verdict"
  elif [ $rc -ne 7 ] || ! echo "$verdict" | grep -q "UNKNOWN"; then
    fail "$reader: expected a sound refusal (UNKNOWN, rc 7), got rc=$rc: $verdict"
  elif ! echo "$out" | grep -q "clocked by a CONSTANT"; then
    fail "$reader: UNKNOWN for another reason than the dead-clock refusal: $verdict"
  else
    echo "ok: $reader dead-clock write port with a live enable is refused"
  fi
done

if [ $FAILED -ne 0 ]; then
  exit 1
fi
echo "PASS: lec_mem_dead_clock_test"
