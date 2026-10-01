#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# End-to-end test for INCREMENTAL `lhd pass <mapper>` (todo/livehd/2opt-incr A+C):
# a persistent region cache (under --workdir, lhd.incremental) content-addressed by a
# canonical region digest. The properties under test, in order of importance:
#
#   1. SOUNDNESS: a netlist assembled from cached + fresh regions is
#      LEC-equivalent to the original logic (a wrong reuse is a miscompile).
#   2. The NoChange edit: re-running on an unchanged design is all hits and
#      zero ABC runs -- the case both commercial flows burn ~106s on (Anubis:
#      24 of 144 changes are no-ops they fail to detect).
#   3. The incremental edit: editing ONE module re-synthesizes only the
#      regions it touched; the rest clone from the cache -- across defs AND
#      across the nid shifts a recompile inflicts on untouched logic.
#   4. A warm clone is BYTE-IDENTICAL Verilog to the cold mapping.
#
# Hermetic: the vendored Liberty (inou/prp/tests/abc/test.lib), no PDK.
#
# MAPPER=usyn has TWO independent reuse tiers under the same lhd.incremental
# switch and --workdir (pass/usyn/README.md): the native logical cache
# (<workdir>/usyn_cache/<key>.usyn-cache; the schema-5 report
# <qor>.usyn.json carries cache.{reused,misses,stored} and per-region
# cache_reused/cache_key) and the technology-map cache
# (<workdir>/usyn_cache/tmap, an atomic `current` generation pointer; qor.json /
# the envelope's qor is the technology-map report with its own
# incremental.{hits,misses} and per-region cache=hit|miss|disabled). Every
# property above is asserted per tier. ABC-only facts stay gated: the
# abc.adder/multiplier/barrel recipe options (USYN does not inherit abc.*), the
# abc-map `resynth` rows / `abc_started` / abc[stats] rendering, and
# <workdir>/abc_cache/abc_cache.json. "Starts no ABC" becomes "every
# technology-map row is a cache hit" (nothing is mapped).
set -u

# One script, both technology mappers: MAPPER=abc (default) runs `lhd pass abc`
# and MAPPER=usyn runs `lhd pass usyn`. lhd/tests/BUILD generates the `_usyn`
# twin from this same file.
MAPPER="${MAPPER:-abc}"
case "$MAPPER" in
  abc | usyn) ;;
  *)
    echo "FAIL: bad MAPPER=$MAPPER (expected abc|usyn)" >&2
    exit 1
    ;;
esac

# pass.abc recipe options (word-split on purpose; no value holds a space).
if [ "$MAPPER" = abc ]; then
  MAP_OPTS="--set abc.adder=rca --set abc.multiplier=array --set abc.barrel=log"
else
  MAP_OPTS=""
fi

LHD=lhd/lhd
LIB=inou/prp/tests/abc/test.lib
FIX=inou/prp/tests/pyrope/hier_seq.prp
W="${TEST_TMPDIR:-/tmp/lhd_abc_incr_$$}"
TOP=dut.top
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}
run() { "$LHD" "$@" -q --result-json "$W/r.json" || fail "$* -> $(cat "$W/r.json" 2>/dev/null)"; }

# hits/misses from the LAST run's qor report: qor.incremental.{hits,misses,abc_started}
# ("" when the cache did not run; the envelope's own `incremental.abc` member
# then still says enabled=false, which is why this reads the qor object).
incr_field() {
  python3 - "$W/r.json" "$1" <<'PY'
import json, sys
try:
    print(json.load(open(sys.argv[1]))["qor"]["incremental"][sys.argv[2]])
except Exception:
    print("")
PY
}
region_count() { grep -o '"resynth":[01]' "$W/r.json" | wc -l | tr -d ' '; }
resynth_count() { grep -o '"resynth":1' "$W/r.json" | wc -l | tr -d ' '; }
expect_incr() {
  local h m
  h=$(incr_field hits)
  m=$(incr_field misses)
  [ "$h" = "$1" ] || fail "$3: expected $1 hit(s), got '$h' -- $(cat "$W/r.json" | head -c 400)"
  [ "$m" = "$2" ] || fail "$3: expected $2 miss(es), got '$m'"
}
expect_resynth() {
  [ "$(region_count)" = "$1" ] || fail "$3: expected $1 color row(s), got $(region_count)"
  [ "$(resynth_count)" = "$2" ] || fail "$3: expected $2 resynthesized color(s), got $(resynth_count)"
}

# --- MAPPER=usyn helpers ------------------------------------------------------
NATIVE="$W/wabc/qor.json.usyn.json"
tmap_regions() {
  python3 -c 'import json, sys; print(len(json.load(open(sys.argv[1]))["qor"]["regions"]))' "$W/r.json" 2>/dev/null
}
native_regions() {
  python3 -c 'import json, sys; print(len(json.load(open(sys.argv[1]))["regions"]))' "$NATIVE" 2>/dev/null
}
# tmap_expect LABEL HITS MISSES: the LAST run's technology-map tier (the
# envelope's qor), cross-checked against its own per-region rows.
tmap_expect() {
  python3 - "$W/r.json" "$@" <<'PY'
import json, sys
r = json.load(open(sys.argv[1]))
label = sys.argv[2]
th, tm = int(sys.argv[3]), int(sys.argv[4])
q = r["qor"]
inc, rows = q["incremental"], q["regions"]
assert q["kind"] == "technology-map", f"{label}: qor is not the technology-map report: {q.get('kind')}"
got = (inc["hits"], inc["misses"])
assert inc["enabled"] and got == (th, tm), f"{label}: technology-map hits/misses {got}, expected {(th, tm)}: {inc}"
assert r["incremental"]["abc"]["enabled"] and r["incremental"]["abc"]["hits"] == th, f"{label}: envelope {r['incremental']}"
per_row = (sum(x["cache"] == "hit" for x in rows), sum(x["cache"] == "miss" for x in rows))
assert len(rows) == th + tm and per_row == (th, tm), f"{label}: technology-map rows {per_row} vs counters {got}: {rows}"
assert not inc["invalid"] and not inc["store_failed"], f"{label}: {inc}"
PY
}
# native_expect LABEL REUSED MISSES: the LAST run's native logical tier (the
# schema-5 report), cross-checked against its own per-region rows.
native_expect() {
  python3 - "$NATIVE" "$@" <<'PY'
import json, sys
n = json.load(open(sys.argv[1]))
label = sys.argv[2]
nr, nm = int(sys.argv[3]), int(sys.argv[4])
assert n["schema_version"] == 5 and n["tmap"] == "abc" and n["output"] == "mapped-cmos", f"{label}: {n.get('output')}"
c = n["cache"]
got = (c["reused"], c["misses"])
assert c["enabled"] and got == (nr, nm), f"{label}: native reused/misses {got}, expected {(nr, nm)}: {c}"
per_row = sum(bool(x["cache_reused"]) for x in n["regions"])
assert len(n["regions"]) == nr + nm and per_row == nr, f"{label}: native rows reused={per_row} vs counters {got}"
assert c["stored"] == nm and not c["invalid"] and not c["refused"] and not c["store_failures"], f"{label}: {c}"
PY
}
# usyn_disabled LABEL [native [REPORT]]: the last run touched no cache at
# either tier. `native` also checks the native logical tier, read from REPORT
# (default $NATIVE).
usyn_disabled() {
  local report="$NATIVE"
  [ $# -ge 3 ] && report="$3"
  python3 - "$W/r.json" "$report" "$1" ${2:+"$2"} <<'PY'
import json, sys
r = json.load(open(sys.argv[1]))
label = sys.argv[3]
q = r["qor"]
inc = q["incremental"]
assert not r["incremental"]["abc"]["enabled"] and not inc["enabled"] and inc["hits"] == 0, f"{label}: {inc}"
assert q["regions"] and all(x["cache"] == "disabled" for x in q["regions"]), f"{label}: {q['regions']}"
if len(sys.argv) > 4:
    n = json.load(open(sys.argv[2]))
    c = n["cache"]
    assert not c["enabled"] and c["reused"] == 0 and c["stored"] == 0 and c["io_work"] == 0, f"{label}: {c}"
    assert not any(x["cache_reused"] for x in n["regions"]), f"{label}: {n['regions']}"
PY
}
cache_listing() { (cd "$W/wabc/usyn_cache" && find . | LC_ALL=C sort); }

[ -f "$FIX" ] || fail "missing fixture $FIX"
[ -f "$LIB" ] || fail "missing liberty $LIB"

# The def names embed the FILE name (internal naming = file.entity), so the
# edited and unedited versions must live under the SAME file name for their
# defs to be the same entities -- exactly like a real edit-in-place.
cp "$FIX" "$W/dut.prp"

compile_and_color() {  # $1 = lg dir tag
  run compile "$W/dut.prp" --top "$TOP" --emit-dir lg:"$W/$1" --workdir "$W/w_c$1"
  run pass color synth --top "$TOP" lg:"$W/$1" --workdir "$W/w_k$1"
}

abc_incr() {  # $1 = input lg tag, $2 = out tag
  # ONE shared --workdir across every abc run: the cache lives under it
  # (<workdir>/<mapper>_cache), on by default (lhd.incremental).
  run pass "$MAPPER" --top "$TOP" lg:"$W/$1" --emit-dir lg:"$W/$2" --set synth.liberty="$LIB" $MAP_OPTS \
      --workdir "$W/wabc" --stats
}

# LEC gate: netlist modules + behavioral cell models vs the original logic
# re-emitted through pass.partition (same module structure, original logic).
run pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --workdir "$W/w_m"
run compile lg:"$W/models" --emit-dir verilog:"$W/modelsv" --workdir "$W/w_mv"
lec_gate() {  # $1 = net tag, $2 = lg tag, $3 = label
  run pass partition --top "$TOP" lg:"$W/$2" --emit-dir lg:"$W/re$1" --workdir "$W/w_p$1"
  run compile lg:"$W/$1" --top "$TOP" --emit-dir verilog:"$W/${1}v" --workdir "$W/w_nv$1"
  run compile lg:"$W/re$1" --top "$TOP" --emit-dir verilog:"$W/re${1}v" --workdir "$W/w_rv$1"
  cat "$W/${1}v/"*.v "$W/modelsv/"*.v > "$W/impl$1.v"
  cat "$W/re${1}v/"*.v > "$W/ref$1.v"
  run lec --impl verilog:"$W/impl$1.v" --ref verilog:"$W/ref$1.v" --top "$TOP" --workdir "$W/w_l$1"
  echo "PASS: $3 is LEC-equivalent"
}

# Ware selection is independently tested in lhd_ware_test. This test pins
# the baseline region cache without context-dependent re-mapping.
#
# HOW MANY regions the shipped coloring opens is a QoR choice that moves
# whenever the coloring is tuned (colouring the flat view of the hierarchy took
# this fixture from 8 regions to 2). What this test is about is the hit/miss
# SPLIT, so `N` is read from the cold run and every later count is expressed
# against it. The invariants: cold = all miss, warm = all hit, and a one-def
# edit = exactly TWO misses (specialized adder plus its wrapper).
#
# MAPPER=usyn follows the same (default, abc-cut) coloring: pass.usyn's
# flatten=auto keeps its regions, and the register-to-register USYN profile
# would put this whole fixture in ONE region -- too few to tell hits from
# misses. N counts technology-map regions (the tmap also maps the wrapper) and
# NN native regions (the wrapper has no logic to select).

# --- 1. cold run: every region misses and is stored --------------------------
compile_and_color lg0
abc_incr lg0 net0
if [ "$MAPPER" = abc ]; then
  N=$(region_count)
  [ -n "$N" ] && [ "$N" -ge 2 ] || fail "cold run reported $N region(s); this test needs several to tell hits from misses"
  expect_incr 0 "$N" "cold run"
  expect_resynth "$N" "$N" "cold run"
  [ "$(incr_field abc_started)" = 1 ] || fail "cold run did not start ABC"
  [ -f "$W/wabc/${MAPPER}_cache/abc_cache.json" ] \
    || fail "cache metadata not persisted under <workdir>/${MAPPER}_cache"
else
  N=$(tmap_regions)
  NN=$(native_regions)
  [ -n "$N" ] && [ "$N" -ge 2 ] || fail "cold run reported $N technology-map region(s); this test needs several"
  [ -n "$NN" ] && [ "$NN" -ge 2 ] || fail "cold run reported $NN native region(s); this test needs several"
  native_expect "cold run" 0 "$NN" || fail "cold run: native tier was not all misses + stores"
  tmap_expect "cold run" 0 "$N" || fail "cold run: technology-map tier was not all misses"
  # Both tiers persist under <workdir>/usyn_cache: one native entry per
  # reported region key, and a published technology-map generation.
  python3 - "$NATIVE" "$W/wabc/usyn_cache" <<'PY' || fail "cache entries not persisted under <workdir>/usyn_cache"
import json, os, sys
n = json.load(open(sys.argv[1]))
root = sys.argv[2]
for region in n["regions"]:
    key = region["cache_key"]
    assert len(key) == 64 and os.path.isfile(os.path.join(root, key + ".usyn-cache")), region["module"]
generation = open(os.path.join(root, "tmap", "current")).read().strip()
assert generation.startswith("entry-") and os.path.isfile(os.path.join(root, "tmap", generation, "integrity")), generation
PY
  cp "$NATIVE" "$W/cold-usyn.json"
fi
lec_gate net0 lg0 "cold mapping"
if [ "$MAPPER" = usyn ]; then
  # CMOS output keeps exactly the original registers -- 4 delayer + 2
  # stage_unit u8 regs = 48 bits -- all mapped onto the library flop, with no
  # native flop left behind and no added state.
  python3 - "$NATIVE" "$W/net0v" <<'PY' || fail "cold USYN mapping did not keep exactly the 48 source register bits"
import glob, json, re, sys
t = json.load(open(sys.argv[1]))["totals"]
assert t["register_bits"] == 48 and t["eligible_endpoints"] == 48, t
text = "".join(open(f).read() for f in glob.glob(sys.argv[2] + "/*.v"))
flops = len(re.findall(r"^\s*DFFx1\s", text, re.M))
assert flops == 48, flops
assert "always_ff" not in text and not re.search(r"always\s*@\s*\(\s*(pos|neg)edge", text), "native flop in the mapped netlist"
PY
fi

# --- 2. NoChange: same design, fresh out dir => all hits, zero ABC ----------
abc_incr lg0 net1
if [ "$MAPPER" = abc ]; then
  expect_incr "$N" 0 "NoChange re-run"
  expect_resynth "$N" 0 "NoChange re-run"
  [ "$(incr_field abc_started)" = 0 ] || fail "all-hit run still started ABC/read Liberty"
else
  native_expect "NoChange re-run" "$NN" 0 || fail "NoChange re-run: native tier was not all reused"
  tmap_expect "NoChange re-run" "$N" 0 || fail "NoChange re-run: technology-map tier was not all hits (ABC mapped)"
  # A hit replays the retained endpoint decisions under the same region keys.
  python3 - "$W/cold-usyn.json" "$NATIVE" <<'PY' || fail "a native cache hit changed the reported decisions"
import json, sys
cold = json.load(open(sys.argv[1]))
warm = json.load(open(sys.argv[2]))
assert len(cold["regions"]) == len(warm["regions"])
for old, new in zip(cold["regions"], warm["regions"]):
    assert old["cache_key"] == new["cache_key"], old["module"]
    for key in old:
        if key != "cache_reused":
            assert old[key] == new[key], (old["module"], key)
assert cold["totals"] == warm["totals"], (cold["totals"], warm["totals"])
PY
fi
run compile lg:"$W/net1" --top "$TOP" --emit-dir verilog:"$W/net1v" --workdir "$W/w_nv1"
diff -r "$W/net0v" "$W/net1v" >/dev/null || fail "warm clone differs from the cold mapping"
echo "PASS: NoChange run is all hits and byte-identical Verilog"

# Pretty rendering is one physical line per color and carries the same
# resynthesis decision as the JSON rows. This additional all-hit run is cheap.
"$LHD" pass "$MAPPER" --top "$TOP" lg:"$W/lg0" --emit-dir lg:"$W/net_pretty" --set synth.liberty="$LIB" $MAP_OPTS \
    --workdir "$W/wabc" --stats --diag-fmt pretty -q >"$W/pretty.out" \
    || fail "pretty stats run failed"
if [ "$MAPPER" = abc ]; then
  [ "$(grep -c '^  abc\[stats\]:' "$W/pretty.out")" = "$N" ] \
    || fail "pretty stats did not print exactly one line per color: $(cat "$W/pretty.out")"
  [ "$(grep -c 'resynth=0$' "$W/pretty.out")" = "$N" ] \
    || fail "pretty all-hit rows did not all say resynth=0: $(cat "$W/pretty.out")"
else
  # USYN renders one tmap[stats] row per technology-map region (the rows carry
  # no reuse flag); the reuse decision is the incremental[stats] summary.
  [ "$(grep -c '^  tmap\[stats\]:' "$W/pretty.out")" = "$N" ] \
    || fail "pretty stats did not print exactly one line per technology-map region: $(cat "$W/pretty.out")"
  grep -q "^  incremental\[stats\]: abc enabled=true regions=$N hits=$N misses=0 " "$W/pretty.out" \
    || fail "pretty all-hit summary did not say hits=$N misses=0: $(cat "$W/pretty.out")"
fi

# --- 3. edit top's combiner; sequential child logic must still hit ----------
# Specializing the new constant changes the ware definition AND the wrapper's
# callee identity. Both must miss; the unrelated sequential region must hit.
# The recompile reallocates every nid; the child defs' regions must hit anyway.
sed 's/o = a + b/o = a + b + 1/' "$FIX" > "$W/dut.prp"
grep -q "o = a + b + 1" "$W/dut.prp" || fail "edit did not apply"
compile_and_color lg1
abc_incr lg1 net2
if [ "$MAPPER" = abc ]; then
  expect_incr "$((N - 2))" 2 "top-only edit"
  expect_resynth "$N" 2 "top-only edit"
  [ "$(incr_field abc_started)" = 1 ] || fail "adder edit did not start ABC"
else
  # Technology-map tier: the re-selected combiner region and the wrapper whose
  # callee interface changed miss; the sequential region hits.
  tmap_expect "top-only edit" "$((N - 2))" 2 \
    || fail "top-only edit: technology-map tier did not miss exactly the combiner + wrapper"
  # Native tier: every region search gets the same credits (pass.usyn.work),
  # independent of the design-size change and of other regions, and the edit
  # only moves constant-pool slots and node numbers in the sequential region,
  # which are not identity -- so exactly one miss, on the edited combiner (the
  # last region), and the sequential region is reused.
  native_expect "top-only edit" "$((NN - 1))" 1 \
    || fail "top-only edit: native tier did not miss exactly the edited combiner"
  python3 -c 'import json, sys; assert not json.load(open(sys.argv[1]))["regions"][-1]["cache_reused"]' "$NATIVE" \
    || fail "top-only edit: the native miss was not the last (edited combiner) region"
fi
lec_gate net2 lg1 "edited design ($((N - 2)) cached + 2 fresh regions)"

# --- 4. the edited design is now cached too ----------------------------------
abc_incr lg1 net3
if [ "$MAPPER" = abc ]; then
  expect_incr "$N" 0 "NoChange after the edit"
  expect_resynth "$N" 0 "NoChange after the edit"
  [ "$(incr_field abc_started)" = 0 ] || fail "all-hit edited run still started ABC/read Liberty"
else
  native_expect "NoChange after the edit" "$NN" 0 || fail "NoChange after the edit: native tier was not all reused"
  # The wrapper stored by the edit run must hit now. Its combiner child kept
  # its name but changed its interface; the region cache refreshes that
  # child's decl in the cached pre-body library (pass/synth/region_cache.cpp
  # copy_pre_children), so the wrapper's exact comparison matches instead of
  # re-mapping it on every run after the edit.
  tmap_expect "NoChange after the edit" "$N" 0 \
    || fail "NoChange after the edit: technology-map tier was not all hits (the wrapper was re-mapped)"
  run compile lg:"$W/net3" --top "$TOP" --emit-dir verilog:"$W/net3v" --workdir "$W/w_nv3"
  diff -r "$W/net2v" "$W/net3v" >/dev/null || fail "warm clone of the edited design differs from its fresh mapping"
fi

# --- 5. the off switch and the no-workdir gate --------------------------------
# lhd.incremental=false: no cache is touched and the envelope carries no counters.
[ "$MAPPER" = abc ] || cache_listing >"$W/cache_before.txt"
run pass "$MAPPER" --top "$TOP" lg:"$W/lg1" --emit-dir lg:"$W/net4" --set synth.liberty="$LIB" $MAP_OPTS \
    --set lhd.incremental=false --workdir "$W/wabc" --stats
if [ "$MAPPER" = abc ]; then
  [ -z "$(incr_field hits)" ] || fail "lhd.incremental=false still ran the cache"
  expect_resynth "$N" "$N" "cache-disabled full run"
else
  # USYN keeps its counters, reporting enabled=false with no reuse at either tier.
  usyn_disabled "cache-disabled full run" native || fail "lhd.incremental=false still used a USYN cache"
  [ "$(tmap_regions)" = "$N" ] || fail "cache-disabled run reported $(tmap_regions) technology-map region(s), expected $N"
  cache_listing | cmp -s - "$W/cache_before.txt" || fail "lhd.incremental=false wrote into <workdir>/usyn_cache"
fi
# No user --workdir: nowhere durable to cache, so the cache stays off even at
# its default of true. USYN's native decision report survives only under a
# user --workdir, so pass.usyn.qor puts it where the native tier can be checked
# too (a fresh directory: a stale report must not pass for this run's).
if [ "$MAPPER" = abc ]; then
  run pass "$MAPPER" --top "$TOP" lg:"$W/lg1" --emit-dir lg:"$W/net5" --set synth.liberty="$LIB" $MAP_OPTS --stats
  [ -z "$(incr_field hits)" ] || fail "no --workdir must mean no cache"
  expect_resynth "$N" "$N" "no-workdir full run"
else
  rm -rf "$W/nowd"
  run pass "$MAPPER" --top "$TOP" lg:"$W/lg1" --emit-dir lg:"$W/net5" --set synth.liberty="$LIB" $MAP_OPTS \
      --set pass.usyn.qor="$W/nowd/qor.json" --stats
  [ -f "$W/nowd/qor.json.usyn.json" ] || fail "no-workdir run wrote no native report under pass.usyn.qor"
  usyn_disabled "no-workdir full run" native "$W/nowd/qor.json.usyn.json" \
    || fail "no --workdir must mean no USYN cache at either tier"
  [ "$(tmap_regions)" = "$N" ] || fail "no-workdir run reported $(tmap_regions) technology-map region(s), expected $N"
fi
echo "PASS: lhd.incremental=false and no-workdir both disable cleanly"

echo "PASS: all incremental pass.$MAPPER flows"
