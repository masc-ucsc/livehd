#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# H5 warm==cold across a PARTIAL compile-cache restore when pass.legalize
# inlines children into a live parent.
#
# pass.legalize inlines a state-free child instance that sits on a
# combinational cycle (here: the top's comb logic between two passes through a
# registered instance) INTO the parent body, copying the child as it stands
# when the parent's legalize runs. In a cold compile that is the child right
# after tolg + the first cprop/bitwidth. A partial restore used to hand the
# live parent the child's FINAL cached body instead, so after an edit of one
# inlined child (alu) the warm top inlined the other one (immg) in its
# post-formal shape and came out structurally different from a cold compile of
# the same sources -- LEC-equivalent, but lhdsuite's H5 gate failed on dino
# (bug1) with other Sum/And operand orders and fewer sext/get_mask cells.
#
# The restore now runs every child a live parent inlined live as well; the
# registered child (sreg), which nothing inlines, is still restored.
set -u

LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_compile_cache_inline_$$}/cc_inline"
rm -rf "$W"
mkdir -p "$W/src"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

cat > "$W/src/sreg.prp" <<'EOF'
pub mod sreg::[timecheck=false](`clock`:Clock, `reset`:U1, d:U64) -> (q:U64@[]) {
  reg r:U64:[initial=0, reset_pin=`reset`, async=false]
  q = r
  r = d
}
EOF
cat > "$W/src/immg.prp" <<'EOF'
pub comb immg::[timecheck=false](io_instruction:U64) -> (io_sextImm:U64) {
  wire opcode:U7 = nil
  wire imm_3:U12 = nil
  wire imm_4:U12 = nil
  wire _GEN_3:U64 = nil
  wire _GEN_4:U64 = nil
  opcode = io_instruction#[0..<7]
  imm_3 = io_instruction#[20..<32]
  imm_4 = (io_instruction#[31..<32] << 11) | (io_instruction#[7..<8] << 10) | (io_instruction#[25..<31] << 4) | io_instruction#[8..<12]
  mut _mux_1:U64 = if opcode == 0x73 { io_instruction#[15..<20] } else { 0 }
  _GEN_3 = _mux_1
  mut _mux_6:U64 = if opcode == 0x63 { (imm_4#[0..<12] << 1)#sext[0..=12]#[0..<0x40] } else { _GEN_3#[0..<0x40] }
  _GEN_4 = _mux_6
  mut _mux_7:U64 = if opcode == 0x67 { imm_3#[0..<12]#sext[0..=11]#[0..<0x40] } else { _GEN_4#[0..<0x40] }
  io_sextImm = _mux_7#[0..<0x40]
}
EOF
cat > "$W/src/alu.prp" <<'EOF'
pub comb alu::[timecheck=false](a:U64, b:U64) -> (o:U64) {
  o = (a + b)#[0..<0x40]
}
EOF
cat > "$W/src/ptop.prp" <<'EOF'
const sreg = import("sreg.sreg")
const immg = import("immg.immg")
const alu  = import("alu.alu")
pub mod ptop::[timecheck=false](`clock`:Clock, `reset`:U1, x:U64) -> (y:U64@[]) {
  wire _s_q:U64 = nil
  wire _alu_o:U64 = nil
  mut i = immg::[name=i](io_instruction = _s_q)
  mut a = alu::[name=a](a = i, b = x)
  _alu_o = a
  mut s = sreg::[name=s](`clock` = `clock`, `reset` = `reset`, d = _alu_o)
  _s_q = s
  y = _alu_o
}
EOF

compile() {  # RESULT_JSON LG_DIR WORKDIR [extra...]
  local result=$1 lg=$2 wd=$3
  shift 3
  "$LHD" compile "$W/src/ptop.prp" --top ptop --emit-dir "lg:$lg" --workdir "$wd" -q --result-json "$result" "$@" \
    || fail "compile failed: $(cat "$result" 2>/dev/null)"
}
field() {
  python3 - "$1" "$2" <<'PY'
import json, sys
value = json.load(open(sys.argv[1]))
for key in sys.argv[2].split('.'):
    value = value[key]
print(value)
PY
}

compile "$W/cold.json" "$W/cold_lg" "$W/w"
# Precondition: legalize must have inlined BOTH comb children into the top, or
# the warm run below never exercises the inlined-restored-child path.
python3 - "$W/w/incr/scopes/compile/ptop/lg/graph_inventory.json" <<'PY' || fail "legalize did not inline alu+immg into ptop; the test proves nothing"
import json, sys
rows = {r["name"]: r.get("inlined", []) for r in json.load(open(sys.argv[1]))["graphs"]}
raise SystemExit(0 if {"alu.alu", "immg.immg"} <= set(rows.get("ptop.ptop", [])) else 1)
PY

# Edit ONE inlined child; the other one (immg) and the registered child are clean.
sed -i 's/(a + b)/(a - b)/' "$W/src/alu.prp"
grep -q '(a - b)' "$W/src/alu.prp" || fail "edit did not apply"
compile "$W/warm.json" "$W/warm_lg" "$W/w"
[ "$(field "$W/warm.json" incremental.compile.misses)" = 1 ] || fail "the one-unit edit did not take the partial-restore path"
hits=$(field "$W/warm.json" incremental.compile.hits)
[ "$hits" -ge 4 ] || fail "no clean graph was restored (hits=$hits): the warm run fell back to a full rebuild"

compile "$W/ref.json" "$W/ref_lg" "$W/wref" --set lhd.incremental=false
verdict=$("$LHD" tool diff "lg:$W/ref_lg" "lg:$W/warm_lg" --structural -q)
[ "$verdict" = identical ] || fail "warm compile after a one-child edit differs from a cold compile: $verdict"
echo "PASS: a live parent re-inlines its legalize-inlined children exactly as a cold compile does"

echo "PASS: lhd_compile_cache_inline_test"
