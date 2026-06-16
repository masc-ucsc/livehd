#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `lhd lec` is the single logic-equivalence command. The former `lhd check`
# (yosys/lgcheck) is now the `--set lec.solver=lgyosys` backend, and lec accepts
# verilog inputs directly: a .v/.sv side elaborates through the default `slang`
# reader (the direct SV->LNAST front-end) or, with --reader yosys-*, the yosys
# front-end. The --set lec.solver knob selects cvc5 (default) / bitwuzla /
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
"$LHD" lec --impl "$PRP" --ref "$V0" --top "$TOP" --set lec.solver=lgyosys \
  --workdir "$W/c2" -q --result-json "$W/r2.json" \
  || fail "lec prp vs verilog (lgyosys) not pass: $(cat "$W/r2.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/r2.json" || fail "lec lgyosys not pass: $(cat "$W/r2.json")"
echo "PASS: lec prp vs verilog (--set lec.solver=lgyosys)"

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
out=$("$LHD" lec --impl "$PRP" --ref "$V0" --top "$TOP" --set lec.solver=foo -q 2>/dev/null)
echo "$out" | grep -q '"status":"fail"' || fail "bad solver should fail: $out"
echo "$out" | grep -q 'cvc5|bitwuzla|lgyosys' || fail "bad solver error lacks the valid set: $out"
echo "PASS: lec.solver=foo rejected"

echo "ALL PASS: lhd lec verilog inputs + solver backends"
