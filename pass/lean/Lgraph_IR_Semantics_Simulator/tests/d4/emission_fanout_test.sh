#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `emission_fanout.py` exists to answer "is this changed register live", and the
# ONE property that makes its answer worth anything is that a DEAD verdict
# cannot come from a parse miss. It broke that on the first form review tried:
#
#     assign out = seed;
#
# was parsed with an LHS of "assign out", a net nothing else mentions, so the
# module's real OUTPUT was never reached and a live seed came back
# `{"unparsed": 0, "seeds": {"seed": ["DEAD"]}}`.
#
# So every ACCEPTED assignment form is checked to preserve the actual left-hand
# side, and every form the reader does not support is checked to come back
# UNPARSED -- never DEAD.
set -u

TOOL="pass/lean/Lgraph_IR_Semantics_Simulator/tests/d4/emission_fanout.py"
for c in "$TOOL" "$(dirname "$0")/emission_fanout.py" "${TEST_SRCDIR:-}/_main/$TOOL"; do
  [ -n "$c" ] && [ -r "$c" ] && TOOL="$c" && break
done
[ -r "$TOOL" ] || { echo "FAIL: cannot find emission_fanout.py (looked under $(pwd))"; exit 1; }
PY="${PYTHON:-python3}"

W="$(mktemp -d "${TEST_TMPDIR:-${TMPDIR:-/tmp}}/fanout.XXXXXX")"
trap 'rm -rf "$W"' EXIT
fails=0
ok()  { echo "ok: $*"; }
bad() { echo "CONTROL FAILED: $*"; fails=$((fails+1)); }

# <name> <body-lines> -> writes $W/<name>.v around a standard header
mod() {
  local n="$1"; shift
  { echo "module $n("
    echo "   input signed clkx"
    echo "  ,input signed din"
    echo "  ,output reg outp"
    echo ");"
    echo "reg seed;"
    echo "reg ___next_seed;"
    echo "reg other;"
    echo "reg ___next_other;"
    echo "reg [3:0] wide;"
    echo "always_comb begin"
    echo "  ___next_seed = din;"
    echo "end"
    printf '%s\n' "$@"
    echo "always @(posedge clkx ) begin"
    echo "seed <= ___next_seed;"
    echo "end"
    echo "endmodule"
  } > "$W/$n.v"
}

# expect <name> <seed> <wanted>   -- wanted is the exact JSON fragment
expect() {
  local n="$1" sd="$2" want="$3" out
  out="$("$PY" "$TOOL" "$W/$n.v" "$sd" 2>&1)"
  if grep -qF "$want" <<<"$out"; then
    ok "$n: $want"
  else
    echo "    got: $out"
    bad "$n: expected $want"
  fi
}

# An UNPARSED module must also EXIT NONZERO and must never say DEAD. A caller
# that only reads the exit code, and a caller that only reads the JSON, must
# both come away knowing no verdict was reached.
expect_unparsed() {
  local n="$1" sd="$2" out rc
  out="$("$PY" "$TOOL" "$W/$n.v" "$sd" 2>&1)"; rc=$?
  if ! grep -qF '"verdict": "UNPARSED"' <<<"$out"; then
    echo "    got: $out"; bad "$n: expected UNPARSED"; return
  fi
  if grep -qF 'DEAD' <<<"$out"; then
    echo "    got: $out"; bad "$n: reported DEAD for a module it could not parse"; return
  fi
  if [ "$rc" -eq 0 ]; then
    bad "$n: UNPARSED but exited 0"; return
  fi
  ok "$n: UNPARSED, exit $rc, no DEAD"
}

# ---- SUPPORTED forms must preserve the real LHS -----------------------------
mod a_assign      'assign outp = seed;'
expect a_assign seed '"seed": ["OUTPUT"]'

mod a_plain       'always_comb begin' '  outp = seed;' 'end'
expect a_plain seed '"seed": ["OUTPUT"]'

mod a_bitsel      'always_comb begin' '  wide[2] = seed;' 'end' 'assign outp = wide[2];'
expect a_bitsel seed '"seed": ["OUTPUT"]'

mod a_partsel     'always_comb begin' '  wide[3:2] = {seed,seed};' 'end' 'assign outp = wide[3];'
expect a_partsel seed '"seed": ["OUTPUT"]'

mod a_assign_sel  'assign outp = wide[0];' 'always_comb begin' '  wide[0] = seed;' 'end'
expect a_assign_sel seed '"seed": ["OUTPUT"]'

mod a_nonblocking 'always @(posedge clkx ) begin' 'other <= seed;' 'end'
expect a_nonblocking seed '"seed": ["STATE"]'

mod a_escaped     'reg \esc.name ;' 'always_comb begin' '  \esc.name  = seed;' 'end' 'assign outp = \esc.name ;'
expect a_escaped seed '"seed": ["OUTPUT"]'

mod a_escaped_sel 'reg [3:0] \esc.wide ;' 'always_comb begin' '  \esc.wide [1] = seed;' 'end' 'assign outp = \esc.wide [1];'
expect a_escaped_sel seed '"seed": ["OUTPUT"]'

# A genuinely dead seed still reads DEAD -- the rule is "no DEAD from a parse
# miss", not "never DEAD".
mod a_dead        'assign outp = din;'
expect a_dead seed '"seed": ["DEAD"]'

# ---- UNSUPPORTED forms must report UNPARSED, never DEAD ---------------------
mod u_concat      'always_comb begin' '  {outp, other} = {seed, seed};' 'end'
expect_unparsed u_concat seed

mod u_indexed     'always_comb begin' '  wide[din +: 2] = {seed,seed};' 'end' 'assign outp = wide[0];'
expect_unparsed u_indexed seed

mod u_gibberish   'this is not an assignment or anything else'
expect_unparsed u_gibberish seed

# THE DECLARATION INITIALIZER. `wire fwd = seed;` is legal Verilog and a real
# connectivity edge. The declaration branch used to swallow the whole line as
# "a declaration", record `seed` as a declared NAME, and never write the edge --
# so a seed live at the module OUTPUT came back
# `{"unparsed": 0, "seeds": {"seed": ["DEAD"]}}` with exit 0. It must never say
# DEAD here: either the form is supported and the answer is OUTPUT, or it is
# refused.
mod u_declinit    'wire forwarded = seed;' 'assign outp = forwarded;'
expect_unparsed u_declinit seed

# ...and the same connectivity written as a bare declaration plus an assign IS
# supported, so the refusal above is about the FORM and not about the shape.
mod a_declthenassign 'wire forwarded;' 'assign forwarded = seed;' 'assign outp = forwarded;'
expect a_declthenassign seed '"seed": ["OUTPUT"]'

# AN INLINE `always` BODY. `always_comb outp = seed;` is legal SystemVerilog and
# cgen emits exactly that form inside the memory wrapper. The reader used to
# accept any line beginning `always` and `continue`, so the statement vanished
# and a seed driving the module OUTPUT came back
# `{"unparsed": 0, "seeds": {"seed": ["DEAD"]}}` with exit 0.
mod u_alwaysinline 'always_comb outp = seed;'
expect_unparsed u_alwaysinline seed
mod u_alwaysffinline 'always_ff @(posedge clkx) outp <= seed;'
expect_unparsed u_alwaysffinline seed

# ...while every opener cgen actually emits is still accepted.
mod a_alwayscomb  'always_comb begin' '  outp = seed;' 'end'
expect a_alwayscomb seed '"seed": ["OUTPUT"]'
mod a_alwaysff    'always_ff @(posedge clkx ) begin' 'outp <= seed;' 'end'
expect a_alwaysff seed '"seed": ["STATE"]'
mod a_alwaysreset 'always @(posedge clkx or negedge din ) begin' 'outp <= seed;' 'end'
expect a_alwaysreset seed '"seed": ["STATE"]'
mod a_alwayslatch 'always_latch begin' '  outp = seed;' 'end'
expect a_alwayslatch seed '"seed": ["OUTPUT"]'

# MALFORMED DECLARATIONS poison the module rather than falling through the
# declaration branch's unconditional `continue`.
mod u_declnoname  'reg [3:0];'
expect_unparsed u_declnoname seed
mod u_decllist    'reg lista, listb;'
expect_unparsed u_decllist seed

echo
if [ "$fails" -eq 0 ]; then
  echo "PASS: emission_fanout_test"
else
  echo "FAIL: $fails control(s)"
fi
exit $(( fails > 0 ))
