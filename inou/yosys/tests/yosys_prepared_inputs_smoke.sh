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

# Model expensive lowering after reading an original, unlowered input. The
# clock guard prepares these inputs outside the proof budget. Strategies may
# still run proc on already lowered designs; that empty pass is inexpensive.
# Reading and lowering the original again exhausts the short proof budget.
printf '#!/bin/bash\nreal_yosys=%q\n' "$YOSYS_ABS" >"$W/slow_prepare_yosys"
cat >>"$W/slow_prepare_yosys" <<'SH'
case "$*" in
  *'lgcheck_gold.il;'*'proc; bmuxmap;'*) sleep 4 ;;
esac
exec "$real_yosys" "$@"
SH
chmod +x "$W/slow_prepare_yosys"

for variant in equivalent changed; do
  if [ "$variant" = changed ]; then
    sed 's/a + b/a ^ b/' "$W/ref.v" >"$W/impl.v"
  fi
  rc=0
  (cd "$W/run" && LGCHECK_EQUIV_TIMEOUT=3 "$LGCHECK" \
    --yosys "$W/slow_prepare_yosys" --top prepared \
    --reference "$W/ref.v" --implementation "$W/impl.v") \
    >"$W/$variant.log" 2>&1 || rc=$?
  expected=0
  [ "$variant" != changed ] || expected=1
  if [ "$rc" -ne "$expected" ]; then
    cat "$W/$variant.log" >&2
    echo "FAIL: $variant returned $rc, expected $expected" >&2
    exit 1
  fi
done
echo 'PASS: preparation is reused within a proof, and changed inputs never reuse stale results'
