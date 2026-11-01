#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A flop with BOTH an initializer and a reset (`reg q = 5;` plus an async reset
# to 9) has two values in the source, but an LGraph Flop has ONE value pin,
# `initial`: with a reset pin it is the reset value, and every consumer (lhd
# sim, `lhd lec`, the Lean exporter) also starts the flop at it. The Yosys
# readers therefore keep 9 and drop 5. That used to happen silently; the reader
# now WARNS. The flop is still read, and a reset still wins.
#
# Each warning case is paired with a case that must stay quiet, or a reader that
# warned on every initialized flop would pass:
#   * async reset to 9, initializer 5          -> warning, reset value 9 kept
#   * async reset to 0, initializer 5          -> warning (a zero reset is still a value)
#   * async reset to 9, initializer 9          -> quiet (nothing is lost)
#   * async reset to 9, initializer 4'b10xx    -> quiet (an x bit is "any value")
#   * sync reset to 9, initializer 5           -> quiet: the reset stays a data mux
#                                                 and the flop keeps 5 as its start value
set -u
W="${TEST_TMPDIR:-/tmp/yosys_init_reset_warning_$$}"
mkdir -p "$W"
LHD="${LHD:-$PWD/lhd/lhd}"
[[ -x "$LHD" ]] || LHD="$PWD/bazel-bin/lhd/lhd"
fail() { echo "FAIL: $*" >&2; exit 1; }

design() {  # <name> <initializer> <reset value> <edge list>
  cat >"$W/$1.v" <<V
module $1(input clk, input rst, input [3:0] d, output [3:0] o);
  reg [3:0] q = $2;
  always @($4) if (rst) q <= $3; else q <= d;
  assign o = q;
endmodule
V
}
design async_diff "4'd5" "4'd9" "posedge clk or posedge rst"
design async_zero "4'd5" "4'd0" "posedge clk or posedge rst"
design async_same "4'd9" "4'd9" "posedge clk or posedge rst"
design async_xbits "4'b10xx" "4'd9" "posedge clk or posedge rst"
design sync_diff "4'd5" "4'd9" "posedge clk"

compile() {  # <name> <reader>
  "$LHD" compile "$W/$1.v" --reader "$2" --top "$1" --workdir "$W/w_$1_$2" \
    --emit-dir "verilog:$W/out_$1_$2" >"$W/$1_$2.log" 2>&1 \
    || { tail -5 "$W/$1_$2.log" >&2; fail "$1 ($2): compile failed"; }
}

for reader in yosys-slang yosys-verilog; do
  for t in async_diff async_zero async_same async_xbits sync_diff; do
    compile "$t" "$reader"
  done

  grep -q "flop '\\\\q' has an initializer (4'b0101) that differs from its reset value (4'b1001)" "$W/async_diff_$reader.log" \
    || { grep -i warn "$W/async_diff_$reader.log" >&2; fail "async_diff ($reader): the dropped initializer was not reported"; }
  grep -q "initializer is DROPPED" "$W/async_diff_$reader.log" \
    || fail "async_diff ($reader): the warning does not say the initializer is dropped"
  grep -qE "q <= \([0-9]+'sh9\)" "$W"/out_async_diff_$reader/*.v \
    || { cat "$W"/out_async_diff_$reader/*.v >&2; fail "async_diff ($reader): the reset value 9 is no longer the flop's value"; }

  grep -q "differs from its reset value (4'b0000)" "$W/async_zero_$reader.log" \
    || fail "async_zero ($reader): a dropped initializer under a zero reset was not reported"

  for t in async_same async_xbits sync_diff; do
    if grep -q "has an initializer" "$W/${t}_$reader.log"; then
      grep "has an initializer" "$W/${t}_$reader.log" >&2
      fail "$t ($reader): warned although nothing is lost"
    fi
  done
  grep -qE "initial q = \([0-9]+'sh5\)" "$W"/out_sync_diff_$reader/*.v \
    || { cat "$W"/out_sync_diff_$reader/*.v >&2; fail "sync_diff ($reader): the start value 5 is no longer kept"; }
done
echo "PASS: yosys_init_reset_warning_test"
