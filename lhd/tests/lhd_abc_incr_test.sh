#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# End-to-end test for INCREMENTAL `lhd pass abc` (todo/livehd/2opt-incr A+C):
# a persistent region cache (--set abc.cache=DIR) content-addressed by a
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
set -u

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

# hits/misses from the LAST run's qor envelope: "incremental":{"hits":H,"misses":M,...}
incr_field() { sed -n 's/.*"incremental":{[^}]*"'"$1"'":\([0-9]*\).*/\1/p' "$W/r.json"; }
expect_incr() {
  local h m
  h=$(incr_field hits)
  m=$(incr_field misses)
  [ "$h" = "$1" ] || fail "$3: expected $1 hit(s), got '$h' -- $(cat "$W/r.json" | head -c 400)"
  [ "$m" = "$2" ] || fail "$3: expected $2 miss(es), got '$m'"
}

[ -f "$FIX" ] || fail "missing fixture $FIX"
[ -f "$LIB" ] || fail "missing liberty $LIB"

# The def names embed the FILE name (internal naming = file.entity), so the
# edited and unedited versions must live under the SAME file name for their
# defs to be the same entities -- exactly like a real edit-in-place.
cp "$FIX" "$W/dut.prp"

compile_and_color() {  # $1 = lg dir tag
  run compile "$W/dut.prp" --top "$TOP" --recipe O1 --emit-dir lg:"$W/$1" --workdir "$W/w_c$1"
  # absorb=false: the tiny fixture defs would otherwise inline away and the
  # per-def region reuse this test pins would have nothing to bite on.
  run pass color synth --top "$TOP" --set color.absorb=false lg:"$W/$1" --workdir "$W/w_k$1"
}

abc_incr() {  # $1 = input lg tag, $2 = out tag
  # ONE shared --workdir across every abc run: the cache lives under it
  # (<workdir>/abc_cache, the formal.cache convention), on by default.
  run pass abc --top "$TOP" lg:"$W/$1" --emit-dir lg:"$W/$2" --set abc.library="$LIB" \
      --workdir "$W/wabc"
}

# LEC gate: netlist modules + behavioral cell models vs the original logic
# re-emitted through pass.partition (same module structure, original logic).
run pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --workdir "$W/w_m"
run compile lg:"$W/models" --recipe O0 --emit-dir verilog:"$W/modelsv" --workdir "$W/w_mv"
lec_gate() {  # $1 = net tag, $2 = lg tag, $3 = label
  run pass partition --top "$TOP" lg:"$W/$2" --emit-dir lg:"$W/re$1" --workdir "$W/w_p$1"
  run compile lg:"$W/$1" --top "$TOP" --recipe O0 --emit-dir verilog:"$W/${1}v" --workdir "$W/w_nv$1"
  run compile lg:"$W/re$1" --top "$TOP" --recipe O0 --emit-dir verilog:"$W/re${1}v" --workdir "$W/w_rv$1"
  cat "$W/${1}v/"*.v "$W/modelsv/"*.v > "$W/impl$1.v"
  cat "$W/re${1}v/"*.v > "$W/ref$1.v"
  run lec --set formal.solver=lgyosys --impl verilog:"$W/impl$1.v" --ref verilog:"$W/ref$1.v" --top "$TOP" --workdir "$W/w_l$1"
  echo "PASS: $3 is LEC-equivalent"
}

# --- 1. cold run: every region misses and is stored --------------------------
compile_and_color lg0
abc_incr lg0 net0
expect_incr 0 3 "cold run"
[ -f "$W/wabc/abc_cache/abc_cache.json" ] || fail "cache metadata not persisted under <workdir>/abc_cache"
lec_gate net0 lg0 "cold mapping"

# --- 2. NoChange: same design, fresh out dir => all hits, zero ABC ----------
abc_incr lg0 net1
expect_incr 3 0 "NoChange re-run"
run compile lg:"$W/net1" --top "$TOP" --recipe O0 --emit-dir verilog:"$W/net1v" --workdir "$W/w_nv1"
diff -r "$W/net0v" "$W/net1v" >/dev/null || fail "warm clone differs from the cold mapping"
echo "PASS: NoChange run is all hits and byte-identical Verilog"

# --- 3. edit ONE def (top's combiner); children must still hit ---------------
# The recompile reallocates every nid; the child defs' regions must hit anyway.
sed 's/o = a + b/o = a + b + 1/' "$FIX" > "$W/dut.prp"
grep -q "o = a + b + 1" "$W/dut.prp" || fail "edit did not apply"
compile_and_color lg1
abc_incr lg1 net2
expect_incr 2 1 "top-only edit"
lec_gate net2 lg1 "edited design (2 cached + 1 fresh region)"

# --- 4. the edited design is now cached too ----------------------------------
abc_incr lg1 net3
expect_incr 3 0 "NoChange after the edit"

# --- 5. the off switch and the no-workdir gate --------------------------------
# cache=false: no cache is touched and the envelope carries no counters.
run pass abc --top "$TOP" lg:"$W/lg1" --emit-dir lg:"$W/net4" --set abc.library="$LIB" \
    --set abc.cache=false --workdir "$W/wabc"
[ -z "$(incr_field hits)" ] || fail "cache=false still ran the cache"
# No user --workdir: nowhere durable to cache, so the cache stays off even at
# its default of true (the formal.cache convention).
run pass abc --top "$TOP" lg:"$W/lg1" --emit-dir lg:"$W/net5" --set abc.library="$LIB"
[ -z "$(incr_field hits)" ] || fail "no --workdir must mean no cache"
echo "PASS: cache=false and no-workdir both disable cleanly"

echo "PASS: all incremental pass.abc flows"
