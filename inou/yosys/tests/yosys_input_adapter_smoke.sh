#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# A missing input stays free on the reference-shaped wrapper: unused inputs
# can prove, used inputs must refute, and outputs/inouts cannot be invented.
set -eu
W="${TEST_TMPDIR:-/tmp/lgcheck_input_adapter_$$}"
mkdir -p "$W"
LGCHECK="${LGCHECK:-$PWD/inou/yosys/lgcheck}"
YOSYS_ABS="${YOSYS_ABS:-$PWD/inou/yosys/yosys2}"

cat >"$W/gate.v" <<'V'
module dut(input d, output y);
  assign y = d;
endmodule
V
cat >"$W/unused.v" <<'V'
module dut(input clk, input rst, input d, output y);
  assign y = d;
endmodule
V
cat >"$W/used.v" <<'V'
module dut(input missing, input d, output y);
  assign y = d ^ missing;
endmodule
V
cat >"$W/output.v" <<'V'
module dut(input d, output y, output missing);
  assign y = d;
  assign missing = d;
endmodule
V
cat >"$W/inout.v" <<'V'
module dut(input d, output y, inout missing);
  assign y = d;
  assign missing = d;
endmodule
V

for entry in unused:0 used:1 output:5 inout:5; do
  IFS=: read -r name expected <<<"$entry"
  mkdir -p "$W/$name"
  rc=0
  (cd "$W/$name" && LGCHECK_EQUIV_TIMEOUT=10 "$LGCHECK" \
    --yosys "$YOSYS_ABS" --top dut --normalize_split_ports \
    --reference "$W/$name.v" --implementation "$W/gate.v") \
    >"$W/$name.log" 2>&1 || rc=$?
  if [ "$rc" -ne "$expected" ]; then
    cat "$W/$name.log"
    echo "FAIL: $name rc=$rc, expected $expected" >&2
    exit 1
  fi
  echo "ok: $name rc=$rc"
done
