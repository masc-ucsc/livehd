#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# simfail witness reproduction (`lhd lec` + --workdir): on a REFUTED verdict the
# CLI writes a self-contained Pyrope testbench (formal.simfail, simfail_<top>.prp)
# that instantiates BOTH designs inside one wrapper, drives the counterexample
# input sequence, and (formal.simfail_run) runs `lhd sim` to dump ONE VCD showing the
# impl-vs-ref divergence. This test verifies the deterministic pieces hermetically:
# the .prp generation, the three knobs, the no-workdir/PROVEN gating, that the
# generated testbench is sim-VALID (`lhd sim --setup-only`), and that its
# hierarchical peek + VCD codegen carries the copyable `shared_ptr<VCDWriter>`.
# (The end-to-end VCD host-compile needs hlop's vcd_writer.cpp, not staged in the
# sandbox runfiles, so it is exercised outside CI — see the goal's manual runs.)

set -u

LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi

WORK="${TEST_TMPDIR:-/tmp/lecwit}"
mkdir -p "$WORK"
fail=0
ck() { if eval "$2"; then echo "ok: $1"; else echo "FAIL: $1"; fail=1; fi; }

# impl (+1) vs ref (+2): a reachable divergence the BMC engine refutes with a trace.
cat > "$WORK/impl.prp" <<'EOF'
mod dut(en:bool) -> (value:u8@[0]) {
  reg count:u8 = 0
  value = count
  if en { wrap count += 1 }
}
EOF
cat > "$WORK/ref.prp" <<'EOF'
mod dut(en:bool) -> (value:u8@[0]) {
  reg count:u8 = 0
  value = count
  if en { wrap count += 2 }
}
EOF

# ---- default: --workdir set => simfail_dut.prp written (VCD is best-effort). The lec
# run must still exit REFUTED even if the reproduction sim can't host-compile. ----
W1="$WORK/w1"
OUT=$($LHD lec --impl "$WORK/impl.prp" --ref "$WORK/ref.prp" --workdir "$W1" 2>&1)
ck "REFUTED"                    'echo "$OUT" | grep -q REFUTED'
ck "wrote simfail_dut.prp"      '[ -f "$W1/simfail_dut.prp" ]'
ck "testbench has both modules" 'grep -q "mod dut" "$W1/simfail_dut.prp" && grep -q "lecref_dut" "$W1/simfail_dut.prp"'
ck "testbench has the wrapper"  'grep -q "__simfail_dut_pair" "$W1/simfail_dut.prp"'
ck "testbench has a test block" 'grep -q "^test simfail_dut" "$W1/simfail_dut.prp"'
ck "testbench exposes impl out" 'grep -q "impl_value" "$W1/simfail_dut.prp"'
ck "testbench exposes ref out"  'grep -q "ref_value" "$W1/simfail_dut.prp"'
ck "testbench drives the seq"   'grep -q "const _drv_en = \[" "$W1/simfail_dut.prp"'
ck "testbench drives reset"     'grep -q "_lec_dut.reset" "$W1/simfail_dut.prp"'

# ---- F7: source-mapped root cut + machine-readable simfail_dut.json ------------
# The first diverging STATE cut (the flop the diverging output inherits) is named
# with its impl-side declaration file:line, both in the .prp header and in a sibling
# simfail JSON whose input sequence matches the .prp _drv_* arrays by construction.
ck "prp header has source-mapped root cut" 'grep -Eq "// Root cut:.*impl\.prp:[0-9]+" "$W1/simfail_dut.prp"'
ck "wrote simfail_dut.json"                '[ -f "$W1/simfail_dut.json" ]'
ck "simfail_dut.json parses"               'python3 -m json.tool "$W1/simfail_dut.json" >/dev/null'
cat > "$WORK/chk.py" <<'PY'
import json, re, sys
prp = open(sys.argv[1]).read()
d   = json.load(open(sys.argv[2]))
rc  = d.get("root_cut")
assert rc and rc["file"].endswith("impl.prp") and rc["line"] > 0, f"bad root_cut {rc}"
cyc = d["trace"]["cycles"]
drv = {m.group(1): [x.strip() for x in m.group(2).split(",")]
       for m in re.finditer(r"const _drv_(\w+) = \[([^\]]*)\]", prp)}
assert drv, "no _drv arrays in the .prp"
for name, arr in drv.items():
    assert len(arr) == len(cyc), f"len _drv_{name}={len(arr)} != json cycles={len(cyc)}"
    for i, v in enumerate(arr):
        got = {x["name"]: x["value"] for x in cyc[i]["inputs"]}.get(name)
        assert got is None or got == v, f"_drv_{name}[{i}] prp={v} json={got}"
print("ok")
PY
ck "json root_cut + input sequence match the .prp" 'python3 "$WORK/chk.py" "$W1/simfail_dut.prp" "$W1/simfail_dut.json" | grep -q ok'

# ---- the generated testbench must be SIM-VALID: `lhd sim --setup-only` runs the
# whole front-end (inou.prp -> upass -> tolg -> cgen_sim) without host-compiling,
# so it catches any codegen regression in the generated wrapper/test hermetically.
S1="$WORK/s1"
$LHD sim "$W1/simfail_dut.prp" --setup-only --set sim.vcd=true --workdir "$S1" >/dev/null 2>&1
ck "generated testbench sim-valid" '[ -f "$S1/sim/drv.cpp" ]'
# The hierarchical peek must snapshot sub-instances by value, so the DUT struct
# stays copyable under VCD: __vcd is a shared_ptr, not unique_ptr (the cgen_sim fix).
ck "VCD __vcd is copyable (shared_ptr)" '! grep -rq "unique_ptr<vcd::VCDWriter>" "$S1/sim"/ && grep -rq "shared_ptr<vcd::VCDWriter>" "$S1/sim"/'

# ---- formal.witness=false disables the whole feature ------------------------------
W2="$WORK/w2"
$LHD lec --impl "$WORK/impl.prp" --ref "$WORK/ref.prp" --workdir "$W2" --set formal.witness=false >/dev/null 2>&1
ck "witness=false => no prp"    '[ ! -f "$W2/simfail_dut.prp" ]'

# ---- formal.simfail=false disables generation -----------------------------------------
W3="$WORK/w3"
$LHD lec --impl "$WORK/impl.prp" --ref "$WORK/ref.prp" --workdir "$W3" --set formal.simfail=false >/dev/null 2>&1
ck "simfail=false => no prp"    '[ ! -f "$W3/simfail_dut.prp" ]'

# ---- simfail is boolean; filenames are derived rather than user-supplied ---------------
W4="$WORK/w4"
OUT=$($LHD lec --impl "$WORK/impl.prp" --ref "$WORK/ref.prp" --workdir "$W4" --set formal.simfail=mycex.prp 2>&1)
ck "simfail rejects a filename" 'echo "$OUT" | grep -q "expects true|false"'

# ---- formal.simfail_run=false writes the .prp but never attempts the VCD sim ------------
W5="$WORK/w5"
$LHD lec --impl "$WORK/impl.prp" --ref "$WORK/ref.prp" --workdir "$W5" --set formal.simfail_run=false >/dev/null 2>&1
ck "simfail_run=false => prp"    '[ -f "$W5/simfail_dut.prp" ]'
ck "simfail_run=false => no vcd" '[ ! -f "$W5/simfail_dut.vcd" ]'

# ---- no --workdir => the feature is off (nothing written / announced) ----------
OUT=$($LHD lec --impl "$WORK/impl.prp" --ref "$WORK/ref.prp" 2>&1)
ck "no --workdir => no testbench" '! echo "$OUT" | grep -q "wrote counterexample testbench"'

# ---- PROVEN (same design both sides) => nothing generated ----------------------
W7="$WORK/w7"
$LHD lec --impl "$WORK/impl.prp" --ref "$WORK/impl.prp" --workdir "$W7" >/dev/null 2>&1
ck "PROVEN => no prp"           '[ ! -f "$W7/simfail_dut.prp" ]'

# ---- hierarchical DUTs: prp_writer re-emits sub-module inputs UNTYPED, so they
# are auto-typed by width (else an internal `mod` boundary won't re-compile). The
# setup-only then exercises the multi-level peek + VCD codegen (shared_ptr fix
# through 2 hierarchy levels) hermetically.
cat > "$WORK/h_impl.prp" <<'EOF'
mod adder(en:bool) -> (o:u8@[0]) {
  reg r:u8 = 0
  o = r
  if en { wrap r += 1 }
}
mod topm(en:bool) -> (o:u8@[0]) {
  o = adder(en=en)
}
EOF
cat > "$WORK/h_ref.prp" <<'EOF'
mod adder(en:bool) -> (o:u8@[0]) {
  reg r:u8 = 0
  o = r
  if en { wrap r += 2 }
}
mod topm(en:bool) -> (o:u8@[0]) {
  o = adder(en=en)
}
EOF
WH="$WORK/wh"
$LHD lec --impl "$WORK/h_impl.prp" --ref "$WORK/h_ref.prp" --impl-top h_impl.topm --ref-top h_ref.topm --workdir "$WH" --set formal.simfail_run=false >/dev/null 2>&1
ck "hier: prp generated"        '[ -f "$WH/simfail_topm.prp" ]'
ck "hier: sub-module input typed" 'grep -Eq "mod adder[^(]*\(en:u" "$WH/simfail_topm.prp"'
SH="$WORK/sh"
$LHD sim "$WH/simfail_topm.prp" --setup-only --set sim.vcd=true --workdir "$SH" >/dev/null 2>&1
ck "hier: testbench sim-valid"  '[ -f "$SH/sim/drv.cpp" ]'

# ---- IMPORT form: when BOTH sides are `pub` .prp with DISTINCT stems, the
# testbench IMPORTS the originals (`import("<stem>.<top>")`) instead of inlining
# renamed copies, so fixing a bug in the original .prp and re-running the SAME
# simfail_dut.prp picks up the fix. The sim is then run with the two sources passed
# POSITIONALLY so the imports resolve to the co-loaded units. ----
cat > "$WORK/pimpl.prp" <<'EOF'
pub mod dut(en:bool) -> (value:u8@[0]) {
  reg count:u8 = 0
  value = count
  if en { wrap count += 1 }
}
EOF
cat > "$WORK/pref.prp" <<'EOF'
pub mod dut(en:bool) -> (value:u8@[0]) {
  reg count:u8 = 0
  value = count
  if en { wrap count += 2 }
}
EOF
WP="$WORK/wp"
$LHD lec --impl "$WORK/pimpl.prp" --ref "$WORK/pref.prp" --workdir "$WP" --set formal.simfail_run=false >/dev/null 2>&1
ck "import: prp generated"       '[ -f "$WP/simfail_dut.prp" ]'
ck "import: imports impl origin" 'grep -q "import(\"pimpl.dut\")" "$WP/simfail_dut.prp"'
ck "import: imports ref origin"  'grep -q "import(\"pref.dut\")" "$WP/simfail_dut.prp"'
ck "import: no inlined copy"     '! grep -q "^pub mod dut" "$WP/simfail_dut.prp" && ! grep -q "lecref_dut" "$WP/simfail_dut.prp"'
# sim-valid ONLY with the sources passed positionally (the imports need them).
SP="$WORK/sp"
$LHD sim "$WORK/pimpl.prp" "$WORK/pref.prp" "$WP/simfail_dut.prp" --setup-only --set sim.vcd=true --workdir "$SP" >/dev/null 2>&1
ck "import: testbench sim-valid" '[ -f "$SP/sim/drv.cpp" ]'

# ---- a COMBINATIONAL side. prp_writer picks the lambda keyword from the body
# (`pub mod` with state, `pub comb` without), so a stateless side re-emits with
# NO `mod` keyword at all — the header parser must still find it, and the
# wrapper must give that side's outputs an explicit `@[]`, because every `mod`
# output declares a landing cycle and a re-emitted comb header carries none.
# Every other pair in this file is mod/mod, which is why neither showed up. ----
cat > "$WORK/qimpl.prp" <<'EOF'
pub mod dut(a:u8) -> (r:u8@[1]) {
  reg q:u8 = 0
  r = q
  q = a
}
EOF
cat > "$WORK/qref.prp" <<'EOF'
pub comb dut(a:u8) -> (r:u8) {
  r = a
}
EOF
WQ="$WORK/wq"
$LHD lec --impl "$WORK/qimpl.prp" --ref "$WORK/qref.prp" --workdir "$WQ" --set formal.simfail_run=false >"$WORK/wq.out" 2>&1
ck "comb: prp generated"        '[ -f "$WQ/simfail_dut.prp" ]'
ck "comb: side was parsed"      '! grep -q "no Pyrope modules were re-emitted" "$WORK/wq.out"'
ck "comb: mod side keeps cycle" 'grep -q "impl_r:u8@\[1\]" "$WQ/simfail_dut.prp"'
ck "comb: comb side gets @[]"   'grep -q "ref_r:u8@\[\]" "$WQ/simfail_dut.prp"'
SQ="$WORK/sq"
$LHD sim "$WORK/qimpl.prp" "$WORK/qref.prp" "$WQ/simfail_dut.prp" --setup-only --set sim.vcd=true --workdir "$SQ" >/dev/null 2>&1
ck "comb: testbench sim-valid"  '[ -f "$SQ/sim/drv.cpp" ]'

# ---- STRUCT ports. A nested port (`io:(valid:u1, bits:(x:u4))`) is ONE decl but
# several scalar leaves, and an instantiation binds it per LEAF (`io.bits.x = e`),
# never as an aggregate. So the wrapper declares one flat scalar per leaf and the
# test pokes those. The impl here also carries a field the ref does NOT
# (`only_impl`), which must appear on the wrapper (impl needs it) and be absent
# from the ref call. Before leaves existed the header split on every comma, so a
# struct port shredded into garbage and no testbench came out at all. ----
cat > "$WORK/simpl.prp" <<'EOF'
pub mod dut(clock:u1, reset:u1, io:(valid:u1, bits:(x:u4, y:u3, only_impl:u2))) -> (o:u8@[]) {
  reg cnt:u8:[reset_pin=ref reset] = 0
  o = cnt
  if io.valid { cnt = (cnt + io.bits.x + io.bits.y + io.bits.only_impl)#[0..=7] }
}
EOF
cat > "$WORK/sref.prp" <<'EOF'
pub mod dut(clock:u1, reset:u1, io:(valid:u1, bits:(x:u4, y:u3))) -> (o:u8@[]) {
  reg cnt:u8:[reset_pin=ref reset] = 0
  o = cnt
  if io.valid { cnt = (cnt + io.bits.x + io.bits.y + 1)#[0..=7] }
}
EOF
WS="$WORK/ws"
$LHD lec --impl "$WORK/simpl.prp" --ref "$WORK/sref.prp" --workdir "$WS" --set formal.simfail_run=false >/dev/null 2>&1
ck "struct: prp generated"        '[ -f "$WS/simfail_dut.prp" ]'
ck "struct: leaves are flat ports" 'grep -q "io__bits__x:u4" "$WS/simfail_dut.prp"'
ck "struct: actuals are per-leaf"  'grep -q "io.bits.x = io__bits__x" "$WS/simfail_dut.prp"'
ck "struct: impl-only leaf on impl" 'grep -q "implmod(.*io.bits.only_impl = io__bits__only_impl" "$WS/simfail_dut.prp"'
ck "struct: impl-only leaf NOT on ref" '! grep -q "refmod(.*only_impl" "$WS/simfail_dut.prp"'
ck "struct: drives each leaf"      'grep -q "_lec_dut.io__bits__y = _drv_io__bits__y\[clock\]" "$WS/simfail_dut.prp"'
SS="$WORK/ss"
$LHD sim "$WORK/simpl.prp" "$WORK/sref.prp" "$WS/simfail_dut.prp" --setup-only --set sim.vcd=true --workdir "$SS" >/dev/null 2>&1
ck "struct: testbench sim-valid"   '[ -f "$SS/sim/drv.cpp" ]'

# ---- a construct pass.prp_writer cannot EMIT must not cost the testbench. The
# import form references the original .prp verbatim, so it needs the two HEADERS
# and nothing else; re-emitting a Pyrope side through the writer was pure cost
# that could fail for a reason the LEC verdict does not care about (here
# `popcount`, `#+[..]`), and took the whole counterexample down with it. ----
sed 's|if io.valid {|const pc:u4 = io.bits.x#+[..]\n  if io.valid {|; s|cnt + io.bits.x|cnt + pc|' \
    "$WORK/simpl.prp" > "$WORK/pcimpl.prp"
sed 's|if io.valid {|const pc:u4 = io.bits.x#+[..]\n  if io.valid {|; s|cnt + io.bits.x|cnt + pc|' \
    "$WORK/sref.prp"  > "$WORK/pcref.prp"
ck "popcount: writer really refuses it" \
   '! $LHD compile "$WORK/pcimpl.prp" --emit-dir "pyrope:$WORK/pcw_out" --workdir "$WORK/pcw" >/dev/null 2>&1'
WC="$WORK/wc"
$LHD lec --impl "$WORK/pcimpl.prp" --ref "$WORK/pcref.prp" --workdir "$WC" --set formal.simfail_run=false >/dev/null 2>&1
ck "popcount: prp still generated" '[ -f "$WC/simfail_dut.prp" ]'
ck "popcount: no writer round trip" '[ ! -d "$WC/lecfail_impl_prp" ] && [ ! -d "$WC/lecfail_ref_prp" ]'
SC="$WORK/sc"
$LHD sim "$WORK/pcimpl.prp" "$WORK/pcref.prp" "$WC/simfail_dut.prp" --setup-only --set sim.vcd=true --workdir "$SC" >/dev/null 2>&1
ck "popcount: testbench sim-valid" '[ -f "$SC/sim/drv.cpp" ]'

if [ $fail -ne 0 ]; then echo "lec_witness_prpfail_test: FAILED"; exit 1; fi
echo "lec_witness_prpfail_test: PASSED"
exit 0
