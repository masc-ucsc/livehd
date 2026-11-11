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

for design in comb_reg:comb_reg reset_enable:reset_enable hier:hier struct_port:struct_port reduce_shift:reduce_shift memory:memory memory_array:memory_array nary:nary setmask:setmask; do
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
  "$LHD_ABS" pass lg2lnast --top "$top" "lg:$d/ref" ${set_passes[@]+"${set_passes[@]}"} --emit-dir "ln:$d/ln_$tag" -q >"$d/l2l_$tag.log" 2>&1 \
    || { cat "$d/l2l_$tag.log" >&2; fail "$file ($tag): lg2lnast failed"; }
  "$LHD_ABS" compile "ln:$d/ln_$tag" --top "$top" --emit-dir "lg:$d/impl_$tag" -q >"$d/c2_$tag.log" 2>&1 \
    || { cat "$d/c2_$tag.log" >&2; fail "$file ($tag): the emitted LNAST does not compile"; }
  "$LHD_ABS" lec --ref "lg:$d/ref" --impl "lg:$d/impl_$tag" --top "$top" --set formal.effort=proof --result-json "$d/lec_$tag.json" -q >"$d/lec_$tag.log" 2>&1 \
    || { cat "$d/lec_$tag.log" >&2; fail "$file ($tag): lec failed"; }
  grep -q '"verdict": *"proven-full"' "$d/lec_$tag.json" || fail "$file ($tag): lec did not prove the round trip: $(cat "$d/lec_$tag.json")"
  done
  echo "ok $file"
done

# Compact loops (Pyrope sources): the loop comes back as a `for` with its body inline and no call. The
# reference keeps a compact loop, so the proof is the bounded one (proven-full is accepted too). The two
# memory loops are only checked structurally here (a `for`, no call, no packed whole-array bus): a Pyrope
# memory with reset does not round-trip through LNAST exactly even without a loop, and their behaviour is
# covered by the lnast_llvm differential test against `lhd sim`.
for file in loop_mem loop_overlay loop_nested loop_step loop_call; do
  d="$W/$file"
  mkdir -p "$d"
  "$LHD_ABS" compile "$SRC/$file.prp" --top "$file" --emit-dir "lg:$d/ref" --workdir "$d/tw" -q >"$d/c1.log" 2>&1 \
    || { cat "$d/c1.log" >&2; fail "$file: compile to lg failed"; }
  "$LHD_ABS" pass lg2lnast --top "$file.$file" "lg:$d/ref" --emit-dir "ln:$d/ln" --emit-dir "lnast-dump:$d/dump" -q >"$d/l2l.log" 2>&1 \
    || { cat "$d/l2l.log" >&2; fail "$file: lg2lnast failed"; }
  grep -q "fcall" "$d"/dump/*.lnast && fail "$file: a loop was lowered as a call"
  grep -q "── for" "$d"/dump/*.lnast || fail "$file: no for loop in the LNAST"
  if [ "$file" = loop_mem ] || [ "$file" = loop_overlay ]; then
    grep -q "concat\|shl\|sra" "$d"/dump/*.lnast && fail "$file: the memory was copied into a packed value"
    echo "ok $file (structure)"
    continue
  fi
  "$LHD_ABS" compile "ln:$d/ln" --top "$file" --emit-dir "lg:$d/impl" -q >"$d/c2.log" 2>&1 \
    || { cat "$d/c2.log" >&2; fail "$file: the emitted LNAST does not compile"; }
  "$LHD_ABS" lec --ref "lg:$d/ref" --impl "lg:$d/impl" --top "$file" --set formal.effort=proof --result-json "$d/lec.json" -q >"$d/lec.log" 2>&1 \
    || { cat "$d/lec.log" >&2; fail "$file: lec failed"; }
  grep -q '"verdict": *"proven-\(full\|bounded\)"' "$d/lec.json" || fail "$file: lec did not prove the round trip: $(cat "$d/lec.json")"
  echo "ok $file"
done

# The stream pass really rewrote the n-ary operators (and only then): a balanced tree has %bal temps.
"$LHD_ABS" pass lg2lnast --top nary "lg:$W/nary/ref" --set pass.lg2lnast.passes=balance --emit-dir "lnast-dump:$W/nary/dump" -q >/dev/null 2>&1 \
  || fail "nary: balance dump failed"
grep -q '%bal' "$W"/nary/dump/nary.lnast || fail "nary: balance pass did not rewrite any n-ary operator"
"$LHD_ABS" pass lg2lnast --top nary "lg:$W/nary/ref" --set pass.lg2lnast.passes=bogus --emit-dir "ln:$W/nary/bogus" -q >/dev/null 2>&1 \
  && fail "nary: an unknown stream pass was accepted"
echo "ok balance rewrite"
