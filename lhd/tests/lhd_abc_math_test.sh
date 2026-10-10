#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# End-to-end test for the pass.abc multiplier / right-shift bit-blast and the
# division bit-blast (task 2a-abc follow-on): technology-map a colored
# combinational design that uses `*`, `>>` and `/` to a standard-cell netlist and
# prove the mapped hierarchy equivalent to the original logic with `lhd lec` (the
# graph-native cvc5 engine).
#
#   prp -> lg -> pass color acyclic   (colors EVERY op, incl mult/div, into
#                                            regions; synth deliberately excludes
#                                            mult/div as region seeds)
#   pass partition --emit-dir lg:re         (the original-logic twin)
#   pass liberty gensim test.lib --emit-dir lg:models
#   pass <mapper> --emit-dir lg:net              (bit-blast mult/sra/div)
#   lhd lec --impl lg:net --ref lg:re --lib lg:models   (complete hierarchy)
#   MAPPER=abc: + a non-default adder (pass.abc.adder=cska), lec --lib
#   MAPPER=usyn: + pass usyn --set pass.usyn.tmap=none, lec without --lib
#
# Coverage: unsigned + signed + n-ary multiply (array multiplier), logical +
# arithmetic right shift (barrel shifter), and unsigned + signed division.
# Divider widths must preserve the full operands even for a narrow quotient.
# Arithmetic builders are also checked independently with software bit models.
#
# Hermetic: small vendored Liberty (inou/prp/tests/abc/test.lib), not the PDK.

set -u

# One script, both technology mappers: MAPPER=abc (default) runs `lhd pass abc`
# and MAPPER=usyn runs `lhd pass usyn`; lhd/tests/BUILD generates the `_usyn`
# twin from this same file. The mapped-hierarchy equivalence, the mapped divider
# and the no---lib control hold for both. The USYN leg proves its logical
# tmap=none output with native CSKA lowering (no Liberty, no cells) equivalent
# without --lib and checks that an unregistered flag is refused.
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
PRP=inou/prp/tests/pyrope/abc_mathops.prp
TOP=abc_mathops.abc_mathops
W="${TEST_TMPDIR:-/tmp/lhd_abc_math_$$}"
mkdir -p "$W"

fail() { echo "FAIL: $*" >&2; exit 1; }
run() { "$LHD" "$@" -q --result-json "$W/r.json" || fail "$* -> $(cat "$W/r.json" 2>/dev/null)"; }

[ -f "$PRP" ] || fail "missing fixture $PRP"
[ -f "$LIB" ] || fail "missing liberty $LIB"

# Shared: compile + color (acyclic so mult/div land in regions), original twin, models.
run compile "$PRP" --top "$TOP" --emit-dir lg:"$W/lg" --workdir "$W/w1"
run pass color acyclic --top "$TOP" lg:"$W/lg" --workdir "$W/w2"
run pass partition --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/re" --workdir "$W/w4"
run pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --workdir "$W/w5"

REGIONS=$(grep -oE '[A-Za-z0-9_.]+__(c[0-9]+|a_[0-9a-f]+)' "$W/re/library.txt" | sort -u)
[ -n "$REGIONS" ] || fail "no __cN region modules in the partition twin: $(cat "$W/re/library.txt")"

# Map every arithmetic region, including division.
"$LHD" pass "$MAPPER" --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net" --set synth.liberty="$LIB" \
  --workdir "$W/w3" --result-json "$W/r.json" 2>"$W/abc.err" || fail "pass "$MAPPER" -> $(cat "$W/r.json" 2>/dev/null)"
if grep -q '"code":"div-blackbox"' "$W/abc.err"; then fail "divider was not mapped"; fi
ls "$W/net"/graph_* >/dev/null 2>&1 || fail "no mapped netlist emitted"
# Structural: the partition twin holds the three dividers, the mapped netlist none.
[ -n "$("$LHD" tool grep kind=div lg:"$W/re" 2>/dev/null)" ] || fail "the partition twin lost its dividers"
divs="$("$LHD" tool grep kind=div lg:"$W/net" 2>/dev/null)"
[ -z "$divs" ] || fail "divider was not mapped: $divs"

# Ware extraction introduces new native module boundaries, so anonymous color
# port names can differ from the unextracted partition twin. Compare through
# the public top interface, checking every arithmetic output and its wiring.
# Cell models from --lib supply the mapped standard-cell implementations.
run lec --impl lg:"$W/net" --ref lg:"$W/re" --lib lg:"$W/models" --top "$TOP" --workdir "$W/wlec"

grep -q '"verdict":"proven"' "$W/r.json" \
  || fail "default LEC did not prove mapped hierarchy with --lib models: $(cat "$W/r.json")"

if [ "$MAPPER" = abc ]; then
  # A non-default adder still proves equivalent (the multiplier's partial-product
  # additions use pass.abc.adder).
  rm -rf "$W/net_cska"
  run pass "$MAPPER" --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net_cska" --set synth.liberty="$LIB" --set adder=cska \
    --workdir "$W/w6"
  run lec --impl lg:"$W/net_cska" --ref lg:"$W/re" --lib lg:"$W/models" --top "$TOP" --workdir "$W/wlec_cska"
else
  # Native logical CMOS output (tmap=none): the XAG expansion of mult/sra/div
  # has no Liberty cells, so it proves equivalent WITHOUT --lib.
  rm -rf "$W/net_none"
  run pass usyn --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net_none" --set pass.usyn.tmap=none --set adder=cska --workdir "$W/w6"
  python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); sys.exit(0 if (d["schema_version"],d["kind"],d["tmap"],d["output"])==(5,"usyn","none","logical-cmos") else 1)' \
    "$W/w6/qor.json" || fail "usyn tmap=none: qor.json is not the schema-5 logical-cmos decision report"
  divs="$("$LHD" tool grep kind=div lg:"$W/net_none" 2>/dev/null)"
  [ -z "$divs" ] || fail "tmap=none kept a native divider: $divs"
  run lec --impl lg:"$W/net_none" --ref lg:"$W/re" --top "$TOP" --workdir "$W/wlec_none"
  grep -q '"verdict":"proven"' "$W/r.json" \
    || fail "usyn tmap=none LEC did not prove the logical hierarchy: $(cat "$W/r.json")"
  # A flag pass.usyn does not register is a usage error (exit 2). block_size is
  # the shared registry spelling of adder_block and is accepted.
  rc=0
  "$LHD" pass usyn --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net_bad" --set synth.liberty="$LIB" --set not_a_usyn_flag=4 \
    --workdir "$W/w7" -q --result-json "$W/rr.json" 2>/dev/null || rc=$?
  [ "$rc" = 2 ] || fail "usyn: unknown option --set not_a_usyn_flag=4 exited $rc (expected usage error 2)"
  grep -q "unknown flag 'not_a_usyn_flag'" "$W/rr.json" || fail "usyn: --set not_a_usyn_flag=4: no unknown-flag diagnostic: $(cat "$W/rr.json")"
fi

# Negative control: the cell models are load-bearing. Without --lib the netlist's
# blackbox cell Subs are unresolved, so lec must NOT prove equivalence — a sound
# Unknown or a fail, never a vacuous pass. The impl-only box obligations gate the
# miter to INCONCLUSIVE (an incomplete correspondence can neither prove nor
# refute), which exits 0 unless strict — so run the control strict, where any
# non-Proven outcome is a hard failure exit.
if "$LHD" lec --impl lg:"$W/net" --ref lg:"$W/re" --top "$TOP" \
    --workdir "$W/wlec_nolib" -q --result-json "$W/rn.json" 2>/dev/null; then
  fail "lec proved equivalence with no --lib (unresolved cells must not vacuously pass)"
fi

echo "PASS: pass.$MAPPER mult/sra/div mapped, all lhd-lec-equivalent (signed + unsigned)"
