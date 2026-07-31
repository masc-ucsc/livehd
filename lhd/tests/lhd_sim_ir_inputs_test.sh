#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `lhd sim` takes ln:/lg: IR inputs, exactly as `lhd compile` does — so a design
# compiled ONCE simulates without re-reading its sources:
#
#   lhd compile dut.prp --emit-dir lg:L        # (or --emit-dir ln:LN)
#   lhd sim lg:L tb.prp                        # tb: const c = import("lg:dut.cnt")
#
# Four things this pins down.
#  1. ROUTING. `sim` classifies a positional by extension, so before the shared
#     route_positional an `lg:DIR` token was silently taken as the TEST NAME and
#     the library never loaded — the run then died on the testbench's unresolved
#     import, pointing at the wrong thing entirely. `--in lg:DIR` is the same
#     input under a different spelling, and must agree.
#  2. lg: IS THE DESIGN, not just a name table. `import("lg:x")` lowers to a
#     black-box Sub; unless the absorbed library's graphs join the design, the
#     emit sees only the testbench side and `sim` fails with "no synthesizable
#     modules to emit as sim:" — the DUT body being nowhere.
#  3. AN ARTIFACT IMPORT IS EXACT. `import("lg:X")` names a graph, so if X is not
#     among the simulated modules the testbench is wrong. The binder used to fall
#     back to "the sole module" / "the unique root", which meant a typo simulated
#     a DIFFERENT module and exited 0 with no diagnostics.
#  4. A slang `--top X --emit-dir ln:` publishes the WHOLE elaborated forest.
#     It used to filter to the one unit named X, dropping every module X
#     instantiates, so the ln: dir could not be linked back ("call to undefined
#     function '<child>'") — which made ln: useless as a sim input for any
#     hierarchical Verilog design.
#
# The structural checks are hermetic (`--setup-only`, no compiler). When the
# sibling ../hlop + ../iassert headers are present the lg: and ln: paths are also
# host-compiled and RUN, and must agree with the plain two-source baseline.

set -u

LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_sim_ir_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# ---- fixtures: a clocked DUT and a testbench that imports it -----------------
cat > "$W/dut.prp" <<'EOF'
pub mod cnt(enable:bool) -> (value:u8@[0]) {
  reg count:u8 = 0
  value = count
  if enable { wrap count += 1 }
}
EOF

# One testbench body, three import spellings (sibling / lg: / ln:). The bound
# name is deliberately NOT the module's own name: a bound name that happens to
# equal a dut key short-circuits the import binding entirely, which would hide
# every resolution bug this test is about.
tb() {  # tb <import-string> <outfile>
  cat > "$2" <<EOF
const dutmod = import("$1")
test dutmod.held(cycles:u20 = 20) {
  mut acc = dutmod
  mut v = 0
  tick cycles {
    acc.enable = true
    acc.reset  = clock < 2
    step
    v = acc.value
  }
  assert(v == cycles - 2, "count must be cycles - 2")
}
EOF
}
tb "dut.cnt"    "$W/tb_src.prp"   # baseline: co-loaded .prp source
tb "lg:dut.cnt" "$W/tb_lg.prp"
tb "ln:dut.cnt" "$W/tb_ln.prp"

# ---- compile the DUT once, into each IR form --------------------------------
"$LHD" compile "$W/dut.prp" --emit-dir lg:"$W/L/" --workdir "$W/w_lg" -q --result-json "$W/r_lg.json" \
  || fail "dut compile→lg failed: $(cat "$W/r_lg.json" 2>/dev/null)"
grep -q 'graph_io .* dut.cnt' "$W/L/library.txt" || fail "lg: library misses dut.cnt: $(cat "$W/L/library.txt")"

"$LHD" compile "$W/dut.prp" --emit-dir ln:"$W/LN/" --workdir "$W/w_ln" -q --result-json "$W/r_ln.json" \
  || fail "dut compile→ln failed: $(cat "$W/r_ln.json" 2>/dev/null)"
[ -f "$W/LN/forest.txt" ] || fail "ln: dir has no forest.txt"

# ---- 1. the positional forms set up a driver AND the DUT body ---------------
# (the DUT body is what proves the lg: library joined the design; a name-only
# absorb would have left `sim` with nothing synthesizable to emit)
for kind in lg ln; do
  case $kind in
    lg) IN="lg:$W/L/"  ; TB="$W/tb_lg.prp" ;;
    ln) IN="ln:$W/LN/" ; TB="$W/tb_ln.prp" ;;
  esac
  "$LHD" sim "$IN" "$TB" --setup-only --workdir "$W/s_$kind" -q --result-json "$W/rs_$kind.json" \
    || fail "sim over $kind: failed: $(cat "$W/rs_$kind.json" 2>/dev/null)"
  [ -f "$W/s_$kind/sim/drv.cpp" ] || fail "sim over $kind: generated no driver"
  [ -s "$W/s_$kind/sim/dut.cnt.cpp" ] || fail "sim over $kind: generated no DUT body (dut.cnt.cpp): $(ls "$W/s_$kind/sim")"
  grep -q '_run_dutmod_held' "$W/s_$kind/sim/drv.cpp" || fail "sim over $kind: driver misses the dutmod.held test"
done

# ---- 2. --in KIND:DIR is the same input as the positional -------------------
for kind in lg ln; do
  case $kind in
    lg) IN="lg:$W/L/"  ; TB="$W/tb_lg.prp" ;;
    ln) IN="ln:$W/LN/" ; TB="$W/tb_ln.prp" ;;
  esac
  "$LHD" sim --in "$IN" "$TB" --setup-only --workdir "$W/i_$kind" -q --result-json "$W/ri_$kind.json" \
    || fail "sim --in $kind: failed: $(cat "$W/ri_$kind.json" 2>/dev/null)"
  [ -s "$W/i_$kind/sim/dut.cnt.cpp" ] || fail "sim --in $kind: generated no DUT body"
done

# ---- 3. an IR positional is NOT swallowed as the test selector ---------------
# A missing lg: dir must be reported AS a bad lg: input; the old parse turned it
# into a test name and failed somewhere else entirely.
out=$("$LHD" sim "lg:$W/nope/" "$W/tb_lg.prp" --setup-only --workdir "$W/s_bad" -q 2>&1)
[ $? -ne 0 ] || fail "a nonexistent lg: input must fail"
grep -q 'lg: input not found' <<<"$out" || fail "expected a missing-lg: diagnosis, got: $out"

# ---- 4. an lg:/ln: import that names no simulated module is an ERROR ---------
# The DUT a `test` block drives is bound from the import STRING, and the binder
# used to fall back to "the sole module" / "the unique root" when the string
# matched nothing — so a typo simulated some other module and passed with zero
# diagnostics. An artifact reference is exact by construction, so it must not
# guess. (A BARE `import("x")` keeps the fallbacks: an lg=-renamed pub entry
# legitimately differs from the emitted module name.)
tb "lg:NoSuchModule" "$W/tb_typo.prp"
out=$("$LHD" sim lg:"$W/L/" "$W/tb_typo.prp" --setup-only --workdir "$W/s_typo" -q 2>&1)
[ $? -ne 0 ] || fail "an lg: import naming no simulated module must fail, got: $out"
grep -q 'NoSuchModule'                  <<<"$out" || fail "the error must name the bad import: $out"
grep -q 'simulated modules: cnt'        <<<"$out" || fail "the error must list the modules that ARE simulated: $out"

# A commented-out import above the live one must not bind the DUT: the scan is
# textual, and the dead line used to contribute a `unit.entry` split that the
# live line had no way to override.
{ echo '// const cnt = import("stale.leftover")'; cat "$W/tb_lg.prp"; } > "$W/tb_cmt.prp"
"$LHD" sim lg:"$W/L/" "$W/tb_cmt.prp" --setup-only --workdir "$W/s_cmt" -q --result-json "$W/r_cmt.json" \
  || fail "a commented-out import broke the live one: $(cat "$W/r_cmt.json" 2>/dev/null)"
grep -q 'dut_cnt acc' "$W/s_cmt/sim/drv.cpp" || fail "driver bound the wrong DUT: $(grep -n ' acc;' "$W/s_cmt/sim/drv.cpp")"

# ---- 5. slang --top publishes the whole forest, so ln: stays linkable --------
cat > "$W/hier.sv" <<'EOF'
module leaf(input logic [7:0] a, output logic [7:0] y);
  assign y = a + 8'd1;
endmodule
module hier_top(input logic [7:0] a, output logic [7:0] y);
  leaf u(.a(a), .y(y));
endmodule
EOF
"$LHD" compile "$W/hier.sv" --reader slang --top hier_top --emit-dir ln:"$W/HN/" \
  --workdir "$W/w_h" -q --result-json "$W/r_h.json" \
  || fail "slang --top → ln: failed: $(cat "$W/r_h.json" 2>/dev/null)"
grep -q '"name":"leaf"'     "$W/HN/manifest.json" || fail "ln: forest dropped the instantiated leaf: $(cat "$W/HN/manifest.json")"
grep -q '"name":"hier_top"' "$W/HN/manifest.json" || fail "ln: forest misses hier_top"
# and it links back: lowering the forest resolves leaf, so Verilog comes out whole
"$LHD" compile ln:"$W/HN/" --emit verilog:"$W/hier.v" --workdir "$W/w_h2" -q --result-json "$W/r_h2.json" \
  || fail "relink of the slang ln: forest failed: $(cat "$W/r_h2.json" 2>/dev/null)"
grep -qE '^module leaf\('     "$W/hier.v" || fail "relinked Verilog misses module leaf: $(cat "$W/hier.v")"
grep -qE '^module hier_top\(' "$W/hier.v" || fail "relinked Verilog misses module hier_top"

# ---- opportunistic real build + run (needs the sibling runtime headers) ------
HLOP_INC=""
IASSERT_INC=""
for d in ../hlop/hlop ../hlop; do [ -f "$d/slop.hpp" ] && HLOP_INC="$d" && break; done
for d in ../iassert/src ../iassert; do [ -f "$d/iassert.hpp" ] && IASSERT_INC="$d" && break; done
if [ -z "$HLOP_INC" ] || [ -z "$IASSERT_INC" ]; then
  echo "SKIP run checks: sibling hlop/iassert headers not found (structural checks passed)"
  echo "PASS: lhd sim ln:/lg: IR inputs (structure)"
  exit 0
fi

# The baseline (two .prp positionals) and both IR forms must all pass the same
# assert — an IR input must not change what is simulated.
"$LHD" sim "$W/dut.prp" "$W/tb_src.prp" --workdir "$W/run_src" --diag-fmt pretty > "$W/run_src.out" 2>&1 \
  || fail "baseline .prp run failed: $(cat "$W/run_src.out")"
grep -q 'PASS dutmod.held' "$W/run_src.out" || fail "baseline did not pass: $(cat "$W/run_src.out")"

"$LHD" sim lg:"$W/L/" "$W/tb_lg.prp" --workdir "$W/run_lg" --diag-fmt pretty > "$W/run_lg.out" 2>&1 \
  || fail "lg: run failed: $(cat "$W/run_lg.out")"
grep -q 'PASS dutmod.held' "$W/run_lg.out" || fail "lg: run did not pass: $(cat "$W/run_lg.out")"

"$LHD" sim ln:"$W/LN/" "$W/tb_ln.prp" --workdir "$W/run_ln" --diag-fmt pretty > "$W/run_ln.out" 2>&1 \
  || fail "ln: run failed: $(cat "$W/run_ln.out")"
grep -q 'PASS dutmod.held' "$W/run_ln.out" || fail "ln: run did not pass: $(cat "$W/run_ln.out")"

echo "PASS: lhd sim ln:/lg: IR inputs (structure + run)"
