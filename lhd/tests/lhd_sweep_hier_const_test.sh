#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# compile.sweep + pass.specialize: a constant crosses REGISTERS without flattening. Fixture sweep_hier_const.v is a
# three-stage chain of one module whose first instance is fed only by constants. By default only the first stage folds
# (its next state is a plain constant): the other two keep their 9 registers (18 flops). With compile.sweep the
# specialization proves each following stage's registers constant in turn
# (the per-definition state sweep inside the refold), publishes the constant outputs to the parent and finishes with
# no flop, while the hierarchy is kept (no compile.flatten).
set -u

LHD="${LHD:-lhd/lhd}"
SRC="$PWD/lhd/tests/sweep_hier_const.v"
LIB="$PWD/lhd/tests/abc_arst_qn.lib"
W="${TEST_TMPDIR:-/tmp/lhd_sweep_hier_const_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

synth() { # <tag> [extra --set ...]
  local d="$W/$1"
  shift
  mkdir -p "$d"
  "$LHD" synth --result-json "$d/r.json" --reader slang --top sweep_hier_const --workdir "$d/W" --emit-dir verilog:"$d/netv" \
    --set synth.mapper=abc --set synth.liberty="$LIB" "$@" -- "$SRC" > "$d/synth.log" 2>&1 || fail "$d: synth -> $(cat "$d/r.json" 2>/dev/null)"
  cat "$d/netv/"*.v > "$d/net.v"
}
flops() { grep -h '^DFF' "$1" | wc -l | tr -d ' '; }

synth off
[ "$(flops "$W/off/net.v")" = 18 ] || fail "off: expected 18 flops (first stage folded), got $(flops "$W/off/net.v")"

synth on --set compile.sweep=true
[ "$(flops "$W/on/net.v")" = 0 ] || fail "on: expected no flop after the hierarchical constant sweep, got $(flops "$W/on/net.v")"

# The mapper-private flat sweep must be reachable through both mapper APIs.
synth flat --set synth.flat_sweep=true
[ "$(flops "$W/flat/net.v")" = 0 ] || fail "flat: expected the constant register chain to fold"
"$LHD" synth --reader slang --top sweep_hier_const --workdir "$W/usyn" --emit verilog:"$W/usyn.v" \
  --set synth.mapper=usyn --set pass.usyn.tmap=none --set synth.flat_sweep=true -- "$SRC" > "$W/usyn.log" 2>&1 \
  || fail "usyn: flat_sweep failed"
grep -q 'flat-swept' "$W/usyn.log" || fail "usyn: flat_sweep was not applied"
grep -q '^always @' "$W/usyn.v" && fail "usyn: the constant register chain survived flat_sweep"
if "$LHD" synth --reader slang --top sweep_hier_const --workdir "$W/bad" \
  --set synth.mapper=usyn --set pass.usyn.tmap=none --set pass.usyn.flat_sweep=maybe -- "$SRC" > "$W/bad.log" 2>&1; then
  fail "usyn: invalid flat_sweep value was accepted"
fi
echo "PASS: compile.sweep crosses registers through pass.specialize (18 flops -> 0, hierarchy kept)"
