#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# pass.lg2lnast round trip: Verilog -> lg -> `lhd pass lg2lnast` -> ln: -> compile -> lg'
# and `lhd lec` proves lg == lg' (formal.effort=proof). The LNAST is ordinary
# front-end input, so the second leg is the plain `lhd compile ln:` flow.
set -uo pipefail
LHD="${LHD:-lhd/lhd}"
LHD_ABS="$(cd "$(dirname "$LHD")" && pwd)/$(basename "$LHD")"
SRC="$(cd pass/lg2lnast/tests && pwd)"
W="${TEST_TMPDIR:-/tmp/lg2lnast_rt_$$}"
mkdir -p "$W"
fail() { echo "FAIL: $*" >&2; exit 1; }

for design in comb_reg:comb_reg reset_enable:reset_enable hier:hier struct_port:struct_port reduce_shift:reduce_shift memory:memory memory_array:memory_array; do
  file="${design%%:*}"
  top="${design##*:}"
  d="$W/$file"
  mkdir -p "$d"
  "$LHD_ABS" compile "$SRC/$file.sv" --reader slang --top "$top" --emit-dir "lg:$d/ref" -q >"$d/c1.log" 2>&1 \
    || { cat "$d/c1.log" >&2; fail "$file: compile to lg failed"; }
  "$LHD_ABS" pass lg2lnast --top "$top" "lg:$d/ref" --emit-dir "ln:$d/ln" -q >"$d/l2l.log" 2>&1 \
    || { cat "$d/l2l.log" >&2; fail "$file: lg2lnast failed"; }
  "$LHD_ABS" compile "ln:$d/ln" --top "$top" --emit-dir "lg:$d/impl" -q >"$d/c2.log" 2>&1 \
    || { cat "$d/c2.log" >&2; fail "$file: the emitted LNAST does not compile"; }
  "$LHD_ABS" lec --ref "lg:$d/ref" --impl "lg:$d/impl" --top "$top" --set formal.effort=proof --result-json "$d/lec.json" -q >"$d/lec.log" 2>&1 \
    || { cat "$d/lec.log" >&2; fail "$file: lec failed"; }
  grep -q '"verdict": *"proven-full"' "$d/lec.json" || fail "$file: lec did not prove the round trip: $(cat "$d/lec.json")"
  echo "ok $file"
done
