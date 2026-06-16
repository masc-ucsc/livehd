#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# --config lhd.toml: pass-flag defaults as a declared input file.
#  - [pass] table entries reach the pass (upass log shows the resolved order)
#  - an explicit CLI --set overrides the file entry
#  - the top-level `recipe` key drives compile's graph passes (a no-op when no
#    graphs are produced, e.g. a bare --emit-dir ln: lowering)
#  - junk (unknown top-level key / typo'd pass table) errors, never no-ops

set -u

LHD=lhd/lhd
PRP=inou/prp/tests/equiv/trivial_if.prp
W="${TEST_TMPDIR:-/tmp/lhd_config_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

printf 'recipe = "O2"  # comment survives\n[upass]\norder = "noop"\nverifier = false\n' > "$W/lhd.toml"

# The [upass] table reaches pass.upass (compile lowers, so upass runs).
# order=noop guts the pipeline, so tolg may legitimately fail afterwards —
# only the resolved order matters here.
"$LHD" compile "$PRP" --config "$W/lhd.toml" --emit verilog:"$W/v1.v" --workdir "$W/w1" -q --result-json "$W/r1.json" 2>/dev/null
grep -q "resolved order: noop" "$W/w1/logs/"*upass*.log || fail "config [upass] order=noop did not reach pass.upass"

# CLI --set wins over the file entry.
"$LHD" compile "$PRP" --config "$W/lhd.toml" --set upass.order=constprop --emit verilog:"$W/v2.v" --workdir "$W/w2" -q \
  --result-json "$W/r2.json" 2>/dev/null
# constprop pulls in its declared deps (attributes typecheck), so the --set
# override resolves to the full chain ending in constprop — not the file's noop.
grep -q "resolved order: attributes typecheck constprop" "$W/w2/logs/"*upass*.log || fail "--set must override the config file entry"

# recipe = "O2" from the file drives the graph passes when graphs are produced.
printf 'recipe = "O2"\n[upass]\nverifier = false\n' > "$W/sane.toml"
"$LHD" compile "$PRP" --config "$W/sane.toml" --emit verilog:"$W/v3.v" --workdir "$W/w3" -q --result-json "$W/r3.json" \
  || fail "compile with a sane config exited non-zero: $(cat "$W/r3.json" 2>/dev/null)"
grep -q 'pass.bitwidth' "$W/r3.json" || fail "config recipe=O2 did not run pass.bitwidth: $(cat "$W/r3.json")"
# A bare ln: emit produces no graphs, so the recipe is a no-op (must still pass).
"$LHD" compile "$PRP" --config "$W/sane.toml" --emit-dir ln:"$W/lns/" --workdir "$W/w4" -q --result-json "$W/r4.json" \
  || fail "compile --emit-dir ln: with a recipe config exited non-zero: $(cat "$W/r4.json" 2>/dev/null)"

# Junk must error (config class), never silently no-op. Config-load errors
# fire in argv parsing, before --result-json exists -> JSON lands on stdout.
printf 'top_level_junk = 1\n' > "$W/bad.toml"
out=$("$LHD" compile "$PRP" --config "$W/bad.toml" --emit verilog:"$W/x.v" -q 2>/dev/null)
[ $? -ne 0 ] || fail "unknown top-level config key must error"
grep -q '"class":"config"' <<<"$out" || fail "expected error.class=config: $out"

# A typo'd pass table parses fine but must fail pass-name validation.
printf '[upasss]\nverifier = false\n' > "$W/typo.toml"
"$LHD" compile "$PRP" --config "$W/typo.toml" --emit verilog:"$W/x.v" -q --result-json "$W/r6.json" 2>/dev/null
[ $? -ne 0 ] || fail "typo'd pass table must error"
grep -q "unknown pass 'upasss'" "$W/r6.json" || fail "expected the unknown-pass message: $(cat "$W/r6.json")"

# A missing config file is a missing_file error (hermetic kernel).
out=$("$LHD" compile "$PRP" --config "$W/nope.toml" --emit verilog:"$W/x.v" -q 2>/dev/null)
[ $? -ne 0 ] || fail "missing config file must error"
grep -q '"class":"missing_file"' <<<"$out" || fail "expected error.class=missing_file: $out"

echo "PASS: --config lhd.toml defaults, CLI precedence, recipe pickup, and strict junk rejection"
