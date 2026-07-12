#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# lecfail witness reproduction (`lhd lec` + --workdir): on a REFUTED verdict the
# CLI writes a self-contained Pyrope testbench (lec.prpfail, default lecfail.prp)
# that instantiates BOTH designs inside one wrapper, drives the counterexample
# input sequence, and (lec.prpfailrun) runs `lhd sim` to dump ONE VCD showing the
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

# ---- default: --workdir set => lecfail.prp written (VCD is best-effort). The lec
# run must still exit REFUTED even if the reproduction sim can't host-compile. ----
W1="$WORK/w1"
OUT=$($LHD lec --impl "$WORK/impl.prp" --ref "$WORK/ref.prp" --workdir "$W1" 2>&1)
ck "REFUTED"                    'echo "$OUT" | grep -q REFUTED'
ck "wrote lecfail.prp"          '[ -f "$W1/lecfail.prp" ]'
ck "testbench has both modules" 'grep -q "mod dut" "$W1/lecfail.prp" && grep -q "lecref_dut" "$W1/lecfail.prp"'
ck "testbench has the wrapper"  'grep -q "__lecfail_dut_pair" "$W1/lecfail.prp"'
ck "testbench has a test block" 'grep -q "^test lecfail" "$W1/lecfail.prp"'
ck "testbench exposes impl out" 'grep -q "impl_value" "$W1/lecfail.prp"'
ck "testbench exposes ref out"  'grep -q "ref_value" "$W1/lecfail.prp"'
ck "testbench drives the seq"   'grep -q "const _drv_en = \[" "$W1/lecfail.prp"'
ck "testbench drives reset"     'grep -q "_lec_dut.reset" "$W1/lecfail.prp"'

# ---- F7: source-mapped root cut + machine-readable lecfail.json -----------------
# The first diverging STATE cut (the flop the diverging output inherits) is named
# with its impl-side declaration file:line, both in the .prp header and in a sibling
# lecfail.json whose input sequence matches the .prp _drv_* arrays by construction.
ck "prp header has source-mapped root cut" 'grep -Eq "// Root cut:.*impl\.prp:[0-9]+" "$W1/lecfail.prp"'
ck "wrote lecfail.json"                    '[ -f "$W1/lecfail.json" ]'
ck "lecfail.json parses"                   'python3 -m json.tool "$W1/lecfail.json" >/dev/null'
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
ck "json root_cut + input sequence match the .prp" 'python3 "$WORK/chk.py" "$W1/lecfail.prp" "$W1/lecfail.json" | grep -q ok'

# ---- the generated testbench must be SIM-VALID: `lhd sim --setup-only` runs the
# whole front-end (inou.prp -> upass -> tolg -> cgen_sim) without host-compiling,
# so it catches any codegen regression in the generated wrapper/test hermetically.
S1="$WORK/s1"
$LHD sim "$W1/lecfail.prp" --setup-only --set sim.vcd=true --workdir "$S1" >/dev/null 2>&1
ck "generated testbench sim-valid" '[ -f "$S1/sim/drv.cpp" ]'
# The hierarchical peek must snapshot sub-instances by value, so the DUT struct
# stays copyable under VCD: __vcd is a shared_ptr, not unique_ptr (the cgen_sim fix).
ck "VCD __vcd is copyable (shared_ptr)" '! grep -rq "unique_ptr<vcd::VCDWriter>" "$S1/sim"/ && grep -rq "shared_ptr<vcd::VCDWriter>" "$S1/sim"/'

# ---- lec.witness=false disables the whole feature ------------------------------
W2="$WORK/w2"
$LHD lec --impl "$WORK/impl.prp" --ref "$WORK/ref.prp" --workdir "$W2" --set lec.witness=false >/dev/null 2>&1
ck "witness=false => no prp"    '[ ! -f "$W2/lecfail.prp" ]'

# ---- lec.prpfail=false disables generation -------------------------------------
W3="$WORK/w3"
$LHD lec --impl "$WORK/impl.prp" --ref "$WORK/ref.prp" --workdir "$W3" --set lec.prpfail=false >/dev/null 2>&1
ck "prpfail=false => no prp"    '[ ! -f "$W3/lecfail.prp" ]'

# ---- lec.prpfail=NAME builds NAME.prp (custom basename under --workdir) --------
W4="$WORK/w4"
$LHD lec --impl "$WORK/impl.prp" --ref "$WORK/ref.prp" --workdir "$W4" --set lec.prpfail=mycex.prp --set lec.prpfailrun=false >/dev/null 2>&1
ck "custom prpfail name"        '[ -f "$W4/mycex.prp" ]'
ck "no default lecfail.prp"     '[ ! -f "$W4/lecfail.prp" ]'

# ---- lec.prpfailrun=false writes the .prp but never attempts the VCD sim -------
W5="$WORK/w5"
$LHD lec --impl "$WORK/impl.prp" --ref "$WORK/ref.prp" --workdir "$W5" --set lec.prpfailrun=false >/dev/null 2>&1
ck "prpfailrun=false => prp"    '[ -f "$W5/lecfail.prp" ]'
ck "prpfailrun=false => no vcd" '[ ! -f "$W5/lecfail.vcd" ]'

# ---- no --workdir => the feature is off (nothing written / announced) ----------
OUT=$($LHD lec --impl "$WORK/impl.prp" --ref "$WORK/ref.prp" 2>&1)
ck "no --workdir => no testbench" '! echo "$OUT" | grep -q "wrote counterexample testbench"'

# ---- PROVEN (same design both sides) => nothing generated ----------------------
W7="$WORK/w7"
$LHD lec --impl "$WORK/impl.prp" --ref "$WORK/impl.prp" --workdir "$W7" >/dev/null 2>&1
ck "PROVEN => no prp"           '[ ! -f "$W7/lecfail.prp" ]'

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
$LHD lec --impl "$WORK/h_impl.prp" --ref "$WORK/h_ref.prp" --impl-top h_impl.topm --ref-top h_ref.topm --workdir "$WH" --set lec.prpfailrun=false >/dev/null 2>&1
ck "hier: prp generated"        '[ -f "$WH/lecfail.prp" ]'
ck "hier: sub-module input typed" 'grep -Eq "mod adder[^(]*\(en:u" "$WH/lecfail.prp"'
SH="$WORK/sh"
$LHD sim "$WH/lecfail.prp" --setup-only --set sim.vcd=true --workdir "$SH" >/dev/null 2>&1
ck "hier: testbench sim-valid"  '[ -f "$SH/sim/drv.cpp" ]'

# ---- IMPORT form: when BOTH sides are `pub` .prp with DISTINCT stems, the
# testbench IMPORTS the originals (`import("<stem>.<top>")`) instead of inlining
# renamed copies, so fixing a bug in the original .prp and re-running the SAME
# lecfail.prp picks up the fix. The sim is then run with the two sources passed
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
$LHD lec --impl "$WORK/pimpl.prp" --ref "$WORK/pref.prp" --workdir "$WP" --set lec.prpfailrun=false >/dev/null 2>&1
ck "import: prp generated"       '[ -f "$WP/lecfail.prp" ]'
ck "import: imports impl origin" 'grep -q "import(\"pimpl.dut\")" "$WP/lecfail.prp"'
ck "import: imports ref origin"  'grep -q "import(\"pref.dut\")" "$WP/lecfail.prp"'
ck "import: no inlined copy"     '! grep -q "^pub mod dut" "$WP/lecfail.prp" && ! grep -q "lecref_dut" "$WP/lecfail.prp"'
# sim-valid ONLY with the sources passed positionally (the imports need them).
SP="$WORK/sp"
$LHD sim "$WORK/pimpl.prp" "$WORK/pref.prp" "$WP/lecfail.prp" --setup-only --set sim.vcd=true --workdir "$SP" >/dev/null 2>&1
ck "import: testbench sim-valid" '[ -f "$SP/sim/drv.cpp" ]'

if [ $fail -ne 0 ]; then echo "lec_witness_prpfail_test: FAILED"; exit 1; fi
echo "lec_witness_prpfail_test: PASSED"
exit 0
