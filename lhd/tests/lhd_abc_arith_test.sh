#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# End-to-end test for the pass.abc arithmetic bit-blast (task 2i-abc_arith):
# technology-map a colored combinational arithmetic design (2- and 3-operand
# adders + LT/GT/EQ comparators) to a standard-cell netlist with EACH selectable
# adder architecture, and prove every one equivalent to the original logic with
# `lhd lec` (the graph-native cvc5 engine).
#
#   prp -> lg -> pass color synth
#   pass partition --emit-dir lg:re   (the original-logic twin)
#   pass liberty gensim test.lib --emit-dir lg:models   (cell behavioural models)
#   MAPPER=abc: for adder in rca, cska, cla (+ a non-default block_size):
#       pass abc --set pass.abc.adder=<a> [--set pass.abc.block_size=<n>] -> lg:net
#       lhd lec --impl lg:net --ref lg:re --lib lg:models   (per region)
#   MAPPER=usyn: pass usyn (default; residual=false) -> lg:net, lec --lib
#       as above; pass usyn --set pass.usyn.tmap=none -> lec without --lib
#
# lec flattens the netlist's blackbox standard-cell `Sub` instances inline by
# resolving them against the `--lib` cell-model library (their name-hash gids
# match), so the cvc5 miter compares the mapped gates against the original
# arithmetic directly — no Verilog round-trip. The adder/comparator math itself
# is also proven against reference arithmetic by the graph-free
# //pass/abc:abc_arith_test unit test.
#
# Hermetic: small vendored Liberty (inou/prp/tests/abc/test.lib), not the PDK.

set -u

# One script, both technology mappers: MAPPER=abc (default) runs `lhd pass abc`
# and MAPPER=usyn runs `lhd pass usyn`; lhd/tests/BUILD generates the `_usyn`
# twin from this same file. The equivalence claims (every mapped region, the
# no---lib control, the constant-operand multiply) hold for both. Adder
# USYN rebuilds arithmetic as its own XAG; its adder/adder_block options select
# native lowering. The USYN leg proves ripple and prefix arithmetic equivalent
# under its native optimizer settings (default, residual round off) and its logical tmap=none output,
# checks that a bare `--set` abbreviation resolves to pass.usyn, and that the
# the ABC-only block_size spelling is refused as a usage error.
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
PRP=inou/prp/tests/pyrope/abc_arith.prp
TOP=abc_arith.abc_arith
W="${TEST_TMPDIR:-/tmp/lhd_abc_arith_$$}"
mkdir -p "$W"

fail() { echo "FAIL: $*" >&2; exit 1; }
run() { "$LHD" "$@" -q --result-json "$W/r.json" || fail "$* -> $(cat "$W/r.json" 2>/dev/null)"; }

[ -f "$PRP" ] || fail "missing fixture $PRP"
[ -f "$LIB" ] || fail "missing liberty $LIB"

# Shared once: compile + color, the original-logic twin, the cell models.
run compile "$PRP" --top "$TOP" --emit-dir lg:"$W/lg" --workdir "$W/w1"
run pass color synth --top "$TOP" lg:"$W/lg" --workdir "$W/w2"
run pass partition --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/re" --workdir "$W/w4"
run pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --workdir "$W/w5"

# The synth-colored design decomposes into region modules -- a def with several
# colors becomes `<def>__c<id>` regions under a wrapper, and a def that is one
# whole region is emitted directly under its own name. Either way, every module
# in the partition twin is mapped by abc and lec-checked against its twin.
REGIONS=$(grep -oE '^graph_io [0-9]+ [A-Za-z0-9_.]+' "$W/re/library.txt" | awk '{print $3}' | sort -u)
[ -n "$REGIONS" ] || fail "no region modules in the partition twin: $(cat "$W/re/library.txt")"

# lec_regions <net_dir> <tag> : prove every region of <net_dir> equivalent to re.
lec_regions() {
  local net="$1" tag="$2" r
  for r in $REGIONS; do
    run lec --impl lg:"$net" --ref lg:"$W/re" --lib lg:"$W/models" --top "$r" --workdir "$W/wlec_${tag}"
  done
}

# map_and_lec <adder> <bstag> <set-args...>
map_and_lec() {
  local adder="$1" bstag="$2"; shift 2
  local tag="${adder}_${bstag}"
  rm -rf "$W/net_$tag"
  run pass "$MAPPER" --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net_$tag" --set synth.liberty="$LIB" "$@" --workdir "$W/wa_$tag"
  # the netlist really is a standard-cell netlist (Sub instances of Liberty cells)
  ls "$W/net_$tag"/graph_* >/dev/null 2>&1 || fail "$tag: no mapped netlist emitted"
  lec_regions "$W/net_$tag" "$tag"
  if [ "$MAPPER" = abc ]; then
    echo "LEC PASS (lhd lec): adder=$adder block_size=$bstag"
  else
    echo "LEC PASS (lhd lec): usyn $bstag"
  fi
}

# usyn_report_field <workdir> <python-expr over d> : print one field of the
# schema-5 native decision report (`<qor>.usyn.json`, written for any tmap).
usyn_report_field() {
  python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print(eval(sys.argv[2]))' \
    "$1/qor.json.usyn.json" "$2" || fail "unreadable USYN report $1/qor.json.usyn.json"
}

if [ "$MAPPER" = abc ]; then
  # Default (rca; block_size ignored). Fully-qualified flag.
  map_and_lec rca default --set pass.$MAPPER.adder=rca
  # Carry-skip, auto block_size and an explicit one.
  map_and_lec cska auto --set pass.$MAPPER.adder=cska
  map_and_lec cska 4 --set pass.$MAPPER.adder=cska --set pass.$MAPPER.block_size=4
  # Carry-lookahead, auto and explicit. Also exercise the 2h-set_path abbreviation
  # (`--set adder=cla` after `pass <mapper>` resolves to pass.abc.adder).
  map_and_lec cla auto --set adder=cla
  map_and_lec cla 3 --set pass.$MAPPER.adder=cla --set pass.$MAPPER.block_size=3
  NET_CTRL="$W/net_rca_default"
else
  # Default native flow (tmap=abc: the selected logical network is only
  # technology-mapped). The report describes a mapped CMOS output.
  map_and_lec usyn default
  [ "$(usyn_report_field "$W/wa_usyn_default" 'd["schema_version"],d["kind"],d["tmap"],d["output"]')" \
    = "(5, 'usyn', 'abc', 'mapped-cmos')" ] \
    || fail "usyn default: report is not a schema-5 mapped-cmos decision report: $(head -c 400 "$W/wa_usyn_default/qor.json.usyn.json")"
  # Residual round off: the unoptimized native XAG expansion of the adders and
  # comparators is itself equivalent, and the option is honored (no residual
  # rewrite/resubstitution win, cost unchanged by the round). Also exercise the
  # 2h-set_path abbreviation: bare `--set stack=3` after `pass usyn` resolves to
  # pass.usyn.stack and reaches the reported gate constraints.
  map_and_lec usyn nores --set pass.usyn.residual=false --set stack=3
  [ "$(usyn_report_field "$W/wa_usyn_nores" 'd["constraints"]["stack"]')" = 3 ] \
    || fail "usyn: bare --set stack=3 did not resolve to pass.usyn.stack"
  [ "$(usyn_report_field "$W/wa_usyn_nores" '[(r["residual"]["rewrite_wins"]+r["residual"]["resub_wins"],r["before"]["total"]-r["after_residual"]["total"]) for r in d["regions"]]' \
    | tr -d ' ')" = "[(0,0)]" ] \
    || fail "usyn: pass.usyn.residual=false still ran the residual round"
  # Logical CMOS output (tmap=none): no Liberty and no mapped cells, so the
  # native output proves equivalent WITHOUT --lib.
  rm -rf "$W/net_usyn_none"
  run pass usyn --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net_usyn_none" --set pass.usyn.tmap=none --workdir "$W/wa_usyn_none"
  [ "$(usyn_report_field "$W/wa_usyn_none" 'd["tmap"],d["output"]')" = "('none', 'logical-cmos')" ] \
    || fail "usyn tmap=none: report is not logical-cmos"
  for r in $REGIONS; do
    run lec --impl lg:"$W/net_usyn_none" --ref lg:"$W/re" --top "$r" --workdir "$W/wlec_usyn_none"
  done
  echo "LEC PASS (lhd lec): usyn tmap=none logical output, no --lib"
  map_and_lec usyn rca --set pass.usyn.adder=rca
  map_and_lec usyn prefix --set adder=prefix
  # The ABC block_size spelling remains absent from pass.usyn: a usage error
  # (exit 2) naming the flag, with no netlist emitted.
  for opt in block_size=4; do
    rm -rf "$W/net_usyn_removed"
    rc=0
    "$LHD" pass usyn --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net_usyn_removed" --set synth.liberty="$LIB" \
      --set "$opt" --workdir "$W/wa_usyn_removed" -q --result-json "$W/rr.json" 2>/dev/null || rc=$?
    [ "$rc" = 2 ] || fail "usyn: removed option --set $opt exited $rc (expected usage error 2)"
    flag="${opt%%=*}"
    grep -q "unknown flag '${flag#pass.usyn.}'" "$W/rr.json" \
      || fail "usyn: removed option --set $opt: no unknown-flag diagnostic: $(cat "$W/rr.json")"
    [ ! -e "$W/net_usyn_removed" ] || fail "usyn: removed option --set $opt still emitted a netlist"
  done
  NET_CTRL="$W/net_usyn_default"
fi

# Control: the PROVEN results are load-bearing on the cell models. Without
# --lib, the netlist's blackbox cell `Sub`s are unresolved, so lec must NOT
# prove equivalence (sound Unknown / fail), never a vacuous pass. The impl-only
# box obligations gate the miter to INCONCLUSIVE (an incomplete correspondence
# can neither prove nor refute), which exits 0 unless strict — so run the
# control strict, where any non-Proven outcome is a hard failure exit.
one_region=$(echo "$REGIONS" | head -1)
if "$LHD" lec --impl lg:"$NET_CTRL" --ref lg:"$W/re" --top "$one_region" \
    --workdir "$W/wlec_nolib" -q --result-json "$W/rn.json" 2>/dev/null; then
  fail "lec proved equivalence with no --lib (unresolved cells must not vacuously pass)"
fi

if [ "$MAPPER" = abc ]; then
  echo "PASS: pass.abc adders (rca/cska/cla) all lhd-lec-equivalent to original arithmetic"
else
  echo "PASS: pass.usyn arithmetic (mapped, residual off, tmap=none) all lhd-lec-equivalent to original arithmetic"
fi

# ---------------------------------------------------------------------------
# Constant-operand multiply (const-mult miscompile regression): a Mult fed by a
# bare constant driver (342) used to be read at effective width 1 by the
# bit-blast (constant pins carry no bits attr, so eff_width clamped them),
# collapsing the whole product cone to constant 0 — every mapped output bit a
# _const0_ tie cell, no diagnostic. Oracle: the mapped netlist must LEC-prove
# against the original-logic partition twin (an all-const0 netlist REFUTES).
# No pass color: the whole design folds into the color-0 region, keeping the
# mult inside the bit-blasted cone (synth coloring excludes mult region seeds).
# ---------------------------------------------------------------------------
CPRP=inou/prp/tests/pyrope/abc_constmul.prp
CTOP=abc_constmul.abc_constmul
C="$W/constmul"
mkdir -p "$C"
[ -f "$CPRP" ] || fail "missing fixture $CPRP"
run compile "$CPRP" --top "$CTOP" --emit-dir lg:"$C/lg" --workdir "$C/w1"
# uncolored pass <mapper> warns once (color-0 region) — tolerated by run()'s exit check
run pass "$MAPPER" --top "$CTOP" lg:"$C/lg" --emit-dir lg:"$C/net" --set synth.liberty="$LIB" --workdir "$C/w2"
run pass partition --top "$CTOP" lg:"$C/lg" --emit-dir lg:"$C/re" --workdir "$C/w3"
CREGIONS=$(grep -oE '^graph_io [0-9]+ [A-Za-z0-9_.]+' "$C/re/library.txt" | awk '{print $3}' | sort -u)
[ -n "$CREGIONS" ] || fail "no region modules in the constmul partition twin: $(cat "$C/re/library.txt")"
for r in $CREGIONS; do
  run lec --impl lg:"$C/net" --ref lg:"$C/re" --lib lg:"$W/models" --top "$r" --workdir "$C/wlec"
done
echo "PASS: pass.$MAPPER constant-operand multiply lhd-lec-equivalent (no const0 collapse)"
