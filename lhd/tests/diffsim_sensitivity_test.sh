#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Sensitivity control for scripts/coreet_lowering_diffsim.py.
#
# That harness reports DIFFSIM-PASS, which is only worth anything if the harness
# can actually SEE a difference. A testbench that drives nothing, samples the
# wrong signal, or compares two copies of the same netlist would report
# DIFFSIM-PASS on every module forever, and the failure would look like success.
#
# So: build a tiny two-module pair that differs in exactly one output bit, and
# require MISMATCH; then compare a netlist against ITSELF and require
# DIFFSIM-PASS. One of each, because a harness that always says MISMATCH is
# equally useless.
#
# Self-contained: needs verilator and python3, not lhd, yosys or CORE-ET.
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DS=""
for c in "${TEST_SRCDIR:-}/_main/scripts/coreet_lowering_diffsim.py" \
         "${TEST_SRCDIR:-}/scripts/coreet_lowering_diffsim.py" \
         "$(cd "$HERE/../.." 2>/dev/null && pwd)/scripts/coreet_lowering_diffsim.py" \
         "scripts/coreet_lowering_diffsim.py"; do
  [ -r "$c" ] && { DS="$c"; break; }
done
[ -n "$DS" ] || { echo "FAIL: cannot find coreet_lowering_diffsim.py"; exit 1; }
command -v verilator >/dev/null 2>&1 || { echo "note: no verilator; SKIPPED"; echo "PASS: diffsim_sensitivity_test (skipped)"; exit 0; }

T="${TEST_TMPDIR:-$(cd "$HERE/../.." && pwd)/generated/tests}/diffsim_sensitivity"
rm -rf "$T"; mkdir -p "$T"

# `golden` and `mutant` differ ONLY in the inverted y1. Both use the ANSI port
# shape the real emitted netlists have, so the harness's port parser is on the
# same path it uses in production.
cat > "$T/golden.v" <<'EOF'
module dut_m(
   input signed [3:0] a
  ,input signed [3:0] b
  ,input signed sel
  ,output reg [3:0] y0
  ,output reg y1
);
always_comb begin
  y0 = sel ? (a & b) : (a | b);
  y1 = ^(a ^ b);
end
endmodule
EOF
sed 's/  y1 = \^(a \^ b);/  y1 = ~(^(a ^ b));/' "$T/golden.v" > "$T/mutant.v"
grep -q '~(\^(a \^ b))' "$T/mutant.v" || { echo "FAIL: the mutation did not apply"; exit 1; }

run() {  # <impl> <ref> <outdir>
  python3 "$DS" --top dut_m --impl "$1" --ref-netlist "$2" --out "$3" \
    --vectors 64 --seed 7 2>&1
}

echo "--- a planted one-bit output inversion MUST be caught ---"
out="$(run "$T/mutant.v" "$T/golden.v" "$T/mut")"; rc=$?
echo "$out" | sed 's/^/    /'
if [ "$rc" -eq 0 ] || ! echo "$out" | grep -q "DIFFSIM-MISMATCH"; then
  echo "FAIL: the harness did NOT catch an inverted output bit -- every DIFFSIM-PASS it reports is meaningless"
  exit 1
fi
echo "ok: mismatch detected"

echo "--- and an identical pair MUST pass ---"
out="$(run "$T/golden.v" "$T/golden.v" "$T/same")"; rc=$?
echo "$out" | sed 's/^/    /'
if [ "$rc" -ne 0 ] || ! echo "$out" | grep -q "DIFFSIM-PASS"; then
  echo "FAIL: the harness reported a difference between a netlist and itself"
  exit 1
fi
echo "ok: identical pair passes"

echo "--- a short/truncated run must be NOT-MEASURED, never a pass ---"
# Half the vectors the testbench was generated for: the TB $fatal()s, the sim
# exits nonzero, and the prefix it did print must not be read as agreement.
head -20 "$T/same/vectors.hex" > "$T/same/vectors.trunc"
mv "$T/same/vectors.hex" "$T/same/vectors.full"
mv "$T/same/vectors.trunc" "$T/same/vectors.hex"
out="$("$T/same/impl/sim" 2>&1 >/dev/null; echo "rc=$?")"
case "$out" in
  rc=0) echo "FAIL: a truncated vector file still exited 0 -- the TB guard is gone"; exit 1 ;;
  *)    echo "ok: truncated input makes the simulation exit nonzero ($out)" ;;
esac

echo "PASS: diffsim_sensitivity_test"
