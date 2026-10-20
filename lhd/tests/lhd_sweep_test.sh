#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# pass/sweep: compile.flatten, compile.unknown_zero and compile.sweep.
#
# Fixture lhd/tests/sweep_mini.sv: a stub endpoint feeds a two-stage buffer chain
# (a1, a2) next to a buffer chain (b1) fed by a real input, plus a 4-bit counter
# `lost_q` nothing reads. Every buffer is a full flag and an 8-bit data register
# with an async reset to 0 (9 flops), so the unswept design has 27 + 4 = 31.
#
#   STUB=0  the stub never sends: after flatten + sweep the a-chain can only hold
#           its reset value and `lost_q` is unobserved, leaving b1's 9 flops. The
#           mapped design is LEC-equivalent to the unswept one (the proof is a
#           bounded one from reset: the constant-state invariant is not inductive
#           from an arbitrary state, so the verdict is proven-bounded).
#   STUB=1  the stub is live (valid = the `go` input) but its data is the constant
#           0x5a: `lost_q` goes, and so do the four bits of each a-chain data
#           register that 0x5a leaves at 0 (19 flops left, the partially
#           constant registers rebuilt over their live bits), still LEC-equal.
#   STUB=2  the stub's valid is an undriven output (x): the flags fold only once
#           compile.unknown_zero reads the x as 0.
# flatten alone and sweep alone are checked too: flatten changes no state, sweep
# without flatten sees each buffer as a separate module and so only drops `lost_q`.
set -u

LHD="${LHD:-lhd/lhd}"
LIB=lhd/tests/abc_arst_q.lib
SRC=lhd/tests/sweep_mini.sv
TOP=sweep_mini
W="${TEST_TMPDIR:-/tmp/lhd_sweep_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}
[ -f "$LIB" ] || fail "missing liberty $LIB"
[ -f "$SRC" ] || fail "missing fixture $SRC"

dffs() {  # dffs <case-dir>: physical mapped flop cells (instances of a shared module count each time)
  python3 - "$1/w/synth/qor.json" <<'PYCODE'
import json, sys
print(sum(json.load(open(sys.argv[1]))['dff']['cells'].values()))
PYCODE
}

# synth_case <dir> <STUB> [extra --set ...]: isolate the compile sweep from
# specialization and the mapper's separate state optimizations.
synth_case() {
  local d="$1" stub="$2"
  shift 2
  mkdir -p "$d"
  "$LHD" synth "$SRC" --top "$TOP" --workdir "$d/w" --emit-dir verilog:"$d/net/" \
      --set synth.liberty="$LIB" --set synth.opentimer=false \
      --set compile.specialize_modules=false --set synth.state_sweep=false --set synth.merge_registers=false -q --result-json "$d/r.json" "$@" -- -GSTUB="$stub" \
      || fail "synth STUB=$stub $* -> $(cat "$d/r.json" 2>/dev/null)"
}

expect_dffs() {  # expect_dffs <dir> <n> <what>
  local n
  n=$(dffs "$1")
  [ "$n" -eq "$2" ] || fail "$3: expected $2 flop cells, got $n"
}

SWEEP=(--set compile.flatten=true --set compile.sweep=true)

# ---------------------------------------------------------------------------
# 1. flop counts: default, flatten alone, sweep alone, both
# ---------------------------------------------------------------------------
synth_case "$W/s0_base" 0
expect_dffs "$W/s0_base" 31 "STUB=0 default"
synth_case "$W/s0_flat" 0 --set compile.flatten=true
expect_dffs "$W/s0_flat" 31 "STUB=0 flatten alone (hierarchy flattened, no state removed)"
synth_case "$W/s0_sweep" 0 --set compile.sweep=true
expect_dffs "$W/s0_sweep" 27 "STUB=0 sweep alone (only the unobserved counter goes)"
synth_case "$W/s0_both" 0 "${SWEEP[@]}"
expect_dffs "$W/s0_both" 9 "STUB=0 flatten + sweep"
echo "PASS: STUB=0 keeps 31 flops by default and 9 after flatten + sweep (flatten alone 31, sweep alone 27)"

synth_case "$W/s1_base" 1
expect_dffs "$W/s1_base" 31 "STUB=1 default"
synth_case "$W/s1_both" 1 "${SWEEP[@]}"
expect_dffs "$W/s1_both" 19 "STUB=1 flatten + sweep (a live stub keeps both chains' flags and live data bits)"
echo "PASS: a live stub keeps both chains' flags and live data bits; the counter and the constant data bits go"

synth_case "$W/s2_noz" 2 "${SWEEP[@]}"
expect_dffs "$W/s2_noz" 19 "STUB=2 without unknown_zero (x is not a constant: the flags stay)"
synth_case "$W/s2_z" 2 "${SWEEP[@]}" --set compile.unknown_zero=true
expect_dffs "$W/s2_z" 9 "STUB=2 with unknown_zero"
echo "PASS: an undriven output folds only under compile.unknown_zero"

# ---------------------------------------------------------------------------
# 2. equivalence: the unswept compile result against the flattened + swept one
# ---------------------------------------------------------------------------
lec_pair() {  # lec_pair <STUB> <name>: ref = default compile, impl = flatten + sweep
  local stub="$1" n="$2" d="$W/lec_$2"
  mkdir -p "$d"
  "$LHD" compile "$SRC" --top "$TOP" --set compile.specialize_modules=false --emit-dir lg:"$d/ref/" --workdir "$d/wr" -q --result-json "$d/rr.json" -- -GSTUB="$stub" \
      || fail "$n: ref compile -> $(cat "$d/rr.json" 2>/dev/null)"
  "$LHD" compile "$SRC" --top "$TOP" --set compile.specialize_modules=false --emit-dir lg:"$d/impl/" --workdir "$d/wi" "${SWEEP[@]}" -q --result-json "$d/ri.json" -- -GSTUB="$stub" \
      || fail "$n: impl compile -> $(cat "$d/ri.json" 2>/dev/null)"
  "$LHD" lec --set formal.effort=proof --impl lg:"$d/impl" --ref lg:"$d/ref" --top "$TOP" --workdir "$d/wl" -q \
      --result-json "$d/lec.json" || fail "$n: lec -> $(cat "$d/lec.json" 2>/dev/null)"
  grep -qE '"verdict":"proven-(full|bounded)"' "$d/lec.json" || fail "$n: not proven: $(cat "$d/lec.json")"
}
lec_pair 0 idle
echo "PASS: STUB=0 flatten + sweep is LEC-equivalent to the default compile"
lec_pair 1 live
echo "PASS: STUB=1 flatten + sweep is LEC-equivalent to the default compile"

# ---------------------------------------------------------------------------
# 3. option validation
# ---------------------------------------------------------------------------
for opt in compile.flatten compile.unknown_zero compile.sweep; do
  if "$LHD" compile "$SRC" --top "$TOP" --workdir "$W/bad_$opt" --set "$opt=maybe" -q --result-json "$W/bad_$opt.json" -- -GSTUB=0 \
      >"$W/bad_$opt.out" 2>&1; then
    fail "$opt=maybe should be rejected"
  fi
  grep -q "$opt" "$W/bad_$opt.json" "$W/bad_$opt.out" || fail "$opt=maybe: no message naming the option"
done
echo "PASS: the three options reject a non-boolean value"
