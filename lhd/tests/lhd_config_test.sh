#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# --config lhd.toml: pass-flag defaults as a declared input file.
#  - [pass] table entries reach the pass (upass log shows the resolved order)
#  - an explicit CLI --set overrides the file entry
#  - graph compilation always runs constant propagation and bitwidth inference
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

printf '# comment survives\n[upass]\norder = "noop"\nverifier = false\n' > "$W/lhd.toml"

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

# Graph optimization is standard, including when a config supplies pass flags.
printf '[upass]\nverifier = false\n' > "$W/sane.toml"
"$LHD" compile "$PRP" --config "$W/sane.toml" --emit verilog:"$W/v3.v" --workdir "$W/w3" -q --result-json "$W/r3.json" \
  || fail "compile with a sane config exited non-zero: $(cat "$W/r3.json" 2>/dev/null)"
grep -q 'pass.aegraph' "$W/r3.json" || fail "the default engine is the e-graph one (pass.aegraph): $(cat "$W/r3.json")"
grep -q 'pass.bitwidth' "$W/r3.json" || fail "compile did not run pass.bitwidth"
# A bare ln: emit produces no graphs and must still pass.
"$LHD" compile "$PRP" --config "$W/sane.toml" --emit-dir ln:"$W/lns/" --workdir "$W/w4" -q --result-json "$W/r4.json" \
  || fail "compile --emit-dir ln: with a config exited non-zero: $(cat "$W/r4.json" 2>/dev/null)"

# Both fold slots run the e-graph engine (pass.aegraph); the classic engine and
# its selector are gone, from --set and from a [compile.cprop] config table alike.
count_steps() { grep -o '"recipe":\[[^]]*\]' "$1" | grep -o "\"pass\.$2[ \"]" | wc -l; }
"$LHD" compile "$PRP" --config "$W/sane.toml" --emit verilog:"$W/e_default.v" --workdir "$W/e_default" -q \
  --result-json "$W/e_default.json" || fail "default compile failed: $(cat "$W/e_default.json" 2>/dev/null)"
[ "$(count_steps "$W/e_default.json" aegraph)" -eq 2 ] || fail "pass.aegraph must run in both slots: $(cat "$W/e_default.json")"
[ "$(count_steps "$W/e_default.json" cprop)" -eq 0 ] || fail "no pass.cprop step remains: $(cat "$W/e_default.json")"
"$LHD" compile "$PRP" --set compile.cprop.engine=classic --workdir "$W/e_removed" -q >"$W/e_removed.json" 2>&1 \
  && fail "compile.cprop.engine must be rejected"
grep -q 'was removed' "$W/e_removed.json" || fail "compile.cprop.engine must say it was removed: $(cat "$W/e_removed.json")"
printf '[compile.cprop]\nengine = "aegraph"\n' > "$W/engine.toml"
"$LHD" compile "$PRP" --config "$W/engine.toml" --workdir "$W/e_cfg" -q >"$W/e_cfg.json" 2>&1 \
  && fail "a [compile.cprop] engine entry must be rejected"
grep -q 'was removed' "$W/e_cfg.json" || fail "[compile.cprop] engine must say it was removed: $(cat "$W/e_cfg.json")"

# Optimization-level selection has been removed from both CLI and config.
out=$("$LHD" compile "$PRP" --recipe O2 -q 2>/dev/null)
[ $? -ne 0 ] || fail "removed --recipe flag must error"
grep -q 'recipe' <<<"$out" || fail "removed flag diagnostic must name recipe: $out"
printf 'recipe = "O2"\n' > "$W/recipe.toml"
out=$("$LHD" compile "$PRP" --config "$W/recipe.toml" -q 2>/dev/null)
[ $? -ne 0 ] || fail "removed recipe config key must error"
grep -q '"class":"config"' <<<"$out" || fail "removed key must be a config error: $out"

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

echo "PASS: --config defaults, CLI precedence, fixed compile pipeline, engine dispatch, and strict junk rejection"
