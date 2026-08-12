#!/usr/bin/env bash
# Independent event-semantics anchor for todo/livehd/3d-sim-color subtask B.
# A transparent-high latch fed by a posedge flop must see the flop's NBA update
# while the latch remains open. The retired module scheduler samples pre-commit
# Q in this shape; the occurrence-wide schedule deliberately does not preserve
# that stale behavior.

set -euo pipefail

LHD="${LHD:-lhd/lhd}"
SIM_SRC="${SIM_SRC:-inou/prp/tests/sim/flop_feeds_transparent_high_latch.prp}"
oracle_tmp="$(mktemp -d "${TMPDIR:-/tmp}/lhd-sim-color-latch.XXXXXX")"
trap 'rm -rf "$oracle_tmp"' EXIT

# The product regression must select and execute the replacement path; a green
# assertion suite on the retired module scheduler is not evidence for this TODO.
"$LHD" sim "$SIM_SRC" --setup-only --workdir "$oracle_tmp/setup" -q >/dev/null
color_header="$(ls "$oracle_tmp"/setup/sim/*flop_high_latch.hpp | head -1)"
grep -q 'color-direct eligible=true' "$color_header" \
  || { echo "FAIL: flop->transparent-high latch did not select the color-direct schedule"; exit 1; }
"$LHD" sim "$SIM_SRC" --workdir "$oracle_tmp/run" -q >/dev/null

if ! command -v iverilog >/dev/null 2>&1 || ! command -v vvp >/dev/null 2>&1; then
  echo "PASS: color-direct latch regression (independent iverilog/vvp oracle skipped: tools not found)"
  exit 0
fi

cat > "$oracle_tmp/oracle.sv" <<'EOF'
`timescale 1ns/1ps
module oracle;
  reg clk = 0;
  reg [7:0] d = 0;
  reg [7:0] qf = 0;
  reg [7:0] qh = 0;
  reg [7:0] stale = 0;
  reg [7:0] previous = 0;
  integer k;
  integer errors = 0;

  always @(posedge clk) qf <= d;
  always_latch if (clk) qh = qf;

  // Negative control: this is the pre-commit sampling mistake. It executes in
  // the active region at the same rise as qf and therefore keeps old qf.
  always @(posedge clk) stale <= qf;

  initial begin
    for (k = 0; k < 4; k = k + 1) begin
      d = 11 + 17*k;
      #5 clk = 1;
      #1;
      if (qf !== d || qh !== d) begin
        $display("EVENT_MISMATCH k=%0d d=%0d qf=%0d qh=%0d", k, d, qf, qh);
        errors = errors + 1;
      end
      if (stale !== previous) begin
        $display("CONTROL_MISMATCH k=%0d previous=%0d stale=%0d", k, previous, stale);
        errors = errors + 1;
      end
      if (stale === qh) begin
        $display("CONTROL_BLIND k=%0d stale=%0d qh=%0d", k, stale, qh);
        errors = errors + 1;
      end
      previous = d;
      #4 clk = 0;
      #5;
    end
    if (errors == 0) $display("SIM_COLOR_LATCH_EVENT_OK");
    else $display("SIM_COLOR_LATCH_EVENT_FAIL errors=%0d", errors);
    $finish;
  end
endmodule
EOF

iverilog -g2012 -o "$oracle_tmp/oracle.vvp" "$oracle_tmp/oracle.sv"
oracle_out="$(vvp "$oracle_tmp/oracle.vvp" 2>&1)"
if ! grep -q "SIM_COLOR_LATCH_EVENT_OK" <<<"$oracle_out"; then
  echo "$oracle_out"
  exit 1
fi
echo "PASS: color-direct transparent-high latch reads the flop's post-rise value; Icarus pre-commit control differs"
