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

for design in comb_reg:comb_reg reset_enable:reset_enable hier:hier struct_port:struct_port reduce_shift:reduce_shift memory:memory memory_array:memory_array nary:nary; do
  file="${design%%:*}"
  top="${design##*:}"
  d="$W/$file"
  mkdir -p "$d"
  "$LHD_ABS" compile "$SRC/$file.sv" --reader slang --top "$top" --emit-dir "lg:$d/ref" -q >"$d/c1.log" 2>&1 \
    || { cat "$d/c1.log" >&2; fail "$file: compile to lg failed"; }
  for passes in "" balance; do
  tag="${passes:-plain}"
  set_passes=()
  [ -n "$passes" ] && set_passes=(--set "pass.lg2lnast.passes=$passes")
  "$LHD_ABS" pass lg2lnast --top "$top" "lg:$d/ref" "${set_passes[@]}" --emit-dir "ln:$d/ln_$tag" -q >"$d/l2l_$tag.log" 2>&1 \
    || { cat "$d/l2l_$tag.log" >&2; fail "$file ($tag): lg2lnast failed"; }
  "$LHD_ABS" compile "ln:$d/ln_$tag" --top "$top" --emit-dir "lg:$d/impl_$tag" -q >"$d/c2_$tag.log" 2>&1 \
    || { cat "$d/c2_$tag.log" >&2; fail "$file ($tag): the emitted LNAST does not compile"; }
  "$LHD_ABS" lec --ref "lg:$d/ref" --impl "lg:$d/impl_$tag" --top "$top" --set formal.effort=proof --result-json "$d/lec_$tag.json" -q >"$d/lec_$tag.log" 2>&1 \
    || { cat "$d/lec_$tag.log" >&2; fail "$file ($tag): lec failed"; }
  grep -q '"verdict": *"proven-full"' "$d/lec_$tag.json" || fail "$file ($tag): lec did not prove the round trip: $(cat "$d/lec_$tag.json")"
  done
  echo "ok $file"
done

# The stream pass really rewrote the n-ary operators (and only then): a balanced tree has %bal temps.
"$LHD_ABS" pass lg2lnast --top nary "lg:$W/nary/ref" --set pass.lg2lnast.passes=balance --emit-dir "lnast-dump:$W/nary/dump" -q >/dev/null 2>&1 \
  || fail "nary: balance dump failed"
grep -q '%bal' "$W"/nary/dump/nary.lnast || fail "nary: balance pass did not rewrite any n-ary operator"
"$LHD_ABS" pass lg2lnast --top nary "lg:$W/nary/ref" --set pass.lg2lnast.passes=bogus --emit-dir "ln:$W/nary/bogus" -q >/dev/null 2>&1 \
  && fail "nary: an unknown stream pass was accepted"
echo "ok balance rewrite"
