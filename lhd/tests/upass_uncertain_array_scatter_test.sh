#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Regression for upass.constprop's scatter_positional_array bypassing the
# uncertain-arm bookkeeping.
#
# A whole-variable store whose RHS folds to a CONSTANT and whose destination is
# an array-shaped symbol-table bundle is scattered lane-by-lane straight into
# the bundle (scatter_positional_array), never through Symbol_table::set. That
# is the ONLY whole-variable write path that skips
# record_uncertain_modification, so when it runs inside a runtime `if` arm
# Symbol_table::leave_scope has nothing to invalidate: the lanes keep the arm's
# constants after the arm closes and the next WHOLE read of the array folds to
# them, as if the arm had certainly executed.
#
# The shape below is minion's vpu_tensorfma (lhdsuite), reduced:
#
#     v = q;                                  // array-shaped seed
#     if (c) v[i][ib] = 1'b1;                 // runtime rhs -> no scatter, records
#     if (d) v = {N/2{2'b0}};                 // const rhs  -> SCATTER, did not record
#     v[j][jb] = 1'b0;                        // whole read of v as the RMW BASE
#
# The last statement's read-modify-write BASE lowered to the literal `0`, so the
# emitted Pyrope cleared the WHOLE 8-bit vector where the SystemVerilog clears
# ONE bit. `lhd lec` REFUTED vpu_tensorfma against its own Verilog because of it.
#
# The `{N/2{2'b0}}` count must come from a PACKAGE parameter: with
# compile.slang.preserve_param_provenance ON (which the kernel turns on for
# exactly the `--emit-dir pyrope:` regeneration flow) the replication is lowered
# structurally instead of being folded in the front end, so constprop is the one
# that folds it and the store's RHS reaches process_assign as a ref rather than
# a const. A plain `{4{2'b0}}` folds in slang and takes the recording path.
#
# Two gates: (1) the emitted Pyrope must name the accumulator as the RMW base,
# never a literal, and (2) the generated Pyrope must cvc5-PROVE equivalent to
# the SystemVerilog it came from.

set -u
LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_uncertain_scatter_$$}"
mkdir -p "$W"
fail() { echo "FAIL: $*" >&2; exit 1; }

cat >"$W/uarr.sv" <<'EOF'
package uarr_pkg;
  localparam int unsigned N = 8;
endpackage
module uarr(
  input  logic       clk,
  input  logic       c,
  input  logic       d,
  input  logic [1:0] i,
  input  logic       ib,
  input  logic [1:0] j,
  input  logic       jb,
  input  logic [1:0] rp,
  output logic [7:0] o,
  output logic       any
);
  logic [(uarr_pkg::N/2)-1:0][1:0] v;
  logic [(uarr_pkg::N/2)-1:0][1:0] q;
  always_comb begin
    v = q;
    if (c) v[i][ib] = 1'b1;
    if (d) v = {uarr_pkg::N/2{2'b0}};
    v[j][jb] = 1'b0;
  end
  always_ff @(posedge clk) q <= v;
  // A runtime-indexed element read keeps `q` (and, through `v = q`, `v`)
  // array-shaped in the symbol table -- that is what makes the destination a
  // positional-array bundle and routes the const store into the scatter path.
  assign any = |q[rp];
  assign o   = v;
endmodule
EOF

"$LHD" compile verilog --top uarr --emit-dir "pyrope:$W/prp" --workdir "$W/cw" -q "$W/uarr.sv" >/dev/null 2>&1 \
  || fail "slang compile of the uncertain-arm array design failed"
PRP="$W/prp/uarr.prp"
[ -f "$PRP" ] || fail "no generated pyrope at $PRP"

# (1) The read-modify-write base must be the accumulator NAME. `((0 & (~(1 <<`
#     is the poisoned spelling: the whole expression folds to 0 and the store
#     wipes every lane the arm did not write.
if grep -q '((0 & (~(1 <<' "$PRP"; then
  fail "the RMW base folded to the constant 0 -- a conditional whole-array store leaked past its arm:
$(cat "$PRP")"
fi
grep -q 'v = ((v & (~(1 <<' "$PRP" \
  || fail "expected the RMW to read back the accumulator 'v'; got:
$(cat "$PRP")"
echo "PASS: a conditional whole-array store does not fold a later read of the array"

# (2) Semantic gate: the generated Pyrope must PROVE equivalent to its source.
"$LHD" lec --top uarr --ref "$W/uarr.sv" --impl "$W/prp/uarr.prp" \
  --workdir "$W/lec" -q --result-json "$W/lec.json" \
  || fail "generated pyrope did not PROVE equivalent to the source SystemVerilog: $(cat "$W/lec.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/lec.json" || fail "lec not pass: $(cat "$W/lec.json")"
echo "PASS: generated pyrope is cvc5-PROVEN equivalent to the SystemVerilog"

echo "PASS: uncertain-arm array scatter invalidates on arm exit"
