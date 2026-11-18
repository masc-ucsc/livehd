#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Synthesis is a pure function of its inputs: the same design synthesized twice, into fresh workdirs and with
# different temporary directories, must emit byte-identical netlists AND source maps. The fixture exercises the
# whole-design state proof (content-named `__f<hash>` copies, constant-bit propagation) and memory lowering, whose
# generated RTL lives in a randomly named temporary directory; its source locations must not depend on that name.
set -u

LHD="${LHD:-lhd/lhd}"
SRC="$PWD/lhd/tests/synth_flat_state.v"
LIB="$PWD/lhd/tests/abc_arst_q.lib"
W="${TEST_TMPDIR:-/tmp/lhd_synth_deterministic_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

ROM="$PWD/lhd/tests/synth_rom_srcmap.v"

check() {  # check <name> <top> <src> [extra lhd args...]
  local name=$1 top=$2 src=$3
  shift 3
  for run in a b; do
    mkdir -p "$W/$name/tmp_$run"
    TMPDIR="$W/$name/tmp_$run" "$LHD" synth --result-json "$W/$name/r_$run.json" --reader slang --top "$top" \
      --workdir "$W/$name/W_$run" --emit-dir verilog:"$W/$name/netv_$run" --set synth.mapper=abc \
      --set synth.liberty="$LIB" "$@" -- "$src" > "$W/$name/synth_$run.log" 2>&1 \
      || fail "$name synth $run -> $(cat "$W/$name/r_$run.json" 2>/dev/null)"
  done
  [ "$(cd "$W/$name/netv_a" && ls | sort)" = "$(cd "$W/$name/netv_b" && ls | sort)" ] || fail "$name: different file sets"
  for f in $(cd "$W/$name/netv_a" && ls); do
    cmp -s "$W/$name/netv_a/$f" "$W/$name/netv_b/$f" || fail "$name: $f differs between two identical runs"
  done
}

check flat synth_flat_state "$SRC"
ls "$W/flat/netv_a" | grep -q '^ring_node__f[0-9a-f_]*\.v$' || fail "no flat-state copy: $(ls "$W/flat/netv_a")"
check rom synth_rom_srcmap "$ROM" --set synth.memory=flops
ls "$W/rom/netv_a" | grep -q '^cgen_memory_.*_blasted\.v$' || fail "the ROM was not lowered: $(ls "$W/rom/netv_a")"
echo PASS
