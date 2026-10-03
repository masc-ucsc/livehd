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

# One script, both technology mappers: MAPPER=abc (default) drives pass.abc and
# MAPPER=usyn drives pass.usyn through `--set synth.mapper=`; lhd/tests/BUILD
# generates the `_usyn` twin from this same file. The workdir layout, the qor
# envelope, warm/cold/disabled reuse, byte-identical netlists, the lg:-input
# contract, the memory LEC and the negative controls hold for both. Only the
# REPORT SHAPE differs: under abc, qor.abc is pass.abc's abc-map report (its
# `total` block, one abc[stats] row per region); under usyn (default
# tmap=abc), qor.usyn is the schema-5 native decision report (definition
# regions, preserved register bits, the logical usyn_cache counters) and qor.abc
# the separate technology-map report of the ABC tmap provider (its own region
# count and usyn_cache/tmap counters, which also feed incremental.abc). USYN
# keeps memories as native barriers (pass.abc lowers them to flops), and
# tmap=none is a Liberty-free logical-CMOS run with no qor.abc and no STA.
MAPPER="${MAPPER:-abc}"
case "$MAPPER" in
  abc | usyn) ;;
  *)
    echo "FAIL: bad MAPPER=$MAPPER (expected abc|usyn)" >&2
    exit 1
    ;;
esac

LHD=lhd/lhd
LIB=inou/prp/tests/abc/test.lib
FIX=inou/prp/tests/pyrope/hier_seq.prp
W="${TEST_TMPDIR:-/tmp/lhd_synth_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}
# run ARGS...: one lhd invocation whose result JSON lands in $RJ (each parallel
# group below binds its own RJ).
RJ="$W/r.json"
run() { "$LHD" "$@" -q --result-json "$RJ" || fail "$* -> $(cat "$RJ" 2>/dev/null)"; }
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
# jeval FILE PYTHON-EXPR -> the printed value ("" on any error). The expression
# sees d (the whole JSON), q (its qor member), u (qor.usyn) and t (qor.abc).
jeval() {
  python3 -c 'import json,sys
try:
    d=json.load(open(sys.argv[1]))
    q=d.get("qor") or {}
    u=q.get("usyn") or {}
    t=q.get("abc") or {}
    print(eval(sys.argv[2]))
except Exception:
    print("")' "$1" "$2"
}
has_phase() { grep -q "\"name\":\"$2\"" "$1"; }
tree_sum() { (cd "$1" && find . -type f | LC_ALL=C sort | xargs shasum | shasum | cut -d' ' -f1); }

[ -f "$FIX" ] || fail "missing fixture $FIX"
[ -f "$LIB" ] || fail "missing liberty $LIB"
# The def names embed the FILE name (internal naming = file.entity).
cp "$FIX" "$W/dut.prp"
SYNTH=(synth "$W/dut.prp" --top top --set synth.liberty="$LIB" --set synth.mapper="$MAPPER")

# How many ABC regions the shipped coloring opens on this fixture is a QoR
# choice that moves whenever the coloring is tuned (it dropped from 8 to 2 when
# `synth` started colouring the flat view of the hierarchy). Pinning the NUMBER
# would make this plumbing test fail on every such tune for no reason -- so read
# it once from the report and assert everything else AGREES with it. That
# agreement is the actual contract: one `abc[stats]` row per region, and
# incremental counters that add up to the same total. Under usyn REGIONS counts
# the native definition regions (qor.usyn) and TREGIONS the technology-map
# regions (qor.abc, whose cache feeds incremental.abc); they need not be equal.
# Under abc both are pass.abc's region count.
REGIONS=""
TREGIONS=""

# --- 1. one-shot with --workdir ---------------------------------------------
run "${SYNTH[@]}" --workdir "$W/w" --stats --emit verilog:"$W/net0.v"
python3 - "$W/r.json" <<'PY'
import json,sys
r=json.load(open(sys.argv[1]))
invocation=r['synthesis_invocation']
assert invocation['scope']=='main_entry_to_result_emission' and invocation['wall_ms']>0,invocation
assert invocation['memory_scope']=='parent_process_peak_rss' and invocation['parent_peak_rss_bytes']>0,invocation
assert invocation['process_tree_peak_bytes'] is None,invocation
assert sum(p['ms'] for p in r['phases'])<=invocation['wall_ms']+len(r['phases'])*0.0005+1e-6,r
PY
for d in lg net; do [ -d "$W/w/synth/$d" ] || fail "missing <workdir>/synth/$d"; done
for f in qor.json timing.json; do [ -s "$W/w/synth/$f" ] || fail "missing <workdir>/synth/$f"; done
[ -s "$W/net0.v" ] || fail "--emit verilog: of the mapped netlist missing"
grep -qE "INVx1|NAND2x1|XOR2x1|DFFx1" "$W/net0.v" || fail "the Verilog is not the MAPPED netlist (no Liberty cells): $(head -c 300 "$W/net0.v")"
[ "$(jget "$W/r.json" qor.kind)" = synth ] || fail "envelope qor.kind != synth: $(head -c 400 "$W/r.json")"
[ "$(jget "$W/r.json" qor.top)" = dut.top ] || fail "--top was not resolved once to the full name: $(jget "$W/r.json" qor.top)"
[ "$(jget "$W/r.json" qor.sta.kind)" = sta ] || fail "qor.sta is not the pass.opentimer report"
[ -n "$(jget "$W/r.json" qor.sta.designs)" ] || fail "sta report carries no designs"
if [ "$MAPPER" = abc ]; then
  [ "$(jget "$W/r.json" qor.abc.kind)" = abc-map ] || fail "qor.abc is not the pass.abc report"
  REGIONS=$(jget "$W/r.json" qor.abc.total.regions)
  [ -n "$REGIONS" ] && [ "$REGIONS" -gt 0 ] || fail "qor.abc.total.regions missing or zero, got '$REGIONS'"
  TREGIONS=$REGIONS
else
  U=$W/w/synth/qor.json.usyn.json
  [ "$(jeval "$W/r.json" 'u["schema_version"],u["kind"],u["tmap"],u["output"]')" = "(5, 'usyn', 'abc', 'mapped-cmos')" ] \
    || fail "qor.usyn is not the schema-5 mapped-cmos USYN report: $(head -c 600 "$W/r.json")"
  [ "$(jeval "$W/r.json" 't["kind"],t["provider"]')" = "('technology-map', 'abc')" ] \
    || fail "qor.abc is not the ABC provider's technology-map report"
  # tmap=abc: qor.json is the technology-map report and <qor>.usyn.json beside
  # it the native one embedded as qor.usyn
  [ "$(jget "$W/w/synth/qor.json" kind)" = technology-map ] || fail "<workdir>/synth/qor.json is not the technology-map report"
  [ "$(jeval "$U" 'd["kind"],d["totals"]')" = "$(jeval "$W/r.json" 'u["kind"],u["totals"]')" ] \
    || fail "$U differs from the embedded qor.usyn"
  REGIONS=$(jeval "$W/r.json" 'u["totals"]["regions"]')
  [ -n "$REGIONS" ] && [ "$REGIONS" -gt 0 ] || fail "qor.usyn.totals.regions missing or zero, got '$REGIONS'"
  [ "$(jeval "$W/r.json" 'len(u["regions"])')" = "$REGIONS" ] || fail "qor.usyn: one regions[] row per definition region"
  TREGIONS=$(jget "$W/r.json" qor.abc.incremental.regions)
  [ -n "$TREGIONS" ] && [ "$TREGIONS" -gt 0 ] || fail "qor.abc.incremental.regions missing or zero, got '$TREGIONS'"
  [ "$(jeval "$W/r.json" 'len(t["regions"])')" = "$TREGIONS" ] || fail "technology-map report: one regions[] row per mapped region"
  # CMOS output keeps exactly the original registers: every reported register
  # bit is an endpoint and one mapped DFF cell, and no native flop survives
  RBITS=$(jeval "$W/r.json" 'u["totals"]["register_bits"]')
  [ -n "$RBITS" ] && [ "$RBITS" -gt 0 ] || fail "qor.usyn.totals.register_bits missing or zero, got '$RBITS'"
  [ "$(jeval "$W/r.json" 'u["totals"]["eligible_endpoints"]')" = "$RBITS" ] || fail "every register bit must be a USYN endpoint"
  [ "$(grep -c '^DFFx1 ' "$W/net0.v")" = "$RBITS" ] || fail "expected $RBITS DFF cells (the original registers), got $(grep -c '^DFFx1 ' "$W/net0.v")"
  ! grep -q 'always @' "$W/net0.v" || fail "a register stayed a native flop"
  # the logical region cache: a cold run misses and stores every region
  [ "$(jeval "$W/r.json" 'tuple(u["cache"][k] for k in ("enabled","reused","misses","stored","invalid","store_failures"))')" = "(True, 0, $REGIONS, $REGIONS, 0, 0)" ] \
    || fail "cold run: the USYN logical cache must miss and store all $REGIONS region(s): $(jeval "$W/r.json" 'u["cache"]')"
  [ -d "$W/w/usyn_cache/tmap" ] || fail "the tmap mapping cache was not created under usyn_cache/"
fi
for p in pass.color "pass.$MAPPER" pass.opentimer lg.save; do has_phase "$W/r.json" $p || fail "phase $p missing from the envelope"; done
[ "$(jget "$W/r.json" incremental.compile.enabled)" = true ] || fail "compile tier not enabled under a user --workdir"
[ "$(jget "$W/r.json" qor.abc.incremental.hits)" = 0 ] || fail "cold run reported abc hits"
[ "$(jget "$W/r.json" qor.abc.incremental.misses)" = "$TREGIONS" ] || fail "cold run: every one of the $TREGIONS regions must miss, got '$(jget "$W/r.json" qor.abc.incremental.misses)'"
grep -q 'pass.color [^"]*alg:reduce' "$W/r.json" && fail "default synthesis must skip experimental reduction"
# the envelope's ONE `incremental` member carries both tiers (what a stats report builder reads)
[ "$(jget "$W/r.json" incremental.abc.misses)" = "$TREGIONS" ] || fail "incremental.abc not mirrored into the envelope: $(head -c 600 "$W/r.json")"
[ "$(jget "$W/r.json" incremental.abc.regions)" = "$TREGIONS" ] || fail "incremental.abc.regions wrong"
[ "$(jget "$W/r.json" incremental.abc.store_failed)" = 0 ] || fail "incremental.abc.store_failed wrong"
[ -d "$W/w/${MAPPER}_cache" ] || fail "$MAPPER region cache not created under --workdir"
grep -q '"qor":{"schema_version":1,"kind":"synth"' "$W/r.json" || fail "qor member not embedded verbatim"
# pretty rendering: the abc-map line (usyn: the native and technology-map
# lines), the STA critical path, and --stats rows
"$LHD" "${SYNTH[@]}" --workdir "$W/w" --stats --diag-fmt pretty -q >"$W/pretty.out" || fail "pretty run failed"
if [ "$MAPPER" = abc ]; then
  grep -q '^  qor: abc-map' "$W/pretty.out" || fail "pretty report lacks the abc-map line: $(cat "$W/pretty.out")"
  [ "$(grep -c '^  abc\[stats\]:' "$W/pretty.out")" = "$REGIONS" ] || fail "--stats did not print one abc row per region: $(cat "$W/pretty.out")"
else
  grep -q "^  qor: usyn 'dut.top' (mapped-cmos): $REGIONS definition region(s), $RBITS register bits" "$W/pretty.out" \
    || fail "pretty report lacks the usyn line: $(cat "$W/pretty.out")"
  grep -q "^  qor: technology-map 'dut.top' provider=abc: " "$W/pretty.out" || fail "pretty report lacks the technology-map line: $(cat "$W/pretty.out")"
  [ "$(grep -c '^  usyn\[stats\]:' "$W/pretty.out")" = "$REGIONS" ] || fail "--stats did not print one usyn row per region: $(cat "$W/pretty.out")"
  [ "$(grep -c '^  tmap\[stats\]:' "$W/pretty.out")" = "$TREGIONS" ] || fail "--stats did not print one tmap row per mapped region: $(cat "$W/pretty.out")"
fi
grep -q '^  sta: ' "$W/pretty.out" || fail "pretty report lacks the sta line: $(cat "$W/pretty.out")"
grep -q '^  incremental\[stats\]: compile enabled=true' "$W/pretty.out" || fail "--stats lacks the compile-tier incremental row: $(cat "$W/pretty.out")"
grep -q "^  incremental\[stats\]: abc enabled=true regions=$TREGIONS hits=$TREGIONS misses=0" "$W/pretty.out" || fail "--stats lacks the abc-tier incremental row: $(cat "$W/pretty.out")"
grep -q "^  phases\[stats\]: .*pass.$MAPPER=.*total=" "$W/pretty.out" || fail "--stats lacks the phases row: $(cat "$W/pretty.out")"
echo "PASS: one-shot synth with --workdir (layout, qor member, phases, report)"

# --- 2. warm re-run: both tiers hit, Verilog byte-identical ------------------
run "${SYNTH[@]}" --workdir "$W/w" --emit verilog:"$W/net1.v"
[ "$(jget "$W/r.json" incremental.compile.misses)" = 0 ] || fail "warm run re-parsed a source unit"
[ "$(jget "$W/r.json" incremental.compile.hits)" -ge 1 ] || fail "warm run did not reuse the compiled design"
[ "$(jget "$W/r.json" qor.abc.incremental.hits)" = "$TREGIONS" ] || fail "warm run: expected $TREGIONS abc hits, got '$(jget "$W/r.json" qor.abc.incremental.hits)'"
[ "$(jget "$W/r.json" qor.abc.incremental.misses)" = 0 ] || fail "warm run re-synthesized a region"
if [ "$MAPPER" = usyn ]; then
  # every definition region is REUSED from the logical cache (no new search, no
  # store); the hit reserves the original search credits
  [ "$(jeval "$W/r.json" 'tuple(u["cache"][k] for k in ("enabled","reused","misses","stored","invalid")) + (u["cache"]["replayed_search_work"]>0,)')" = "(True, $REGIONS, 0, 0, 0, True)" ] \
    || fail "warm run: all $REGIONS USYN region(s) must be reused: $(jeval "$W/r.json" 'u["cache"]')"
  [ "$(jeval "$W/r.json" 'all(r["cache_reused"] for r in u["regions"])')" = True ] || fail "warm run: a USYN region row is not cache_reused"
fi
cmp -s "$W/net0.v" "$W/net1.v" || fail "warm netlist differs from the cold mapping"
echo "PASS: warm re-run is incremental at both tiers and byte-identical"

# --- 3. lhd.incremental=false: honest cold run, same answer ------------------
cache_before=$(tree_sum "$W/w/${MAPPER}_cache")
run "${SYNTH[@]}" --workdir "$W/w" --set lhd.incremental=false --emit verilog:"$W/net2.v"
[ "$(jget "$W/r.json" incremental.compile.enabled)" = false ] || fail "lhd.incremental=false left the compile tier on"
[ "$(jget "$W/r.json" incremental.compile.hits)" = 0 ] || fail "lhd.incremental=false still reported compile hits"
if [ "$MAPPER" = abc ]; then
  [ -z "$(jget "$W/r.json" qor.abc.incremental.hits)" ] || fail "lhd.incremental=false still ran the abc region cache"
else
  # the technology-map cache reports itself disabled with no hit; the logical
  # cache performs neither reads nor writes (no reuse, no store, no cache I/O)
  [ "$(jeval "$W/r.json" 't["incremental"]["enabled"],t["incremental"]["hits"]')" = "(False, 0)" ] \
    || fail "lhd.incremental=false still ran the tmap region cache: $(jeval "$W/r.json" 't["incremental"]')"
  [ "$(jeval "$W/r.json" 'tuple(u["cache"][k] for k in ("enabled","reused","misses","stored","io_work"))')" = "(False, 0, 0, 0, 0)" ] \
    || fail "lhd.incremental=false still used the USYN logical cache: $(jeval "$W/r.json" 'u["cache"]')"
fi
[ "$(jget "$W/r.json" incremental.abc.enabled)" = false ] || fail "incremental.abc.enabled must be false on a cold map"
[ "$(tree_sum "$W/w/${MAPPER}_cache")" = "$cache_before" ] || fail "lhd.incremental=false wrote into $W/w/${MAPPER}_cache"
cmp -s "$W/net0.v" "$W/net2.v" || fail "cold (incremental=false) netlist differs from the cached one"

# Everything below is independent of the shared workdir above and of each
# other: every group owns its files and its result JSON ($RJ) and runs in the
# background (a debug lhd spends most of each invocation starting up), and the
# script fails if any group fails.
PIDS=()
spawn() {
  "$@" &
  PIDS+=($!)
}

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
anon_synth() {  # <tag> [extra --set ...]
  local RJ="$W/anon_$1.json" tag=$1
  shift
  run synth "$ANON" --workdir "$W/wa_$tag" --set synth.liberty="$LIB" --set synth.mapper="$MAPPER" --set synth.opentimer=false \
      "$@" --emit verilog:"$W/anon_$tag.v"
}
group_anon() {
  local a b
  anon_synth on &
  a=$!
  anon_synth off --set lhd.incremental=false &
  b=$!
  wait "$a" || exit 1
  wait "$b" || exit 1
  # Vacuity guard on the DEFAULT run (the one whose scheme is not what regressed):
  # if this fixture ever loses its anonymous crossings, the cmp below can no longer
  # fail and would pass for the wrong reason.
  grep -qE '[.]o_[0-9a-f]{16}\(' "$W/anon_on.v" \
    || fail "the anonymous-crossing fixture stopped producing content-hashed ports -- this gate is now vacuous"
  cmp -s "$W/anon_on.v" "$W/anon_off.v" \
    || fail "lhd.incremental changed the NETLIST on a design with anonymous region crossings: $(diff "$W/anon_on.v" "$W/anon_off.v" | head -6 | tr '\n' ' ')"
  echo "PASS: lhd.incremental=false disables every tier with identical output"
}
ANON=inou/prp/tests/pyrope/abc_block_attr.prp
spawn group_anon

# --- 4. no --workdir: scratch; --emit-dir lg:/report:; opentimer off ----------
group_scratch() {
  local RJ="$W/scratch.json"
  run "${SYNTH[@]}" --emit-dir lg:"$W/net_only" --emit-dir report:"$W/rep" --set synth.opentimer=false
  [ -d "$W/net_only" ] || fail "--emit-dir lg: netlist missing without --workdir"
  [ -s "$W/rep/qor.json" ] || fail "--emit-dir report: qor.json missing"
  [ -e "$W/rep/timing.json" ] && fail "synth.opentimer=false still produced timing.json"
  [ -z "$(jget "$RJ" qor.sta)" ] || fail "synth.opentimer=false still embedded an sta report"
  has_phase "$RJ" pass.opentimer && fail "synth.opentimer=false still ran pass.opentimer"
  [ -z "$(jget "$RJ" incremental.compile.enabled)" ] || fail "no --workdir must report no compile tier"
  grep -q "\"$W/net_only\"" "$RJ" || fail "--emit-dir lg: not declared as an output"
  grep -q "synth/net" "$RJ" && fail "a scratch workdir path leaked into the declared outputs: $(cat "$RJ")"
  if [ "$MAPPER" = usyn ]; then
    # report: carries the technology-map qor.json, the native <qor>.usyn.json and
    # the region artifacts that report links
    [ "$(jget "$W/rep/qor.json" kind)" = technology-map ] || fail "--emit-dir report: qor.json is not the technology-map report"
    [ "$(jget "$W/rep/qor.json.usyn.json" kind)" = usyn ] || fail "--emit-dir report: lacks the native qor.json.usyn.json"
    local art
    art=$(jeval "$W/rep/qor.json.usyn.json" 'd["regions"][0]["artifact"]["path"]')
    [ -n "$art" ] && [ -s "$W/rep/$art" ] || fail "--emit-dir report: did not copy the region artifact '$art'"
  fi
  echo "PASS: no --workdir runs in scratch; emits and reports are the only artifacts"
}
spawn group_scratch

# --- 4b. usyn tmap=none: logical CMOS, no Liberty, no qor.abc, no STA ---------
group_logic() {
  local RJ="$W/logic.json"
  run synth "$W/dut.prp" --top top --set synth.mapper=usyn --set pass.usyn.tmap=none --workdir "$W/w_logic" \
      --emit verilog:"$W/logic.v"
  [ "$(jeval "$RJ" 'q["abc"],"sta" in q,u["tmap"],u["output"]')" = "(None, False, 'none', 'logical-cmos')" ] \
    || fail "tmap=none: expected qor.abc=null, no STA and a logical-cmos USYN report: $(head -c 600 "$RJ")"
  has_phase "$RJ" pass.opentimer && fail "tmap=none still ran pass.opentimer"
  [ -e "$W/w_logic/synth/timing.json" ] && fail "tmap=none produced timing.json"
  [ "$(jget "$W/w_logic/synth/qor.json" kind)" = usyn ] || fail "tmap=none: qor.json must be the native USYN report"
  # the same original registers, now native (one always block per bit), no cells
  [ "$(jeval "$RJ" 'u["totals"]["register_bits"]')" = "$RBITS" ] || fail "tmap=none changed the register count"
  [ "$(grep -c 'always @' "$W/logic.v")" = "$RBITS" ] || fail "tmap=none: expected $RBITS native registers, got $(grep -c 'always @' "$W/logic.v")"
  ! grep -qE '^(INV|NAND2|XOR2|DFF)x1 ' "$W/logic.v" || fail "tmap=none emitted Liberty cells"
  echo "PASS: tmap=none is a Liberty-free logical CMOS run that keeps the registers"
}
[ "$MAPPER" = usyn ] && spawn group_logic

# --- 5. an lg: input is read-only --------------------------------------------
group_lg_input() {
  local RJ="$W/lg_in.json" before
  run compile "$W/dut.prp" --top top --emit-dir lg:"$W/lg_in" --workdir "$W/w_c"
  before=$(tree_sum "$W/lg_in")
  run synth lg:"$W/lg_in" --top top --set synth.liberty="$LIB" --set synth.mapper="$MAPPER" --emit-dir lg:"$W/net_lg"
  if [ "$MAPPER" = abc ]; then
    [ "$(jget "$RJ" qor.abc.total.regions)" = "$REGIONS" ] || fail "lg: input synth: expected $REGIONS regions, got '$(jget "$RJ" qor.abc.total.regions)'"
  else
    [ "$(jeval "$RJ" 'u["totals"]["regions"],t["incremental"]["regions"]')" = "($REGIONS, $TREGIONS)" ] \
      || fail "lg: input synth: expected $REGIONS USYN / $TREGIONS tmap regions, got $(jeval "$RJ" 'u["totals"]["regions"],t["incremental"]["regions"]')"
  fi
  [ "$(tree_sum "$W/lg_in")" = "$before" ] || fail "synth rewrote its lg: INPUT (the coloring must stay in memory)"
  echo "PASS: an lg: input is never rewritten"
}
spawn group_lg_input

# --- 5b. a reused --workdir runs like a fresh one ------------------------------
# Re-running in the SAME workdir after renaming a top input (a -> a2) and
# dropping a module used to keep the previous <workdir>/synth/lg: the old port
# stayed declared on the renamed one's pin (usyn: "region input is not declared
# on the destination"; pass.abc crashed) and the dropped module stayed in the
# design. The re-run must publish the fresh workdir's design and netlist.
group_reuse() {
  local RJ="$W/reuse.json" d=$W/reuse
  mkdir -p "$d"
  cat >"$d/d.v" <<'V'
module reuse_sub(input p, output y);
  assign y = ~p;
endmodule
module reuse_top(input clk, input a, b, c, output x, z, q);
  reg r;
  wire n;
  reuse_sub u(.p(c), .y(n));
  always @(posedge clk) r <= a ^ b;
  assign x = r & n;
  assign z = (a | c) ^ b;
  assign q = r;
endmodule
V
  local R=(synth "$d/d.v" --top reuse_top --set synth.liberty="$LIB" --set synth.mapper="$MAPPER" --workdir "$d/w")
  run "${R[@]}"
  cat >"$d/d.v" <<'V'
module reuse_top(input clk, input a2, b, c, output x, z, q);
  reg r;
  always @(posedge clk) r <= a2 ^ b;
  assign x = r & ~c;
  assign z = (a2 | c) ^ b;
  assign q = r;
endmodule
V
  run "${R[@]}" --emit verilog:"$d/reused.v"
  mv "$d/w" "$d/w_reused"  # the fresh run gets the same paths, so its files are comparable byte for byte
  run "${R[@]}" --emit verilog:"$d/fresh.v"
  # tree_sum of a missing dir prints nothing, and two empty sums compare equal.
  local t
  for t in "$d/w_reused/synth/lg" "$d/w/synth/lg"; do
    [ -f "$t/library.txt" ] || fail "missing compiled library $t"
  done
  [ "$(tree_sum "$d/w_reused/synth/lg")" = "$(tree_sum "$d/w/synth/lg")" ] \
    || fail "a reused workdir compiled a different design: $(diff "$d/w_reused/synth/lg/library.txt" "$d/w/synth/lg/library.txt" | tr '\n' ' ')"
  [ -s "$d/fresh.v" ] || fail "the fresh run emitted no netlist"
  cmp -s "$d/reused.v" "$d/fresh.v" || fail "a reused workdir mapped a different netlist"
  if [ "$MAPPER" = usyn ]; then
    for t in "$d/w_reused/synth/qor.json.usyn.artifacts" "$d/w/synth/qor.json.usyn.artifacts"; do
      [ -d "$t" ] && [ -n "$(ls "$t")" ] || fail "missing USYN artifact tree $t"
    done
    [ "$(tree_sum "$d/w_reused/synth/qor.json.usyn.artifacts")" = "$(tree_sum "$d/w/synth/qor.json.usyn.artifacts")" ] \
      || fail "a reused workdir published different USYN artifacts: $(ls "$d/w_reused/synth/qor.json.usyn.artifacts" | tr '\n' ' ')"
    # Same workdir, mapper switched usyn -> abc: no USYN report may survive
    # next to the ABC run's qor.json (it would be declared as this run's).
    [ -f "$d/w/synth/qor.json.usyn.json" ] || fail "the usyn run wrote no qor.json.usyn.json to go stale"
    run synth "$d/d.v" --top reuse_top --set synth.liberty="$LIB" --set synth.mapper=abc --workdir "$d/w"
    [ "$(jget "$RJ" qor.abc.kind)" = abc-map ] && [ -z "$(jget "$RJ" qor.usyn.kind)" ] \
      || fail "the switched run did not map with abc: $(jget "$RJ" recipe)"
    [ -f "$d/w/synth/qor.json" ] || fail "the abc run after usyn wrote no qor.json"
    [ -z "$(ls "$d/w/synth" | grep '^qor\.json\.usyn')" ] \
      || fail "a usyn report outlived the switch to abc: $(ls "$d/w/synth" | tr '\n' ' ')"
  fi
  echo "PASS: a reused --workdir runs like a fresh one after a top-input rename"
}
spawn group_reuse

# Conditional writes to packed lanes must finish canonicalizing in one compile.
# Otherwise the extra cprop invocation on lg: input changes the ABC regions.
cat >"$W/packed_mux.v" <<'V'
module packed_mux(input [11:0] d, input [8:0] s, output reg [11:0] q);
  integer lane, choice;
  always @* begin
    q = 0;
    for (lane = 0; lane < 3; lane = lane + 1)
      for (choice = 0; choice < 3; choice = choice + 1)
        if (s[3*lane + choice]) q[4*lane +: 4] = d[4*choice +: 4];
  end
endmodule
V
packed_direct() {
  local RJ="$W/packed_direct.json"
  run synth "$W/packed_mux.v" --top packed_mux --set synth.liberty="$LIB" --set synth.mapper="$MAPPER" --set synth.opentimer=false \
      --set lhd.incremental=false --workdir "$W/packed_direct" --emit verilog:"$W/packed_direct.v"
}
group_packed() {
  local RJ="$W/packed_staged.json" p
  packed_direct &
  p=$!
  run compile "$W/packed_mux.v" --emit-dir lg:"$W/packed_lg" --workdir "$W/packed_compile" --set lhd.incremental=false
  run synth lg:"$W/packed_lg" --top packed_mux --set synth.liberty="$LIB" --set synth.mapper="$MAPPER" --set synth.opentimer=false \
      --set lhd.incremental=false --workdir "$W/packed_staged" --emit verilog:"$W/packed_staged.v"
  wait "$p" || exit 1
  if [ "$MAPPER" = abc ]; then
    for metric in regions input_nodes input_ge pred_aig gates area module_gates module_area max_region_depth max_delay; do
      [ "$(jget "$RJ" qor.abc.total.$metric)" = "$(jget "$W/packed_direct.json" qor.abc.total.$metric)" ] \
        || fail "source and lg: synthesis disagree on $metric"
    done
  else
    # the same native decisions (totals, per-region cost stages and search work)
    # and the same technology map (per-region gates/area/depth/delay)
    local metric v
    for metric in 'u["totals"]' \
        '[(r["module"],r["before"],r["after"],r["work"]["total"]) for r in u["regions"]]' \
        '[(r["module"],r["gates"],r["area"],r["logic_depth"],r["delay"]) for r in t["regions"]]'; do
      v=$(jeval "$RJ" "$metric")
      [ -n "$v" ] && [ "$v" = "$(jeval "$W/packed_direct.json" "$metric")" ] || fail "source and lg: synthesis disagree on $metric"
    done
  fi
  cmp -s "$W/packed_direct.v" "$W/packed_staged.v" || fail "source and lg: synthesis produce different mapped Verilog"
  echo "PASS: source and compile+synth produce identical packed-lane mapping"
}
spawn group_packed

# A small br_ram_flops-shaped memory must survive default synthesis. Storage
# is uninitialized, while reset clears the pipelined controls and read output.
# Keep byte enables: they create one memory with two partial write ports.
cat >"$W/ram128_repro.sv" <<'SV'
// One 8 x 16-bit memory, with byte enables and registered read/write controls.
// Reset clears the pipeline registers, but deliberately does not reset storage.
module ram128_repro(
  input clk, rst, wr_valid, rd_valid,
  input [2:0] wr_addr, rd_addr,
  input [15:0] wr_data,
  input [1:0] wr_word_en,
  output reg [15:0] rd_data
);
  reg [15:0] mem [0:7];
  reg [2:0] wr_addr_q;
  reg [15:0] wr_data_q;
  reg [1:0] wr_word_en_q;
  reg wr_valid_q;
  always @(posedge clk) begin
    if (rst) begin
      wr_addr_q <= 0;
      wr_data_q <= 0;
      wr_word_en_q <= 0;
      wr_valid_q <= 0;
      rd_data <= 0;
    end else begin
      wr_addr_q <= wr_addr;
      wr_data_q <= wr_data;
      wr_word_en_q <= wr_word_en;
      wr_valid_q <= wr_valid;
      if (rd_valid) rd_data <= mem[rd_addr];
    end
    if (wr_valid_q) begin
      if (wr_word_en_q[0]) mem[wr_addr_q][7:0] <= wr_data_q[7:0];
      if (wr_word_en_q[1]) mem[wr_addr_q][15:8] <= wr_data_q[15:8];
    end
  end
endmodule
SV
ram128_models() {
  local RJ="$W/ram128_models.json"
  run pass liberty gensim "$LIB" --emit-dir lg:"$W/ram128_models" --workdir "$W/ram128_gensim"
}
group_ram128() {
  local RJ="$W/ram128.json" p
  ram128_models &
  p=$!
  run compile "$W/ram128_repro.sv" --top ram128_repro \
      --emit-dir lg:"$W/ram128_source" --workdir "$W/ram128_compile"
  # No pass.abc.memory override: this must exercise the default lowering policy.
  run synth lg:"$W/ram128_source" --top ram128_repro --set synth.liberty="$LIB" --set synth.mapper="$MAPPER" \
      --emit-dir lg:"$W/ram128_mapped" --emit verilog:"$W/ram128_mapped.v" \
      --workdir "$W/ram128_synth"
  if [ "$MAPPER" = abc ]; then
    [ "$(grep -c '^module cgen_memory_.*_blasted' "$W/ram128_mapped.v")" = 1 ] \
        || fail "128-bit fixture must synthesize exactly one lowered memory"
  else
    # USYN keeps the memory a native barrier: ONE 1-read/2-write memory instance,
    # nothing lowered, and the 38 pipeline-register bits as 38 mapped DFF cells
    [ "$(grep -c '^cgen_memory_1rd_2wr ' "$W/ram128_mapped.v")" = 1 ] \
        || fail "128-bit fixture must keep exactly one native 1rd/2wr memory: $(grep 'cgen_memory' "$W/ram128_mapped.v")"
    ! grep -q '^module cgen_memory_.*_blasted' "$W/ram128_mapped.v" || fail "USYN lowered the native memory barrier"
    [ "$(jeval "$RJ" 'u["totals"]["register_bits"]')" = 38 ] || fail "ram128: USYN must report the 38 pipeline register bits"
    [ "$(grep -cE '^DFF[A-Za-z0-9]* ' "$W/ram128_mapped.v")" = 38 ] || fail "ram128: expected 38 mapped DFF cells"
    ! grep -q 'always @' "$W/ram128_mapped.v" || fail "ram128: a register stayed a native flop"
  fi
  wait "$p" || exit 1
  run lec --ref lg:"$W/ram128_source" --impl lg:"$W/ram128_mapped" \
      --lib lg:"$W/ram128_models" --top ram128_repro --set formal.timeout=30 \
      --workdir "$W/ram128_lec"
  [ "$(jget "$RJ" lec.verdict)" = proven ] || fail "128-bit memory synthesis is not equivalent"
  echo "PASS: default synthesis preserves one 128-bit memory with byte writes"
}
spawn group_ram128

# --- 6. negative controls ----------------------------------------------------
expect_fail() {  # CLASS PATTERN ARGS...
  local cls=$1 pat=$2 j
  shift 2
  j=$(mktemp "$W/neg.XXXXXX") || fail "mktemp"
  if "$LHD" "$@" -q --result-json "$j" 2>"$j.err"; then
    fail "expected failure: $*"
  fi
  [ "$(jget "$j" error.class)" = "$cls" ] || fail "$*: expected error class $cls, got $(cat "$j")"
  grep -q "$pat" "$j" || fail "$*: message lacks '$pat': $(cat "$j")"
}
spawn expect_fail usage "synth.liberty" synth "$W/dut.prp" --top top --set pass.abc.library="$LIB"
spawn expect_fail missing_file "synth.liberty" synth "$W/dut.prp" --top top --set synth.liberty="$W/no_such.lib"
spawn expect_fail usage "manual flow" synth "$W/dut.prp" --top top --set synth.liberty="$LIB" --set color.alg=flat
spawn expect_fail usage "does not emit ln" synth "$W/dut.prp" --top top --set synth.liberty="$LIB" --emit-dir ln:"$W/ln"
spawn expect_fail usage "lhd synth" compile "$W/dut.prp" --top top --emit-dir report:"$W/rep2"
spawn expect_fail usage "unknown synth flag" synth "$W/dut.prp" --top top --set synth.liberty="$LIB" --set synth.potato=1
spawn expect_fail usage "lhd.incremental" synth "$W/dut.prp" --top top --set synth.liberty="$LIB" --set abc.cache=false
spawn expect_fail usage "lhd.incremental" compile "$W/dut.prp" --top top --set compile.cache=false
spawn expect_fail usage "lhd.incremental" lec --impl "$W/dut.prp" --ref "$W/dut.prp" --top top --set formal.cache=false
if [ "$MAPPER" = usyn ]; then
  # the native entry's own guards: one Liberty spelling, the old cover options
  # answer with a migration diagnostic (not an alias), a retired cache flag,
  # tmap's vocabulary, and no STA on logical-only output
  USYN_SYNTH=(synth "$W/dut.prp" --top top --set synth.mapper=usyn)
  spawn expect_fail usage "synth.liberty" "${USYN_SYNTH[@]}" --set pass.usyn.library="$LIB"
  spawn expect_fail missing_file "Liberty" "${USYN_SYNTH[@]}" --set synth.liberty="$W/no_such.lib"
  spawn expect_fail syntax "replaced whole-region cover" "${USYN_SYNTH[@]}" --set synth.liberty="$LIB" --set pass.usyn.support=4
  spawn expect_fail syntax "invalid native USYN" "${USYN_SYNTH[@]}" --set synth.liberty="$LIB" --set pass.usyn.adder=potato
  spawn expect_fail usage "pass.usyn.cache' was removed" "${USYN_SYNTH[@]}" --set synth.liberty="$LIB" --set usyn.cache=false
  spawn expect_fail usage "expects none|abc" "${USYN_SYNTH[@]}" --set synth.liberty="$LIB" --set pass.usyn.tmap=potato
  spawn expect_fail usage "cannot run OpenTimer" "${USYN_SYNTH[@]}" --set pass.usyn.tmap=none --set synth.opentimer=true
fi

# --- 7. the help surface ------------------------------------------------------
help_check() {  # PATTERN MESSAGE ARGS...
  local pat=$1 msg=$2
  shift 2
  "$LHD" "$@" | grep -q "$pat" || fail "$msg"
}
spawn help_check '^usage: lhd synth' "lhd synth --help has no usage line" synth --help --diag-fmt pretty
spawn help_check '"name":"synth"' "lhd help synth (json) is not the synth record" help synth --diag-fmt json
spawn help_check '"name":"synth"' "lhd describe synth missing" describe synth
spawn help_check '"report"' "report: missing from the emit-kind vocabulary" list emit-kinds

# Experimental extraction remains available through an explicit opt-in.
group_reduce() {
  local RJ="$W/reduce.json"
  run "${SYNTH[@]}" --set synth.reduce=true --set synth.opentimer=false --workdir "$W/reduce-opt-in"
  grep -q 'pass.color [^"]*alg:reduce' "$RJ" || fail "synth.reduce=true must enable extraction"
}
spawn group_reduce

rc=0
for p in "${PIDS[@]}"; do wait "$p" || rc=1; done
[ "$rc" = 0 ] || fail "a check above failed"
echo "PASS: negative controls"
echo "PASS: help surface"
echo "PASS: all lhd synth flows"
