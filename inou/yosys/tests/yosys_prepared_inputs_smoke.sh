#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# Input lowering must run before the shared proof budget, once per comparison.
# Reusing the same work directory after changing an input must still refute it.
set -euo pipefail
W="${TEST_TMPDIR:-/tmp/lgcheck_prepared_inputs_$$}"
mkdir -p "$W/run"
LGCHECK="$PWD/inou/yosys/lgcheck"
YOSYS_ABS="$PWD/inou/yosys/yosys2"

cat >"$W/ref.v" <<'V'
module prepared(input clk, input [3:0] a, b, output reg [3:0] q);
  wire [3:0] next_value = a + b;
  always @(posedge clk) q <= next_value;
endmodule
V
cp "$W/ref.v" "$W/impl.v"

# Record the actual reads alongside lgcheck's budget-start message. Descriptor
# 3 stays with the test log when lgcheck redirects individual strategy logs.
export YOSYS_ABS
cat >"$W/trace_yosys" <<'SH'
#!/bin/bash
if [ "$1" = -p ]; then
  printf '%s\n' "$2" | tr ';' '\n' | awk '
    $1 == "read_verilog" || $1 == "read_rtlil" || $1 == "write_rtlil" {
      n = split($NF, path, "/")
      print "YOSYS_IO", $1, path[n]
    }
  ' >&3
fi
exec "$YOSYS_ABS" "$@"
SH
chmod +x "$W/trace_yosys"

for variant in equivalent changed; do
  if [ "$variant" = changed ]; then
    sed 's/a + b/a ^ b/' "$W/ref.v" >"$W/impl.v"
  fi
  rc=0
  (cd "$W/run" && "$LGCHECK" \
    --yosys "$W/trace_yosys" --top prepared \
    --reference "$W/ref.v" --implementation "$W/impl.v") \
    >"$W/$variant.log" 2>&1 3>&1 || rc=$?
  expected=0
  [ "$variant" != changed ] || expected=1
  if [ "$rc" -ne "$expected" ]; then
    cat "$W/$variant.log" >&2
    echo "FAIL: $variant returned $rc, expected $expected" >&2
    exit 1
  fi
  if ! awk '
    /^INFO: starting equivalence checks/ { started++ }
    /^YOSYS_IO read_verilog (ref|impl)\.v$/ {
      reads[$3]++
      if (started) late = 1
    }
    /^YOSYS_IO read_rtlil lgcheck_(gold|gate)\.il$/ {
      reads[$3]++
      if (started) late = 1
    }
    /^YOSYS_IO write_rtlil lgcheck_prepared\.rtlil$/ { prepared++; if (started) late = 1 }
    /^YOSYS_IO read_rtlil lgcheck_prepared\.rtlil$/  { if (started) reused++ }
    END {
      exit !(started == 1 && !late && prepared == 1 && reused > 0 &&
             reads["ref.v"] == 1 && reads["impl.v"] == 1 &&
             reads["lgcheck_gold.il"] == 1 && reads["lgcheck_gate.il"] == 1)
    }
  ' "$W/$variant.log"; then
    cat "$W/$variant.log" >&2
    echo "FAIL: $variant did not prepare each input once before the budget and reuse it during proof" >&2
    exit 1
  fi
done
echo 'PASS: preparation is reused within a proof, and changed inputs never reuse stale results'
