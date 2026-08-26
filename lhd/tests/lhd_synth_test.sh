#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# End-to-end test for `lhd synth`: the one-shot compile -> pass.color synth ->
# pass.abc -> pass.opentimer flow over ONE in-memory design, and the ONE
# incremental switch (`lhd.incremental`) it shares with the manual steps.
#
#   1. --workdir layout (<W>/synth/{lg,net,qor.json,timing.json}) + the
#      envelope's {kind:synth, abc:{abc-map}, sta:{sta}} qor member + phases.
#   2. A warm re-run over the same workdir is INCREMENTAL at both tiers
#      (compile cache hit, every abc region a hit) and byte-identical Verilog.
#   3. lhd.incremental=false is an honest cold run: no reuse, same netlist.
#   4. No --workdir: scratch dir, nothing durable except --emit-dir lg:/report:;
#      synth.opentimer=false skips STA (no timing.json, no sta member).
#   5. An lg: INPUT is never rewritten (the coloring stays in memory).
#   6. Negative controls: pass.abc.library refused (synth.liberty is the one
#      spelling), a missing Liberty is `missing_file`, a non-synth coloring is
#      refused, LNAST-side emits are refused, report: is synth-only, and the
#      retired per-tier cache flags answer with the lhd.incremental hint.
#
# Hermetic: the vendored Liberty (inou/prp/tests/abc/test.lib), no PDK.
set -u

LHD=lhd/lhd
LIB=inou/prp/tests/abc/test.lib
FIX=inou/prp/tests/pyrope/hier_seq.prp
W="${TEST_TMPDIR:-/tmp/lhd_synth_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}
run() { "$LHD" "$@" -q --result-json "$W/r.json" || fail "$* -> $(cat "$W/r.json" 2>/dev/null)"; }
# jget FILE DOTTED.PATH -> the value ("" when absent). 3.9-clean python.
jget() {
  python3 - "$1" "$2" <<'PY'
import json, sys
try:
    v = json.load(open(sys.argv[1]))
    for k in sys.argv[2].split('.'):
        v = v[k]
    print(str(v).lower() if isinstance(v, bool) else v)
except Exception:
    print("")
PY
}
has_phase() { grep -q "\"name\":\"$2\"" "$1"; }
tree_sum() { (cd "$1" && find . -type f | LC_ALL=C sort | xargs shasum | shasum | cut -d' ' -f1); }

[ -f "$FIX" ] || fail "missing fixture $FIX"
[ -f "$LIB" ] || fail "missing liberty $LIB"
# The def names embed the FILE name (internal naming = file.entity).
cp "$FIX" "$W/dut.prp"
# absorb=false keeps the tiny fixture defs as their own regions (3 regions),
# exactly as the pass.abc incremental test pins; it also proves that a pass
# knob spelled under its pass namespace (`color.*`) reaches the fused step.
SYNTH=(synth "$W/dut.prp" --top top --set synth.liberty="$LIB" --set color.absorb=false)

# --- 1. one-shot with --workdir ---------------------------------------------
run "${SYNTH[@]}" --workdir "$W/w" --stats --emit verilog:"$W/net0.v"
for d in lg net; do [ -d "$W/w/synth/$d" ] || fail "missing <workdir>/synth/$d"; done
for f in qor.json timing.json; do [ -s "$W/w/synth/$f" ] || fail "missing <workdir>/synth/$f"; done
[ -s "$W/net0.v" ] || fail "--emit verilog: of the mapped netlist missing"
grep -qE "INVx1|NAND2x1|XOR2x1|DFFx1" "$W/net0.v" || fail "the Verilog is not the MAPPED netlist (no Liberty cells): $(head -c 300 "$W/net0.v")"
[ "$(jget "$W/r.json" qor.kind)" = synth ] || fail "envelope qor.kind != synth: $(head -c 400 "$W/r.json")"
[ "$(jget "$W/r.json" qor.top)" = dut.top ] || fail "--top was not resolved once to the full name: $(jget "$W/r.json" qor.top)"
[ "$(jget "$W/r.json" qor.abc.kind)" = abc-map ] || fail "qor.abc is not the pass.abc report"
[ "$(jget "$W/r.json" qor.sta.kind)" = sta ] || fail "qor.sta is not the pass.opentimer report"
[ -n "$(jget "$W/r.json" qor.sta.designs)" ] || fail "sta report carries no designs"
[ "$(jget "$W/r.json" qor.abc.total.regions)" = 3 ] || fail "expected 3 abc regions, got '$(jget "$W/r.json" qor.abc.total.regions)'"
for p in pass.color pass.abc pass.opentimer lg.save; do has_phase "$W/r.json" $p || fail "phase $p missing from the envelope"; done
[ "$(jget "$W/r.json" incremental.compile.enabled)" = true ] || fail "compile tier not enabled under a user --workdir"
[ "$(jget "$W/r.json" qor.abc.incremental.hits)" = 0 ] || fail "cold run reported abc hits"
[ "$(jget "$W/r.json" qor.abc.incremental.misses)" = 3 ] || fail "cold run: expected 3 abc misses"
# the envelope's ONE `incremental` member carries both tiers (what a stats report builder reads)
[ "$(jget "$W/r.json" incremental.abc.misses)" = 3 ] || fail "incremental.abc not mirrored into the envelope: $(head -c 600 "$W/r.json")"
[ "$(jget "$W/r.json" incremental.abc.regions)" = 3 ] || fail "incremental.abc.regions wrong"
[ "$(jget "$W/r.json" incremental.abc.store_failed)" = 0 ] || fail "incremental.abc.store_failed wrong"
[ -d "$W/w/abc_cache" ] || fail "abc region cache not created under --workdir"
grep -q '"qor":{"schema_version":1,"kind":"synth"' "$W/r.json" || fail "qor member not embedded verbatim"
# pretty rendering: the abc-map line, the STA critical path, and --stats rows
"$LHD" "${SYNTH[@]}" --workdir "$W/w" --stats --diag-fmt pretty -q >"$W/pretty.out" || fail "pretty run failed"
grep -q '^  qor: abc-map' "$W/pretty.out" || fail "pretty report lacks the abc-map line: $(cat "$W/pretty.out")"
grep -q '^  sta: ' "$W/pretty.out" || fail "pretty report lacks the sta line: $(cat "$W/pretty.out")"
[ "$(grep -c '^  abc\[stats\]:' "$W/pretty.out")" = 3 ] || fail "--stats did not print one abc row per region: $(cat "$W/pretty.out")"
grep -q '^  incremental\[stats\]: compile enabled=true' "$W/pretty.out" || fail "--stats lacks the compile-tier incremental row: $(cat "$W/pretty.out")"
grep -q '^  incremental\[stats\]: abc enabled=true regions=3 hits=3 misses=0' "$W/pretty.out" || fail "--stats lacks the abc-tier incremental row: $(cat "$W/pretty.out")"
grep -q '^  phases\[stats\]: .*pass.abc=.*total=' "$W/pretty.out" || fail "--stats lacks the phases row: $(cat "$W/pretty.out")"
echo "PASS: one-shot synth with --workdir (layout, qor member, phases, report)"

# --- 2. warm re-run: both tiers hit, Verilog byte-identical ------------------
run "${SYNTH[@]}" --workdir "$W/w" --emit verilog:"$W/net1.v"
[ "$(jget "$W/r.json" incremental.compile.misses)" = 0 ] || fail "warm run re-parsed a source unit"
[ "$(jget "$W/r.json" incremental.compile.hits)" -ge 1 ] || fail "warm run did not reuse the compiled design"
[ "$(jget "$W/r.json" qor.abc.incremental.hits)" = 3 ] || fail "warm run: expected 3 abc hits, got '$(jget "$W/r.json" qor.abc.incremental.hits)'"
[ "$(jget "$W/r.json" qor.abc.incremental.misses)" = 0 ] || fail "warm run re-synthesized a region"
cmp -s "$W/net0.v" "$W/net1.v" || fail "warm netlist differs from the cold mapping"
echo "PASS: warm re-run is incremental at both tiers and byte-identical"

# --- 3. lhd.incremental=false: honest cold run, same answer ------------------
run "${SYNTH[@]}" --workdir "$W/w" --set lhd.incremental=false --emit verilog:"$W/net2.v"
[ "$(jget "$W/r.json" incremental.compile.enabled)" = false ] || fail "lhd.incremental=false left the compile tier on"
[ "$(jget "$W/r.json" incremental.compile.hits)" = 0 ] || fail "lhd.incremental=false still reported compile hits"
[ -z "$(jget "$W/r.json" qor.abc.incremental.hits)" ] || fail "lhd.incremental=false still ran the abc region cache"
[ "$(jget "$W/r.json" incremental.abc.enabled)" = false ] || fail "incremental.abc.enabled must be false on a cold map"
cmp -s "$W/net0.v" "$W/net2.v" || fail "cold (incremental=false) netlist differs from the cached one"

# ... and on a design whose region boundary has ANONYMOUS crossings, which is
# where the two used to diverge. hier_seq above cannot see this: every one of its
# crossings is a named wire, so both port schemes spell it the same way and the
# cmp passes no matter what. abc_block_attr has unnamed crossings, so a
# regression shows up as a reordered port list plus `o_n<nid>_p<pid>` in place of
# the content hash -- and pass.abc creates its ABC POs in that order, so a
# different order is a different AIG and a different (equally correct) mapping.
# That is how `lhd.incremental` silently stopped being QoR-neutral: measured on
# cva6 at 5,364,322 gates cache-off vs 5,366,128 cache-on, and on dino at a
# 50.6705 vs 51.0985 critical path, from a byte-identical pre-ABC design.
ANON=inou/prp/tests/pyrope/abc_block_attr.prp
run synth "$ANON" --workdir "$W/wa_on" --set synth.liberty="$LIB" --set synth.opentimer=false \
    --emit verilog:"$W/anon_on.v"
run synth "$ANON" --workdir "$W/wa_off" --set synth.liberty="$LIB" --set synth.opentimer=false \
    --set lhd.incremental=false --emit verilog:"$W/anon_off.v"
# Vacuity guard on the DEFAULT run (the one whose scheme is not what regressed):
# if this fixture ever loses its anonymous crossings, the cmp below can no longer
# fail and would pass for the wrong reason.
grep -qE '[.]o_[0-9a-f]{16}\(' "$W/anon_on.v" \
  || fail "the anonymous-crossing fixture stopped producing content-hashed ports -- this gate is now vacuous"
cmp -s "$W/anon_on.v" "$W/anon_off.v" \
  || fail "lhd.incremental changed the NETLIST on a design with anonymous region crossings: $(diff "$W/anon_on.v" "$W/anon_off.v" | head -6 | tr '\n' ' ')"
echo "PASS: lhd.incremental=false disables every tier with identical output"

# --- 4. no --workdir: scratch; --emit-dir lg:/report:; opentimer off ----------
run "${SYNTH[@]}" --emit-dir lg:"$W/net_only" --emit-dir report:"$W/rep" --set synth.opentimer=false
[ -d "$W/net_only" ] || fail "--emit-dir lg: netlist missing without --workdir"
[ -s "$W/rep/qor.json" ] || fail "--emit-dir report: qor.json missing"
[ -e "$W/rep/timing.json" ] && fail "synth.opentimer=false still produced timing.json"
[ -z "$(jget "$W/r.json" qor.sta)" ] || fail "synth.opentimer=false still embedded an sta report"
has_phase "$W/r.json" pass.opentimer && fail "synth.opentimer=false still ran pass.opentimer"
[ -z "$(jget "$W/r.json" incremental.compile.enabled)" ] || fail "no --workdir must report no compile tier"
grep -q "\"$W/net_only\"" "$W/r.json" || fail "--emit-dir lg: not declared as an output"
grep -q "synth/net" "$W/r.json" && fail "a scratch workdir path leaked into the declared outputs: $(cat "$W/r.json")"
echo "PASS: no --workdir runs in scratch; emits and reports are the only artifacts"

# --- 5. an lg: input is read-only --------------------------------------------
run compile "$W/dut.prp" --top top --emit-dir lg:"$W/lg_in" --workdir "$W/w_c"
before=$(tree_sum "$W/lg_in")
run synth lg:"$W/lg_in" --top top --set synth.liberty="$LIB" --set color.absorb=false --emit-dir lg:"$W/net_lg"
[ "$(jget "$W/r.json" qor.abc.total.regions)" = 3 ] || fail "lg: input synth: expected 3 regions"
[ "$(tree_sum "$W/lg_in")" = "$before" ] || fail "synth rewrote its lg: INPUT (the coloring must stay in memory)"
echo "PASS: an lg: input is never rewritten"

# --- 6. negative controls ----------------------------------------------------
expect_fail() {  # CLASS PATTERN ARGS...
  local cls=$1 pat=$2
  shift 2
  if "$LHD" "$@" -q --result-json "$W/neg.json" 2>"$W/neg.err"; then
    fail "expected failure: $*"
  fi
  [ "$(jget "$W/neg.json" error.class)" = "$cls" ] || fail "$*: expected error class $cls, got $(cat "$W/neg.json")"
  grep -q "$pat" "$W/neg.json" || fail "$*: message lacks '$pat': $(cat "$W/neg.json")"
}
expect_fail usage "synth.liberty" synth "$W/dut.prp" --top top --set pass.abc.library="$LIB"
expect_fail missing_file "synth.liberty" synth "$W/dut.prp" --top top --set synth.liberty="$W/no_such.lib"
expect_fail usage "manual flow" synth "$W/dut.prp" --top top --set synth.liberty="$LIB" --set color.alg=flat
expect_fail usage "does not emit ln" synth "$W/dut.prp" --top top --set synth.liberty="$LIB" --emit-dir ln:"$W/ln"
expect_fail usage "lhd synth" compile "$W/dut.prp" --top top --emit-dir report:"$W/rep2"
expect_fail usage "unknown synth flag" synth "$W/dut.prp" --top top --set synth.liberty="$LIB" --set synth.potato=1
expect_fail usage "lhd.incremental" synth "$W/dut.prp" --top top --set synth.liberty="$LIB" --set abc.cache=false
expect_fail usage "lhd.incremental" compile "$W/dut.prp" --top top --set compile.cache=false
expect_fail usage "lhd.incremental" lec --impl "$W/dut.prp" --ref "$W/dut.prp" --top top --set formal.cache=false
echo "PASS: negative controls"

# --- 7. the help surface ------------------------------------------------------
"$LHD" synth --help --diag-fmt pretty | grep -q '^usage: lhd synth' || fail "lhd synth --help has no usage line"
"$LHD" help synth --diag-fmt json | grep -q '"name":"synth"' || fail "lhd help synth (json) is not the synth record"
"$LHD" describe synth | grep -q '"name":"synth"' || fail "lhd describe synth missing"
"$LHD" list emit-kinds | grep -q '"report"' || fail "report: missing from the emit-kind vocabulary"
echo "PASS: help surface"

echo "PASS: all lhd synth flows"
