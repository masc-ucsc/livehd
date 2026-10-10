#!/usr/bin/env bash
set -uo pipefail
# Positive and negative controls for the RTL equivalence gate.
#
# The gate in run_coreet_module_lean.sh spent its whole life exiting 2 with
# {"command":"usage","status":"fail"} while the runner called that
# "inconclusive" and continued -- 162 recorded gate logs, not one of them an
# equivalence result. The lesson is not "fix the flags": it is that a gate
# nobody has watched REFUTE something is a gate nobody knows can fail.
#
# So this proves both directions on the binary in hand before any sweep trusts
# it: identical RTL must come back `proven`, and a one-bit mutation must come
# back `refuted` with a counterexample. It also fingerprints the binary, because
# a debug build announces `lec-debug-build-slow` and will make a timed replay
# meaningless.
#
# Usage: lec_gate_controls.sh [workdir]

R="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LHD="${LHD:-$R/bazel-bin/lhd/lhd}"
T="${1:-${TMPDIR:-$R/generated/pass_lean_runtime_tmp}/lec_controls.$$}"
mkdir -p "$T"
rc=0

cat > "$T/add4.sv" <<'SV'
module add4(input logic [3:0] a, input logic [3:0] b, output logic [3:0] y);
  assign y = a + b;
endmodule
SV
# A ONE-BIT difference: visible at a=0,b=0, so any solver must find it.
sed 's/assign y = a + b;/assign y = (a + b) ^ 4'"'"'b0001;/' "$T/add4.sv" > "$T/add4_bad.sv"

echo "[lec-controls] binary: $LHD"
"$LHD" compile "$T/add4.sv" --reader yosys-slang --top add4 \
  --emit-dir lg:"$T/lg" --workdir "$T/w0" > "$T/compile.log" 2>&1 || {
  echo "[lec-controls] FAIL: could not build the implementation LGraph" >&2
  tail -5 "$T/compile.log" >&2; exit 2; }

run() {  # <name> <ref.sv> ; returns exit, sets VERDICT
  "$LHD" lec --impl lg:"$T/lg" --ref verilog:"$2" --top add4 \
    --workdir "$T/w_$1" --set formal.engine=auto \
    --result-json "$T/$1.json" > "$T/$1.log" 2>&1
  local e=$?
  VERDICT="$(grep -oP '"verdict":"\K[^"]*' "$T/$1.json" 2>/dev/null | tail -1)"
  return $e
}

run pos "$T/add4.sv"; e=$?
if [[ $e -eq 0 && "$VERDICT" == proven ]]; then
  echo "[lec-controls] ok: identical RTL -> proven (exit 0)"
else
  echo "[lec-controls] FAIL: identical RTL gave exit=$e verdict='${VERDICT:-<none>}'"; rc=1
fi

run neg "$T/add4_bad.sv"; e=$?
if [[ "$VERDICT" == refuted ]]; then
  echo "[lec-controls] ok: mutated RTL -> refuted (exit $e)"
  grep -m1 'counterexample' "$T/neg.log" | sed 's/^/[lec-controls]   /' || {
    echo "[lec-controls] FAIL: refuted with NO counterexample -- cannot confirm the solver ran"; rc=1; }
else
  echo "[lec-controls] FAIL: mutated RTL gave exit=$e verdict='${VERDICT:-<none>}' -- the gate cannot fail"; rc=1
fi

# A timed replay against a debug binary measures the build, not the design.
if grep -q 'lec-debug-build-slow' "$T/pos.log" 2>/dev/null; then
  echo "[lec-controls] WARNING: this is a DEBUG build (lec-debug-build-slow)."
  echo "[lec-controls]   Fine for correctness; do NOT publish timings from it."
fi
[[ $rc -eq 0 ]] && echo "[lec-controls] PASS: the gate proves AND refutes"
exit $rc
