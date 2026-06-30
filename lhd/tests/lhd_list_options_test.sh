#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `lhd list options [REGEX]` + `lhd describe pass.flag`: the --set/--config
# vocabulary, enumerated from the live EPRP label registry (one registration
# point: add_label_optional/required in each pass's setup). Honors --diag-fmt
# (auto == jsonl here, stdout is piped). Also the strict --set/--config
# validation: a typo'd pass or flag is a usage error, never a silent no-op,
# and the Pass-base plumbing labels (files/path/odir) are kernel-managed.

set -u

LHD=lhd/lhd
PRP=inou/prp/tests/equiv/trivial_if.prp
W="${TEST_TMPDIR:-/tmp/lhd_list_options_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# 1. Piped (auto == jsonl): one JSON line in the `lhd list` schema.
out=$("$LHD" list options)
case "$out" in
  '{"schema_version":1,"pattern":"options"'*) ;;
  *) fail "piped list options must be the JSON line: $out" ;;
esac
echo "$out" | grep -q '"name":"compile.cgen.srcmap"' || fail "compile.cgen.srcmap missing: $out"
echo "$out" | grep -q '"name":"compile.upass.verifier"' || fail "compile.upass.verifier missing: $out"
echo "$out" | grep -q '"name":"compile.upass.reset_style","method":"pass.upass","default":"sync"' \
  || fail "reset_style default/method missing: $out"
echo "$out" | grep -q '"name":"compile.cgen.odir"' && fail "kernel-managed odir must not be listed: $out"
# Compile-only passes live under compile.*; lec/pass passes keep their own namespace.
echo "$out" | grep -q '"name":"compile.cprop.hier"' || fail "compile.cprop.hier missing: $out"
echo "$out" | grep -q '"name":"compile.bitwidth.max_iterations"' || fail "compile.bitwidth.* missing: $out"
echo "$out" | grep -q '"name":"compile.prp_writer.debug"' || fail "compile.prp_writer.debug missing: $out"
echo "$out" | grep -q '"name":"lec.solver"' || fail "lec.solver must stay top-level: $out"
# The shared lhd.* kernel namespace (seed); the old per-pass top/seed are gone.
echo "$out" | grep -q '"name":"lhd.seed"' || fail "shared lhd.seed missing: $out"
echo "$out" | grep -q '"name":"pass.color.seed"' && fail "per-pass pass.color.seed must be gone (use lhd.seed): $out"
echo "$out" | grep -q '"name":"pass.color.top"' && fail "per-pass pass.color.top must be gone (use --top): $out"
echo "$out" | grep -q '"name":"pass.abc.top"' && fail "per-pass pass.abc.top must be gone (use --top): $out"
echo "$out" | grep -q '"name":"pass.partition.top"' && fail "per-pass pass.partition.top must be gone (use --top): $out"

# 2. The options pattern is advertised.
"$LHD" list | grep -q '"name":"options"' || fail "bare lhd list must advertise the options pattern"

# 3. --diag-fmt pretty: one `pass.flag=default  # help` line each.
out=$("$LHD" list options --diag-fmt pretty)
echo "$out" | grep -q '^compile\.cgen\.srcmap=false  *# emit an ECMA-426' || fail "pretty compile.cgen.srcmap line missing: $out"
echo "$out" | grep -q 'schema_version' && fail "pretty leaked JSON: $out"

# 4. REGEX filter (regex_search over pass.flag names).
out=$("$LHD" list options 'compile\.cgen\..*' --diag-fmt pretty)
echo "$out" | grep -q '^compile\.cgen\.srcmap=' || fail "filter dropped compile.cgen.srcmap: $out"
echo "$out" | grep -q '^compile\.upass\.' && fail "filter leaked non-cgen options: $out"
"$LHD" list options '[' >/dev/null 2>"$W/e4" && fail "a bad regex must fail"
grep -q 'bad regex' "$W/e4" || fail "bad-regex error text missing: $(cat "$W/e4")"

# 5. describe pass.flag: full help. jsonl when piped, prose with --diag-fmt pretty.
#    An abbreviated key resolves to its compile.* namespace (2h-set_path), so
#    `describe cgen.srcmap` still works and reports the canonical name.
out=$("$LHD" describe cgen.srcmap)
echo "$out" | grep -q '"kind":"option"' || fail "describe jsonl kind missing: $out"
echo "$out" | grep -q '"name":"compile.cgen.srcmap"' || fail "describe must report the canonical name: $out"
echo "$out" | grep -q '"method":"inou.cgen.verilog"' || fail "describe jsonl method missing: $out"
out=$("$LHD" describe upass.toln --diag-fmt pretty)
echo "$out" | grep -q 'compile.upass.toln = true' || fail "describe pretty header missing: $out"
echo "$out" | grep -q 'forced back on while tolg:1' || fail "describe pretty must show the FULL help: $out"
"$LHD" describe cgen.bogus 2>"$W/e5" && fail "describe of an unknown flag must fail"
grep -q "lhd list options compile.cgen" "$W/e5" || fail "describe unknown-flag hint missing: $(cat "$W/e5")"
# The shared lhd.* namespace describes too.
"$LHD" describe lhd.seed | grep -q '"name":"lhd.seed"' || fail "describe lhd.seed missing"

# 6. --set validation: unknown pass, unknown flag, kernel-managed flag — all
#    usage errors (envelope on stdout), checked before any work runs.
"$LHD" compile "$PRP" --set foo.bar=1 --workdir "$W/w6a" -q >"$W/r6a.json" 2>/dev/null && fail "--set foo.bar must fail"
grep -q '"class":"usage"' "$W/r6a.json" || fail "unknown pass must be a usage error: $(cat "$W/r6a.json")"
grep -q "unknown pass 'foo'" "$W/r6a.json" || fail "unknown-pass message missing: $(cat "$W/r6a.json")"
"$LHD" compile "$PRP" --set cgen.bogus=1 --workdir "$W/w6b" -q >"$W/r6b.json" 2>/dev/null && fail "--set cgen.bogus must fail"
grep -q "unknown flag 'bogus' of pass 'compile.cgen'" "$W/r6b.json" || fail "unknown-flag message missing: $(cat "$W/r6b.json")"
"$LHD" compile "$PRP" --set cgen.odir=/tmp/zz --workdir "$W/w6c" -q >"$W/r6c.json" 2>/dev/null && fail "--set cgen.odir must fail"
grep -q 'kernel-managed' "$W/r6c.json" || fail "kernel-managed message missing: $(cat "$W/r6c.json")"

# 7. The same validation covers --config tables (one funnel: opts.sets).
cat >"$W/bad.toml" <<'EOF'
[cgen]
bogus = true
EOF
"$LHD" compile "$PRP" --config "$W/bad.toml" --workdir "$W/w7" -q >"$W/r7.json" 2>/dev/null && fail "config typo must fail"
grep -q "unknown flag 'bogus' of pass 'compile.cgen'" "$W/r7.json" || fail "config typo message missing: $(cat "$W/r7.json")"

# 8. Listed flags really are settable end-to-end.
"$LHD" compile "$PRP" --set cgen.srcmap=1 --set upass.verifier=false --emit-dir verilog:"$W/v8" --workdir "$W/w8" -q \
  >/dev/null 2>&1 || fail "valid --set flags must still compile"
ls "$W"/v8/*.v.map >/dev/null 2>&1 || fail "cgen.srcmap=1 must still produce the .v.map sidecar"

# 9. Flag-order freedom: shared flags may come before the command word, with
#    value-taking flags keeping their value; the run_id must not depend on
#    flag order. The optional language word also survives intervening flags.
out=$("$LHD" --diag-fmt pretty list options 'compile\.cgen\..*')
echo "$out" | grep -q '^compile\.cgen\.srcmap=' || fail "flags-before-command list options failed: $out"
"$LHD" --workdir "$W/w9a" -q compile "$PRP" >"$W/r9a.json" 2>/dev/null || fail "flags-before-command compile failed"
"$LHD" compile "$PRP" --workdir "$W/w9b" -q >"$W/r9b.json" 2>/dev/null || fail "flags-after-command compile failed"
id_a=$(sed 's/.*"run_id":"\([^"]*\)".*/\1/' "$W/r9a.json")
id_b=$(sed 's/.*"run_id":"\([^"]*\)".*/\1/' "$W/r9b.json")
[ -n "$id_a" ] && [ "$id_a" = "$id_b" ] || fail "run_id must not depend on flag order: '$id_a' vs '$id_b'"
"$LHD" compile -q pyrope "$PRP" --workdir "$W/w9c" >/dev/null 2>&1 \
  || fail "language word after intervening flags failed"
"$LHD" --help >/dev/null 2>&1 || fail "lhd --help must print the general help"
"$LHD" --bogus list >/dev/null 2>&1 && fail "an unknown flag before the command must still error"

# 10. per-command --help options block: namespaced header + the actual flags
# (not the old silently-empty list), capped with a "more" pointer, and pointing
# at `lhd list options <namespace>`. Guards the wrong-prefix bug that printed an
# empty options section under `lhd pass <sub> --help`.
help=$("$LHD" pass abc --help 2>&1)
echo "$help" | grep -qF 'options (--set pass.abc.flag=value; `lhd describe pass.abc.flag` for each listed flag option in `lhd list options pass.abc`):' \
  || fail "pass abc --help: namespaced options header missing -> $help"
echo "$help" | grep -q 'pass.abc.flow=' || fail "pass abc --help: the flow flag must be listed (empty options block?) -> $help"
echo "$help" | grep -q 'more; `lhd list options pass.abc`' || fail "pass abc --help: >5 flags must show a '... more' pointer -> $help"
"$LHD" lec --help 2>&1 | grep -q 'options (--set lec.flag=value;' || fail "lec --help: namespaced options header missing"

# 11. The `sim.*` command namespace (sim_command, not an EPRP pass) lives in the
# SAME registry as the pass flags, so `lhd list options` / `lhd describe` /
# `lhd sim --help` all see it (single source of truth = kSimSetOptions). It must
# stay DISTINCT from the `compile.sim.*` cgen labels, and must never collect a
# command-path prefix the way an abbreviated pass key does.
out=$("$LHD" list options)
echo "$out" | grep -q '"name":"sim.vcd","method":"sim","default":"false"' || fail "sim.vcd namespace option missing/wrong: $out"
echo "$out" | grep -q '"name":"sim.checkpoint","method":"sim","default":"true"' || fail "sim.checkpoint missing/wrong: $out"
echo "$out" | grep -q '"name":"sim.checkpoint_min_secs","method":"sim","default":"10"' || fail "sim.checkpoint_min_secs missing/wrong: $out"
# The cgen `compile.sim.*` labels stay separate (different method, distinct knob).
echo "$out" | grep -q '"name":"compile.sim.vcd","method":"inou.cgen.sim"' || fail "compile.sim.vcd cgen label missing: $out"
# `lhd list options sim` (regex) surfaces BOTH namespaces in one place.
simlist=$("$LHD" list options sim --diag-fmt pretty)
echo "$simlist" | grep -q '^sim\.checkpoint_min_secs=10' || fail "list options sim dropped sim.checkpoint_min_secs: $simlist"
echo "$simlist" | grep -q '^compile\.sim\.vcd=' || fail "list options sim dropped compile.sim.vcd: $simlist"
# describe resolves `sim.flag` to the sim namespace, NOT compile.sim.* (regression:
# canonical_set_key used to prepend `compile.` because `compile.sim` is a pass).
"$LHD" describe sim.checkpoint_min_secs | grep -q '"name":"sim.checkpoint_min_secs","kind":"option","method":"sim"' \
  || fail "describe sim.checkpoint_min_secs must resolve to the sim namespace"
"$LHD" describe sim.vcd | grep -q '"name":"sim.vcd","kind":"option","method":"sim"' \
  || fail "describe sim.vcd must be the sim namespace, not compile.sim.vcd"
"$LHD" describe compile.sim.vcd | grep -q '"name":"compile.sim.vcd","kind":"option","method":"inou.cgen.sim"' \
  || fail "describe compile.sim.vcd must still reach the distinct cgen label"
# `lhd sim --help` ends with the standardized options block (like lec/compile),
# enumerating the sim.* flags instead of a hand-maintained list.
simhelp=$("$LHD" sim --help 2>&1)
echo "$simhelp" | grep -qF 'options (--set sim.flag=value; `lhd describe sim.flag` for each listed flag option in `lhd list options sim`):' \
  || fail "sim --help: standardized options header missing -> $simhelp"
echo "$simhelp" | grep -q '^  sim.checkpoint_min_secs=10 ' || fail "sim --help: sim.checkpoint_min_secs not listed -> $simhelp"
# Validation drives off the same table: an unknown sim flag is a usage error.
"$LHD" sim "$PRP" --set sim.bogus=1 --workdir "$W/w11" -q >"$W/r11.json" 2>/dev/null && fail "--set sim.bogus must fail"
grep -q "unknown sim flag 'sim.bogus'" "$W/r11.json" || fail "unknown sim-flag message missing: $(cat "$W/r11.json")"

echo "PASS: lhd list options / describe pass.flag / --set validation / flag-order freedom / per-command --help options / sim namespace"
