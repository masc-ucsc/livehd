#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# Saved deferred templates retain their signature and specialize in a fresh process.
set -eu
LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_ln_generic_$$}"
mkdir -p "$W"
cat > "$W/templates.prp" <<'PRP'
pub mod add_node<T>::[timecheck=false](a:T, b:T) -> (s:T@[0]) {
  reg s_r:T = 0
  s = s_r
  wrap s_r = a + b
}
pub mod multiply<FAST=false>::[timecheck=false](a:u8) -> (s:u8@[0]) {
  s = if FAST { a ^ 3 } else { a }
}
pub mod top::[timecheck=false](a:u8, b:u8) -> (s:u8@[0]) {
  const mul = multiply<FAST=true>(a=a)
  s = add_node<u8>(a=mul, b=b)
}
pub mod signed_chain::[timecheck=false](a:s4, b:s4) -> (s:s10@[0]) {
  const first = add_node<s10>(a=a, b=b)
  const second = add_node<s10>(a=first, b=first)
  const third = add_node<s10>(a=second, b=second)
  s = add_node<s10>(a=third, b=third)
}
PRP
"$LHD" compile "$W/templates.prp" --emit-dir "ln:$W/library" --workdir "$W/export"
"$LHD" lec --impl "ln:$W/library" --ref "$W/templates.prp" --top top \
  --workdir "$W/reload" --result-json "$W/reload.json"
grep -q '"verdict":"proven".*"bounded":false' "$W/reload.json"
"$LHD" lec --impl "ln:$W/library" --ref "$W/templates.prp" --top signed_chain \
  --set formal.engine=ind --workdir "$W/signed_chain" --result-json "$W/signed_chain.json"
grep -q '"verdict":"proven".*"bounded":false' "$W/signed_chain.json"
# Hide the source so the importer cannot silently rediscover it. Exercise a
# new specialization and an omitted default in a source + ln: invocation.
cat > "$W/consumer.prp" <<'PRP'
const lib = import("templates")
const add = import("templates.add_node")
pub mod consumer::[timecheck=false](a:u8, b:u8) -> (s:u8@[0]) {
  const mul = lib.multiply(a=a)
  s = add<u8>(a=mul, b=b)
}
PRP
cat > "$W/reference.prp" <<'PRP'
pub mod consumer::[timecheck=false](a:u8, b:u8) -> (s:u8@[0]) {
  reg s_r:u8 = 0
  s = s_r
  wrap s_r = a + b
}
PRP
mv "$W/templates.prp" "$W/templates.hidden"
"$LHD" compile "$W/consumer.prp" "ln:$W/library" --top consumer \
  --emit-dir "lg:$W/consumer_lg" --workdir "$W/import"
"$LHD" lec --impl "lg:$W/consumer_lg" --ref "$W/reference.prp" --top consumer \
  --workdir "$W/proof" --result-json "$W/proof.json"
grep -q '"verdict":"proven".*"bounded":false' "$W/proof.json"
echo 'PASS: saved generic templates and fresh importer'
