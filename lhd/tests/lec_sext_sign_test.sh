#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Regression for the cvc5 LEC/formal encoder's `#sext` (partial sign-extend) sign
# propagation. The encoder (pass/lec, shared by pass/formal) reasons on the
# LGraph directly: a `#sext` slice must SIGN-extend (not zero-extend) when it is
# widened, arithmetic-shifted (`>>` on a signed operand), or signed-compared.
# Historically it zero-extended, so `lhd lec` REFUTED designs that simulate
# correctly (e.g. an ALU word path: ref=0xFFFFFFFFFFFFFFFF vs impl=0x00000000FFFFFFFF).
# Fixed by stamping THIS design's sign on each encoded value (encode.cpp
# `v.is_signed = sgn`). Each case proves the `#sext`-slice form EQUIVALENT — via
# the default cvc5 solver — to a GROUND-TRUTH form that expresses the sign
# behavior WITHOUT relying on the slice's sign flag (explicit conditional bit
# fill, or a native signed type). A regression to zero-extend would REFUTE.

set -u
LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_lec_sext_$$}"
mkdir -p "$W"
fail() { echo "FAIL: $*" >&2; exit 1; }

# prove_equiv <name> : compile $W/<name>_a.prp and $W/<name>_b.prp to LGraphs and
# prove them equivalent with the in-process cvc5 solver (default). Both modules
# are named <name>; their units are <name>_a.<name> / <name>_b.<name>.
prove_equiv() {
  local n="$1"
  "$LHD" compile "$W/${n}_a.prp" --top "$n" --emit-dir "lg:$W/${n}_a/lg/" \
    --set compile.formal.mode=none --workdir "$W/${n}_a/w" -q >/dev/null 2>&1 \
    || fail "${n}: ref (#sext) compile failed"
  "$LHD" compile "$W/${n}_b.prp" --top "$n" --emit-dir "lg:$W/${n}_b/lg/" \
    --set compile.formal.mode=none --workdir "$W/${n}_b/w" -q >/dev/null 2>&1 \
    || fail "${n}: ground-truth compile failed"
  "$LHD" lec --ref "lg:$W/${n}_a/lg/" --impl "lg:$W/${n}_b/lg/" \
    --ref-top "${n}_a.${n}" --impl-top "${n}_b.${n}" \
    --workdir "$W/${n}/lec" -q --result-json "$W/${n}.json" \
    || fail "${n}: cvc5 lec did NOT prove equivalent (sext sign regression?): $(cat "$W/${n}.json" 2>/dev/null)"
  grep -q '"status":"pass"' "$W/${n}.json" || fail "${n}: lec not pass: $(cat "$W/${n}.json")"
  echo "PASS(${n}): #sext-slice == ground truth (cvc5)"
}

# 1. WIDEN: `#sext[0..=31]` into a wider UNSIGNED net must sign-extend, matching
#    an explicit conditional fill of the upper bits from bit 31.
cat >"$W/widen_a.prp" <<'EOF'
comb widen(x:u64) -> (r:u64) {
  r = x#sext[0..=31]
}
EOF
cat >"$W/widen_b.prp" <<'EOF'
comb widen(x:u64) -> (r:u64) {
  mut s:u64 = x#[0..=31]
  if x#[31] != 0 { s#[32..=63] = 0xFFFFFFFF }
  r = s
}
EOF
prove_equiv widen

# 2. SRA: `>>` on a `#sext` slice is arithmetic (sign-replicating), matching the
#    same shift on a native signed type.
cat >"$W/sra_a.prp" <<'EOF'
comb sra(x:s32) -> (r:s32) {
  r = x >> 4
}
EOF
cat >"$W/sra_b.prp" <<'EOF'
comb sra(x:u32) -> (r:s32) {
  r = (x#sext[0..=31]) >> 4
}
EOF
prove_equiv sra

# 3. SIGNED COMPARE: `<` between `#sext` slices is a signed compare, matching the
#    compare on native signed types.
cat >"$W/cmp_a.prp" <<'EOF'
comb cmp(x:s32, y:s32) -> (r:bool) {
  r = x < y
}
EOF
cat >"$W/cmp_b.prp" <<'EOF'
comb cmp(x:u32, y:u32) -> (r:bool) {
  r = (x#sext[0..=31]) < (y#sext[0..=31])
}
EOF
prove_equiv cmp

echo "PASS: cvc5 LEC honors #sext sign propagation (widen / SRA / signed-compare)"
