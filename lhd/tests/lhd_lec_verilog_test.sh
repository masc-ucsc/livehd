#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `lhd lec` is the single logic-equivalence command. The former `lhd check`
# (yosys/lgcheck) is now the `--set formal.solver=lgyosys` backend, and lec accepts
# verilog inputs directly: a .v/.sv side elaborates through the default `slang`
# reader (the direct SV->LNAST front-end) or, with --reader yosys-*, the yosys
# front-end. The --set formal.solver knob selects cvc5 (default) / bitwuzla /
# lgyosys. Fixtures: the committed inou/prp/tests/equiv trivial_if pyrope/verilog
# golden pair, plus the merge_demo inverter (a plainly-named module).

set -u
LHD=lhd/lhd
PRP=inou/prp/tests/equiv/trivial_if.prp
V0=inou/prp/tests/equiv/trivial_if.v
INV=lhd/tests/merge_demo/inv.v
TOP='trivial_if.fun3'
W="${TEST_TMPDIR:-/tmp/lhd_lec_verilog_$$}"
mkdir -p "$W"

fail() { echo "FAIL: $*" >&2; exit 1; }

# 1. Headline case: a Pyrope impl vs a Verilog reference, discharged in-process
#    with cvc5 (the default). The Verilog side elaborates through slang.
"$LHD" lec --impl "$PRP" --ref "$V0" --top "$TOP" --workdir "$W/c1" -q --result-json "$W/r1.json" \
  || fail "lec prp vs verilog (cvc5) not pass: $(cat "$W/r1.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/r1.json" || fail "lec cvc5 not pass: $(cat "$W/r1.json")"
echo "PASS: lec prp vs verilog (default cvc5, slang reader)"

# 2. The same pair through the lgyosys backend (the former `lhd check`).
"$LHD" lec --impl "$PRP" --ref "$V0" --top "$TOP" --set formal.solver=lgyosys \
  --workdir "$W/c2" -q --result-json "$W/r2.json" \
  || fail "lec prp vs verilog (lgyosys) not pass: $(cat "$W/r2.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/r2.json" || fail "lec lgyosys not pass: $(cat "$W/r2.json")"
echo "PASS: lec prp vs verilog (--set formal.solver=lgyosys)"

# The yosys-slang reference reader is staged in a sibling external-repository
# runfiles directory. Exercise that exact lookup: large V2V tests use it for
# packed-struct SystemVerilog references and must not depend on the caller cwd.
"$LHD" lec --impl "$INV" --ref "$INV" --top inv --set formal.solver=lgyosys \
  --set formal.lec.gold_reader=slang --workdir "$W/c2_slang" -q --result-json "$W/r2_slang.json" \
  || fail "lec lgyosys slang reader not pass: $(cat "$W/r2_slang.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/r2_slang.json" \
  || fail "lec lgyosys slang reader not pass: $(cat "$W/r2_slang.json")"
echo "PASS: lgyosys locates the yosys-slang plugin in runfiles"

# The generated/implementation side can independently use yosys-slang. This is
# the scalable path for large cgen Verilog (Minion/XiangShan), while keeping the
# legacy read_verilog reader as the default for compatibility.
"$LHD" lec --impl "$INV" --ref "$INV" --top inv --set formal.solver=lgyosys \
  --set formal.lec.gate_reader=slang --workdir "$W/c2_gate_slang" -q --result-json "$W/r2_gate_slang.json" \
  || fail "lec lgyosys gate slang reader not pass: $(cat "$W/r2_gate_slang.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/r2_gate_slang.json" \
  || fail "lec lgyosys gate slang reader not pass: $(cat "$W/r2_gate_slang.json")"
echo "PASS: lgyosys gate-side yosys-slang reader"

cat >"$W/relaxed_ref.sv" <<'SV'
module relaxed_ref(input logic a, output logic y);
  typedef enum logic { E0, E1 } mode_t;
  mode_t unused_mode;
  assign y = declared_later;
  assign unused_mode = a;
  logic declared_later;
  assign declared_later = a;
endmodule
SV
cat >"$W/relaxed_impl.v" <<'V'
module relaxed_ref(input a, output y);
  assign y = a;
endmodule
V
"$LHD" lec --impl "$W/relaxed_impl.v" --ref "$W/relaxed_ref.sv" --top relaxed_ref \
  --set formal.solver=lgyosys --set formal.lec.gold_reader=slang \
  --workdir "$W/c2_relaxed" -q --result-json "$W/r2_relaxed.json" \
  || fail "lec lgyosys relaxed slang source not pass: $(cat "$W/r2_relaxed.json" 2>/dev/null)"
echo "PASS: lgyosys slang reader accepts project enum and declaration-order idioms"

# A reader/top/setup failure has no equivalence evidence. Keep it a dependency
# error, never a synthetic REFUTED verdict (only a bounded CEX may use that).
if "$LHD" lec --impl "$INV" --ref "$INV" --top no_such_top --set formal.solver=lgyosys \
  --workdir "$W/c2_setup_fail" -q --result-json "$W/r2_setup_fail.json"; then
  fail "lgyosys accepted a missing top"
fi
grep -q '"class":"dependency"' "$W/r2_setup_fail.json" \
  || fail "lgyosys setup failure was not classified as dependency: $(cat "$W/r2_setup_fail.json")"
grep -q 'REFUTED' "$W/r2_setup_fail.json" \
  && fail "lgyosys setup failure was mislabeled REFUTED: $(cat "$W/r2_setup_fail.json")"
echo "PASS: lgyosys setup failures stay distinct from refutations"

# 3. Bare .v paths on BOTH sides: the verilog kind is inferred from the
#    extension; an identical netlist is trivially PROVEN (in-process cvc5).
"$LHD" lec --impl "$INV" --ref "$INV" --top inv --workdir "$W/c3" -q --result-json "$W/r3.json" \
  || fail "lec verilog identity (cvc5) not pass: $(cat "$W/r3.json" 2>/dev/null)"
echo "PASS: lec verilog vs verilog (bare .v, kind inferred, slang)"

# 4. --reader yosys-verilog override: the verilog side elaborates through the
#    yosys front-end instead of slang (same design => still PROVEN).
"$LHD" lec --impl "$INV" --ref "$INV" --top inv --reader yosys-verilog \
  --workdir "$W/c4" -q --result-json "$W/r4.json" \
  || fail "lec verilog (--reader yosys-verilog) not pass: $(cat "$W/r4.json" 2>/dev/null)"
echo "PASS: lec verilog vs verilog (--reader yosys-verilog override)"

# 5. The retired `check` command points at the merged `lec`.
out=$("$LHD" check --impl "$V0" --ref "$V0" 2>/dev/null)
echo "$out" | grep -q '"status":"fail"' || fail "check should fail (merged into lec): $out"
echo "$out" | grep -q 'merged into' || fail "check error lacks migration hint: $out"
echo "PASS: check command rejected with lec migration hint"

# 6. An unknown solver is a usage error, never a silent fallthrough.
out=$("$LHD" lec --impl "$PRP" --ref "$V0" --top "$TOP" --set formal.solver=foo -q 2>/dev/null)
echo "$out" | grep -q '"status":"fail"' || fail "bad solver should fail: $out"
echo "$out" | grep -q 'cvc5|bitwuzla|lgyosys' || fail "bad solver error lacks the valid set: $out"
echo "PASS: formal.solver=foo rejected"

echo "ALL PASS: lhd lec verilog inputs + solver backends"
