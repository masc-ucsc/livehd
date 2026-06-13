#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Vcd_reader smoke test through the vcd_sample binary: the .vcd input drives
# header commands ($scope/$var/$upscope/$enddefinitions), timestamps and both
# vector (b... id) and scalar (0id/1id) samples. vcd_sample toggles a parity
# bit per value change of every *uart* signal, so the expected parity is
# pinned by the change counts in the input (uart_tx: 3 changes -> true,
# uart_data: 2 changes -> false; other_sig must not be tracked).

set -u

BIN=core/vcd_sample
VCD=core/tests/vcd_sample_signals.vcd

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

OUT="$("$BIN" "$VCD")" || fail "vcd_sample exited non-zero"
echo "$OUT"
echo "$OUT" | grep -q 'top,uart_tx : true' || fail "uart_tx parity wrong (want true after 3 changes)"
echo "$OUT" | grep -q 'top,uart_data : false' || fail "uart_data parity wrong (want false after 2 changes)"
echo "$OUT" | grep -q 'other_sig' && fail "non-uart signal leaked into the parity table"

# A missing file must fail cleanly.
"$BIN" /nonexistent.vcd >/dev/null 2>&1 && fail "missing input did not fail"

echo "PASS vcd_sample_test"
