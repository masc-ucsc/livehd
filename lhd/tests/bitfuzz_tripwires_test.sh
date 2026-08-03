#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# pass.bitfuzz TRIPWIRES: for each design below, compile twice -- once plain,
# once with `--set compile.bitfuzz.mode=wires` (strip every combinational
# per-pin bits/pin_signed annotation and make pass.bitwidth re-infer them) --
# and require `lhd lec` to PROVE the two netlists equivalent.
#
# The invariant under test is the LGraph width contract itself: cells are
# unlimited-width and always signed, `bits`/`pin_signed` are DERIVED metadata,
# and any semantically load-bearing narrowing must be an explicit cell
# (Get_mask/Set_mask/Sext), never the annotation. If stripping the annotation
# changes what a design MEANS, some stage gave it semantic weight -- and
# because `bw_pass` skips every already-sized pin while every front end stamps
# widths, the re-inference paths this exercises run in NO other CLI flow.
#
# Each design pins a specific defect class found (and fixed) by the first
# bitfuzz sweeps; see todo_bitfuzz.md for the full write-ups:
#   trivial_if          IO seeding: sign must come from the decl, not the pin
#   tup_in_port         a port's declared bits is the LITERAL bus width
#   comb_single_out_op  Get_mask all-ones worst-case probe (negative mask)
#   gen_type_cast       same probe, through a runtime type cast
#   rt_typecast         Mux selector envelope must be [0..n-1], never negative
#   match_no_else       booleans are the unsigned {0,1}, or one-hot `match`
#                       selectors assemble wrong and pass.formal refutes them
#   packed_assign       variable-amount SRA takes the four-corner envelope
#   sext_scalar_net     LEC encoder: a Sum of all-signed operands is signed
#   signed_shift_widen  LEC encoder: a shift's sign is its LEFT operand's
#   comb_array_const_index_read
#                       tolg spells LOGICAL negation as EQ-against-0, never a
#                       bitwise Not with a truncating 1-bit stamp (~bool is
#                       {-1,-2}, never zero -- an update_enable so annotated
#                       wrote a memory on every cycle once the stamp was gone)
#
# The last two are FALSE-refutation guards: both netlists are equivalent (the
# fuzzed side simply carries the accurate sign) and the encoder must model the
# stamped-unsigned baseline correctly or an equivalent pair refutes.

set -u

LHD="${LHD:-lhd/lhd}"
EQUIV="${EQUIV_DIR:-inou/prp/tests/equiv}"
W="${TEST_TMPDIR:-/tmp/lhd_bitfuzz_tripwires_$$}"
mkdir -p "$W"

fail() { echo "FAIL: $*" >&2; exit 1; }

[ -x "$LHD" ] || fail "lhd binary not found at $LHD"

DESIGNS="trivial_if tup_in_port comb_single_out_op gen_type_cast rt_typecast match_no_else packed_assign sext_scalar_net signed_shift_widen comb_array_const_index_read"

for name in $DESIGNS; do
  src="$EQUIV/$name.v"
  [ -f "$src" ] || fail "$name: missing source $src"
  d="$W/$name"
  mkdir -p "$d"

  "$LHD" compile "$src" --recipe O2 --workdir "$d/wref" --emit-dir "verilog:$d/ref/" \
    >"$d/ref.log" 2>&1 || fail "$name: baseline compile failed (see $d/ref.log)"

  "$LHD" compile "$src" --recipe O2 --workdir "$d/wbf" --emit-dir "verilog:$d/bf/" \
    --set compile.bitfuzz.mode=wires >"$d/bf.log" 2>&1 \
    || fail "$name: bitfuzz compile failed (see $d/bf.log)"

  # The whole test is vacuous if the pass silently did not run (an option
  # rename, a recipe change): require its summary record in the diagnostics.
  grep -q "bitfuzz-summary" "$d/bf.log" || fail "$name: pass.bitfuzz did not run (no bitfuzz-summary in $d/bf.log)"

  for ref in "$d"/ref/*.v; do
    base="$(basename "$ref")"
    impl="$d/bf/$base"
    [ -f "$impl" ] || fail "$name: fuzzed netlist missing $base"
    # Module name: first module header in the reference emission.
    top="$(grep -m1 -oE '^module +\\?[A-Za-z_][^ (;]*' "$ref" | sed 's/module *//; s/^\\//')"
    [ -n "$top" ] || fail "$name: could not find module name in $ref"
    if ! "$LHD" lec --impl "verilog:$impl" --ref "verilog:$ref" --top "$top" \
        --workdir "$d/lec_$top" >"$d/lec_$top.log" 2>&1; then
      grep -E "counterexample|REFUTED|error" "$d/lec_$top.log" | head -5 >&2
      fail "$name: fuzzed netlist not PROVEN equivalent to baseline for $top (see $d/lec_$top.log)"
    fi
  done
  echo "ok: $name survives annotation stripping (all modules PROVEN)"
done

echo "PASS: bitfuzz tripwires -- annotation stripping is semantics-preserving on every pinned design"
