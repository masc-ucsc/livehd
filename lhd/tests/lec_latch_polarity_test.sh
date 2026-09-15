#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# The default LEC engine must prove identical level-sensitive latches and
# refute a transparent-high / transparent-low pair with matching ports.

set -u

LHD="${LHD:-lhd/lhd}"
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT

fail() {
  echo "FAIL: $*"
  exit 1
}

# Transparent-HIGH latch.
cat > "$W/high.v" <<'EOF'
module dut(input g, input [7:0] d, output reg [7:0] q);
  always_latch begin
    if (g)
      q <= d;
  end
endmodule
EOF

# Transparent-LOW latch — SAME ports, only the enable polarity differs.
cat > "$W/low.v" <<'EOF'
module dut(input g, input [7:0] d, output reg [7:0] q);
  always_latch begin
    if (!g)
      q <= d;
  end
endmodule
EOF

# Second copy of the high latch so case 1 compares two independent builds.
cp "$W/high.v" "$W/high2.v"

build() {
  local name="$1"
  "$LHD" compile "$W/$name.v" --reader slang --top dut --emit-dir "lg:$W/lg_$name" \
    -q >"$W/$name.log" 2>&1 || fail "compile $name: $(cat "$W/$name.log")"
}

for m in high high2 low; do
  build "$m"
done

check_lec() {
  local impl="$1" ref="$2"
  "$LHD" lec --impl "lg:$W/lg_$impl" --ref "lg:$W/lg_$ref" --top dut \
    --workdir "$W/lec_${impl}_${ref}" 2>&1
}

# A verdict MUST be present in every run — an empty/errored run must never read
# as success (the vacuous-gate failure mode the 2f-latch page warns about).
# The verdict line quotes the IMPL PATH, not the --top name:
#   lec: '<workdir>/lg_high' PROVEN equivalent (solver=default LEC)
require_verdict() {
  echo "$1" | grep -qE "lec: '.*' (PROVEN|REFUTED|UNKNOWN)" \
    || fail "$2: no verdict in lec output (run failed?): $1"
}

# ------------------------------------------------ case 1: identical => PROVEN
out="$(check_lec high high2)"
require_verdict "$out" "identical latch pair"
echo "$out" | grep -qE "lec: '.*' PROVEN equivalent" \
  || fail "identical transparent-high latches did not PROVE: $out"
echo "ok: identical latch pair PROVEN (oracle not vacuous)"

# ------------------------------------- case 2: polarity flip => REFUTED
out="$(check_lec high low)"
require_verdict "$out" "polarity flip"
echo "$out" | grep -qE "lec: '.*' REFUTED" \
  || fail "transparent-high vs transparent-low did not REFUTE (polarity-blind oracle): $out"
echo "ok: enable-polarity flip REFUTED (oracle discriminates)"

echo "PASS: default LEC decides latches and discriminates enable polarity"
