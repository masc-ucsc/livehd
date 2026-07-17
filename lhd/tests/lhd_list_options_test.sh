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
echo "$out" | grep -q '"name":"compile.bitwidth.max_iterations"' || fail "compile.bitwidth.* missing: $out"
# hier standardization: the vestigial per-def toggles are deleted, the real
# hier options all spell it `hier` and default true.
echo "$out" | grep -q '"name":"compile.cprop.hier"' && fail "compile.cprop.hier must be gone (vestigial): $out"
echo "$out" | grep -q '"name":"compile.bitwidth.hier"' && fail "compile.bitwidth.hier must be gone (vestigial): $out"
echo "$out" | grep -q '"name":"lec.hierarchical"' && fail "lec.hierarchical must be gone (renamed formal.lec.hier): $out"
# formal-root restructure (2026-07-17): lec.* is an accepted-but-unlisted alias;
# the canonical names are formal.<common> and formal.lec.<lec-specific>.
echo "$out" | grep -q '"name":"lec\.' && fail "lec.* must not be listed (alias of formal[.lec].*): $out"
echo "$out" | grep -q '"name":"formal.lec.hier","method":"pass.lec","default":"true"' || fail "formal.lec.hier default true missing: $out"
echo "$out" | grep -q '"name":"pass.semdiff.hier","method":"pass.semdiff","default":"true"' || fail "pass.semdiff.hier default true missing: $out"
echo "$out" | grep -q '"name":"pass.opentimer.hier","method":"pass.opentimer","default":"true"' || fail "pass.opentimer.hier default true missing: $out"
# per-pass `top` labels are hidden everywhere: lhd.top is the one spelling.
echo "$out" | grep -q '"name":"pass.opentimer.top"' && fail "pass.opentimer.top must be hidden (use --top / lhd.top): $out"
echo "$out" | grep -q '"name":"lhd.top"' || fail "lhd.top missing: $out"
echo "$out" | grep -q '"name":"lhd.stats"' || fail "lhd.stats missing: $out"
echo "$out" | grep -q '"name":"compile.prp_writer.debug"' || fail "compile.prp_writer.debug missing: $out"
echo "$out" | grep -q '"name":"formal.solver"' || fail "formal.solver (shared formal vocabulary) missing: $out"
echo "$out" | grep -q '"name":"formal.strict","method":"pass.lec","default":"false"' || fail "formal.strict missing: $out"
echo "$out" | grep -q '"name":"formal.isabelle.strict"' || fail "formal.isabelle.strict missing: $out"
echo "$out" | grep -q '"name":"formal.lean.strict"' || fail "formal.lean.strict missing: $out"
echo "$out" | grep -q '"name":"compile.isabelle.' && fail "compile.isabelle.* must not exist (canonical is formal.isabelle.*): $out"
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
# empty options section under `lhd pass <sub> --help`. `--diag-fmt pretty` is
# forced: this is a piped run (auto == jsonl), and the options block is a
# feature of the PRETTY page (jsonl help emits the machine record instead).
help=$("$LHD" pass abc --help --diag-fmt pretty 2>&1)
echo "$help" | grep -qF 'options (--set pass.abc.flag=value; `lhd describe pass.abc.flag` for each listed flag option in `lhd list options pass.abc`):' \
  || fail "pass abc --help: namespaced options header missing -> $help"
echo "$help" | grep -q 'pass.abc.flow=' || fail "pass abc --help: the flow flag must be listed (empty options block?) -> $help"
echo "$help" | grep -q 'more; `lhd list options pass.abc`' || fail "pass abc --help: >5 flags must show a '... more' pointer -> $help"
"$LHD" lec --help --diag-fmt pretty 2>&1 | grep -q 'options (--set formal.flag=value;' || fail "lec --help: formal-namespaced options header missing"

# 11. The `sim.*` command namespace (sim_command, not an EPRP pass) lives in the
# SAME registry as the pass flags, so `lhd list options` / `lhd describe` /
# `lhd sim --help` all see it (single source of truth = kSimSetOptions). It must
# stay DISTINCT from the `compile.sim.*` cgen labels, and must never collect a
# command-path prefix the way an abbreviated pass key does.
out=$("$LHD" list options)
echo "$out" | grep -q '"name":"sim.vcd","method":"sim","default":"false"' || fail "sim.vcd namespace option missing/wrong: $out"
echo "$out" | grep -q '"name":"sim.checkpoint","method":"sim","default":"true"' || fail "sim.checkpoint missing/wrong: $out"
echo "$out" | grep -q '"name":"sim.checkpoint_min_secs","method":"sim","default":"10"' || fail "sim.checkpoint_min_secs missing/wrong: $out"
# sim.* is the ONE sim vocabulary (2026-07-17): compile.sim.* does not exist,
# in the code or as an alias, and the codegen reads the same sim.vcd knob.
echo "$out" | grep -q '"name":"compile.sim.vcd"' && fail "compile.sim.* must not exist: $out"
echo "$out" | grep -q '"name":"sim.vcd_fake_delay","method":"sim","default":"true"' || fail "sim.vcd_fake_delay missing: $out"
# the old vcdfakedelay spelling is DELETED: setting it errors with the rename hint
"$LHD" sim "$PRP" --set sim.vcdfakedelay=false --workdir "$W/w11c" -q >"$W/r11c.json" 2>/dev/null && fail "--set sim.vcdfakedelay must fail (renamed)"
grep -q "use --set sim.vcd_fake_delay=false instead" "$W/r11c.json" || fail "vcdfakedelay rename hint missing: $(cat "$W/r11c.json")"
# `lhd list options sim` (regex) shows the one sim namespace.
simlist=$("$LHD" list options sim --diag-fmt pretty)
echo "$simlist" | grep -q '^sim\.checkpoint_min_secs=10' || fail "list options sim dropped sim.checkpoint_min_secs: $simlist"
# describe resolves `sim.flag` to the sim namespace, NOT compile.sim.* (regression:
# canonical_set_key used to prepend `compile.` because `compile.sim` is a pass).
"$LHD" describe sim.checkpoint_min_secs | grep -q '"name":"sim.checkpoint_min_secs","kind":"option","method":"sim"' \
  || fail "describe sim.checkpoint_min_secs must resolve to the sim namespace"
"$LHD" describe sim.vcd | grep -q '"name":"sim.vcd","kind":"option","method":"sim"' \
  || fail "describe sim.vcd must be the sim namespace, not compile.sim.vcd"
"$LHD" describe compile.sim.vcd >/dev/null 2>&1 && fail "describe compile.sim.vcd must fail (the namespace does not exist)"
# a --set of the dead spelling errors with the DIRECTED replacement hint
"$LHD" compile "$PRP" --set compile.sim.vcd=1 --workdir "$W/w11b" -q >"$W/r11b.json" 2>/dev/null && fail "--set compile.sim.vcd must fail"
grep -q "use --set sim.vcd=1 instead" "$W/r11b.json" || fail "dead compile.sim.vcd must name the replacement: $(cat "$W/r11b.json")"
# same for the removed lec.* namespace: common flags point at formal.*, pairing
# flags at formal.lec.*, and renamed flags compose (lec.minetimeout -> formal.mine_timeout)
"$LHD" compile "$PRP" --set lec.solver=lgyosys --workdir "$W/w11d" -q >"$W/r11d.json" 2>/dev/null && fail "--set lec.solver must fail (namespace removed)"
grep -q "use --set formal.solver=lgyosys instead" "$W/r11d.json" || fail "lec.solver must point at formal.solver: $(cat "$W/r11d.json")"
"$LHD" compile "$PRP" --set lec.hier=false --workdir "$W/w11e" -q >"$W/r11e.json" 2>/dev/null && fail "--set lec.hier must fail (namespace removed)"
grep -q "use --set formal.lec.hier=false instead" "$W/r11e.json" || fail "lec.hier must point at formal.lec.hier: $(cat "$W/r11e.json")"
"$LHD" compile "$PRP" --set lec.minetimeout=9 --workdir "$W/w11f" -q >"$W/r11f.json" 2>/dev/null && fail "--set lec.minetimeout must fail"
grep -q "use --set formal.mine_timeout=9 instead" "$W/r11f.json" || fail "lec.minetimeout must compose both renames: $(cat "$W/r11f.json")"
# `lhd sim --help` ends with the standardized options block (like lec/compile),
# enumerating the sim.* flags instead of a hand-maintained list (pretty page;
# jsonl help would emit the machine record instead — forced here since piped).
simhelp=$("$LHD" sim --help --diag-fmt pretty 2>&1)
echo "$simhelp" | grep -qF 'options (--set sim.flag=value; `lhd describe sim.flag` for each listed flag option in `lhd list options sim`):' \
  || fail "sim --help: standardized options header missing -> $simhelp"
echo "$simhelp" | grep -q '^  sim.checkpoint_min_secs=10 ' || fail "sim --help: sim.checkpoint_min_secs not listed -> $simhelp"
# Validation drives off the same table: an unknown sim flag is a usage error.
"$LHD" sim "$PRP" --set sim.bogus=1 --workdir "$W/w11" -q >"$W/r11.json" 2>/dev/null && fail "--set sim.bogus must fail"
grep -q "unknown sim flag 'sim.bogus'" "$W/r11.json" || fail "unknown sim-flag message missing: $(cat "$W/r11.json")"

# 12. Typo suggestions: an unknown option whose LEAF matches real options lists
# them ("maybe you meant"), e.g. potato.vcd -> sim.vcd.
"$LHD" compile "$PRP" --set potato.vcd=1 --workdir "$W/w12" -q >"$W/r12.json" 2>/dev/null && fail "--set potato.vcd must fail"
grep -q "maybe you meant:" "$W/r12.json" || fail "leaf-match suggestion missing: $(cat "$W/r12.json")"
# the candidates are rendered INLINE, `lhd list options`-style (name=default # help),
# so nobody has to run the second command themselves
grep -q "sim.vcd=false" "$W/r12.json" || fail "inline sim.vcd=default line missing: $(cat "$W/r12.json")"
"$LHD" compile "$PRP" --set cgen.strict=1 --workdir "$W/w12b" -q >"$W/r12b.json" 2>/dev/null && fail "--set cgen.strict must fail"
grep -q "formal.strict=false" "$W/r12b.json" || fail "wrong-pass inline suggestion missing: $(cat "$W/r12b.json")"

echo "PASS: lhd list options / describe pass.flag / --set validation / flag-order freedom / per-command --help options / sim namespace"
