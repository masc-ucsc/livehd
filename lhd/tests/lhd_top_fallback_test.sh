#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# The --top entity fallback: internal module names are `file.entity`, so a
# Pyrope compile of adder.prp yields the graph `adder.adder` and a bare
# `--top adder` used to fail (or silently degrade) in most commands. Every
# command that resolves a top against internal graph/unit names must now
# accept the bare entity via the shared resolver (resolve_top_name), emit the
# `top-entity-fallback` diag warning, and hand its consumer the RESOLVED full
# name. The fallback must stay SAFE: ambiguous entities and wrong-file dotted
# spellings do not resolve, an unresolvable --top still errors, and `tool
# diff` must not print "identical" for it (the silent false-equal trap).
# --diag-fmt jsonl everywhere a grep needs the warning CODE (the pretty
# format prints the message, not the code, and format auto-picks by tty).

set -u

LHD=lhd/lhd
PRP=lhd/tests/merge_demo/adder.prp
W="${TEST_TMPDIR:-/tmp/lhd_top_fallback_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}
run() { "$LHD" "$@" -q --result-json "$W/r.json" || fail "$* -> $(cat "$W/r.json" 2>/dev/null)"; }

run compile "$PRP" --emit-dir lg:"$W/lg" --workdir "$W/w0"

# pass plumbing: the kernel resolves --top once and labels["top"] carries the
# full name (the recipe echoes it). No -q: the warning must reach stderr.
"$LHD" pass color acyclic --top adder lg:"$W/lg" --workdir "$W/w1" --diag-fmt jsonl --result-json "$W/r.json" 2>"$W/color.err" \
  || fail "pass color --top adder: $(cat "$W/r.json" 2>/dev/null)"
grep -q 'top-entity-fallback' "$W/color.err" || fail "pass color: no top-entity-fallback warning"
grep -q 'top:adder.adder' "$W/r.json" || fail "pass color: recipe does not show the resolved top"

# lg-side tool verbs go through tool_select_graphs.
"$LHD" tool tree lg:"$W/lg" --top adder --diag-fmt jsonl >"$W/tree.out" 2>"$W/tree.err" || fail "tool tree --top adder failed"
grep -q 'adder.adder' "$W/tree.out" || fail "tool tree: adder.adder root not printed"
grep -q 'top-entity-fallback' "$W/tree.err" || fail "tool tree: no top-entity-fallback warning"

# ln-side tool verbs go through filter_top (unit names are file.entity too).
# The file must differ from the entity (addfile.adder) or the FILE unit
# `adder` exact-matches and the fallback never fires.
cp "$PRP" "$W/addfile.prp"
run compile "$W/addfile.prp" --emit-dir ln:"$W/ln" --workdir "$W/w2"
"$LHD" tool tree ln:"$W/ln" --top adder --diag-fmt jsonl >"$W/lntree.out" 2>"$W/lntree.err" \
  || fail "tool tree ln: --top adder failed"
grep -q 'addfile.adder' "$W/lntree.out" || fail "tool tree ln:: addfile.adder unit not printed"
grep -q 'top-entity-fallback' "$W/lntree.err" || fail "tool tree ln:: no top-entity-fallback warning"

# lec / semdiff per-side top pick (semdiff used to hard-error on the entity;
# semdiff needs two DISTINCT lg: libraries, so diff a copy).
run lec --impl lg:"$W/lg" --ref lg:"$W/lg" --top adder
cp -r "$W/lg" "$W/lg2"
run pass semdiff --ref lg:"$W/lg" --impl lg:"$W/lg2" --top adder

# sim emit: the resolved FULL name reaches inou.cgen.sim, so both the bare
# entity and the full spelling bake the VCD path into exactly the top module.
rm -rf "$W/simd"
run compile "$PRP" --emit-dir sim:"$W/simd" --top adder.adder --set sim.vcd=out.vcd --workdir "$W/w3"
grep -ql '__vcd_path = "out.vcd"' "$W/simd/adder.adder.hpp" || fail "sim emit: full --top did not bake the VCD path"

# A top that resolves nowhere still errors; tool diff must fail rather than
# report the empty-vs-empty selection as "identical", and diff --match must
# fail rather than print the misleading "run semdiff" hint.
if "$LHD" tool diff lg:"$W/lg" lg:"$W/lg2" --top no_such_module >"$W/diff.out" 2>&1; then
  fail "tool diff with a bogus --top must fail"
fi
grep -q 'identical' "$W/diff.out" && fail "tool diff printed 'identical' for a bogus --top"
if "$LHD" tool diff lg:"$W/lg" lg:"$W/lg2" --match --top no_such_module >"$W/mdiff.out" 2>&1; then
  fail "tool diff --match with a bogus --top must fail"
fi
grep -q 'run semdiff' "$W/mdiff.out" && fail "tool diff --match printed the semdiff hint for a bogus --top"

# SAFETY: with two same-entity modules (x.adder + y.adder) the entity match is
# ambiguous, so neither the bare entity nor a dotted spelling with a wrong
# file part may resolve (the fallback matches the entity of both sides — lec's
# rule — but only when UNIQUE). The exact full name still works.
printf 'pub comb adder(a:s8) -> (r:s9) { r = a + 1 }\n' >"$W/x.prp"
printf 'pub comb adder(a:s8) -> (r:s9) { r = a + 2 }\n' >"$W/y.prp"
run compile "$W/x.prp" "$W/y.prp" --emit-dir lg:"$W/amb" --workdir "$W/w4"
"$LHD" tool tree lg:"$W/amb" --top adder >/dev/null 2>&1 && fail "ambiguous entity --top must not resolve"
"$LHD" tool tree lg:"$W/amb" --top zzz.adder >/dev/null 2>&1 && fail "ambiguous dotted --top must not resolve"
"$LHD" tool tree lg:"$W/amb" --top x.adder >"$W/amb.out" 2>&1 || fail "exact full --top failed on ambiguous library"
grep -q 'x.adder' "$W/amb.out" || fail "exact full --top did not select x.adder"

echo "PASS: --top entity fallback resolves XXX to XXX.XXX across commands, safely"
