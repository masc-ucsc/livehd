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
echo "$out" | grep -q '"name":"cgen.srcmap"' || fail "cgen.srcmap missing: $out"
echo "$out" | grep -q '"name":"upass.verifier"' || fail "upass.verifier missing: $out"
echo "$out" | grep -q '"name":"upass.reset_style","method":"pass.upass","default":"sync"' \
  || fail "reset_style default/method missing: $out"
echo "$out" | grep -q '"name":"cgen.odir"' && fail "kernel-managed odir must not be listed: $out"

# 2. The options pattern is advertised.
"$LHD" list | grep -q '"name":"options"' || fail "bare lhd list must advertise the options pattern"

# 3. --diag-fmt pretty: one `pass.flag=default  # help` line each.
out=$("$LHD" list options --diag-fmt pretty)
echo "$out" | grep -q '^cgen\.srcmap=false  *# emit an ECMA-426' || fail "pretty cgen.srcmap line missing: $out"
echo "$out" | grep -q 'schema_version' && fail "pretty leaked JSON: $out"

# 4. REGEX filter (regex_search over pass.flag names).
out=$("$LHD" list options 'cgen\..*' --diag-fmt pretty)
echo "$out" | grep -q '^cgen\.srcmap=' || fail "filter dropped cgen.srcmap: $out"
echo "$out" | grep -q '^upass\.' && fail "filter leaked non-cgen options: $out"
"$LHD" list options '[' >/dev/null 2>"$W/e4" && fail "a bad regex must fail"
grep -q 'bad regex' "$W/e4" || fail "bad-regex error text missing: $(cat "$W/e4")"

# 5. describe pass.flag: full help. jsonl when piped, prose with --diag-fmt pretty.
out=$("$LHD" describe cgen.srcmap)
echo "$out" | grep -q '"kind":"option"' || fail "describe jsonl kind missing: $out"
echo "$out" | grep -q '"method":"inou.cgen.verilog"' || fail "describe jsonl method missing: $out"
out=$("$LHD" describe upass.toln --diag-fmt pretty)
echo "$out" | grep -q 'upass.toln = true' || fail "describe pretty header missing: $out"
echo "$out" | grep -q 'forced back on while tolg:1' || fail "describe pretty must show the FULL help: $out"
"$LHD" describe cgen.bogus 2>"$W/e5" && fail "describe of an unknown flag must fail"
grep -q "lhd list options cgen" "$W/e5" || fail "describe unknown-flag hint missing: $(cat "$W/e5")"

# 6. --set validation: unknown pass, unknown flag, kernel-managed flag — all
#    usage errors (envelope on stdout), checked before any work runs.
"$LHD" compile "$PRP" --set foo.bar=1 --workdir "$W/w6a" -q >"$W/r6a.json" 2>/dev/null && fail "--set foo.bar must fail"
grep -q '"class":"usage"' "$W/r6a.json" || fail "unknown pass must be a usage error: $(cat "$W/r6a.json")"
grep -q "unknown pass 'foo'" "$W/r6a.json" || fail "unknown-pass message missing: $(cat "$W/r6a.json")"
"$LHD" compile "$PRP" --set cgen.bogus=1 --workdir "$W/w6b" -q >"$W/r6b.json" 2>/dev/null && fail "--set cgen.bogus must fail"
grep -q "unknown flag 'bogus' of pass 'cgen'" "$W/r6b.json" || fail "unknown-flag message missing: $(cat "$W/r6b.json")"
"$LHD" compile "$PRP" --set cgen.odir=/tmp/zz --workdir "$W/w6c" -q >"$W/r6c.json" 2>/dev/null && fail "--set cgen.odir must fail"
grep -q 'kernel-managed' "$W/r6c.json" || fail "kernel-managed message missing: $(cat "$W/r6c.json")"

# 7. The same validation covers --config tables (one funnel: opts.sets).
cat >"$W/bad.toml" <<'EOF'
[cgen]
bogus = true
EOF
"$LHD" compile "$PRP" --config "$W/bad.toml" --workdir "$W/w7" -q >"$W/r7.json" 2>/dev/null && fail "config typo must fail"
grep -q "unknown flag 'bogus' of pass 'cgen'" "$W/r7.json" || fail "config typo message missing: $(cat "$W/r7.json")"

# 8. Listed flags really are settable end-to-end.
"$LHD" compile "$PRP" --set cgen.srcmap=1 --set upass.verifier=false --emit-dir verilog:"$W/v8" --workdir "$W/w8" -q \
  >/dev/null 2>&1 || fail "valid --set flags must still compile"
ls "$W"/v8/*.v.map >/dev/null 2>&1 || fail "cgen.srcmap=1 must still produce the .v.map sidecar"

# 9. Flag-order freedom: shared flags may come before the command word, with
#    value-taking flags keeping their value; the run_id must not depend on
#    flag order. The optional language word also survives intervening flags.
out=$("$LHD" --diag-fmt pretty list options 'cgen\..*')
echo "$out" | grep -q '^cgen\.srcmap=' || fail "flags-before-command list options failed: $out"
"$LHD" --workdir "$W/w9a" -q compile "$PRP" >"$W/r9a.json" 2>/dev/null || fail "flags-before-command compile failed"
"$LHD" compile "$PRP" --workdir "$W/w9b" -q >"$W/r9b.json" 2>/dev/null || fail "flags-after-command compile failed"
id_a=$(sed 's/.*"run_id":"\([^"]*\)".*/\1/' "$W/r9a.json")
id_b=$(sed 's/.*"run_id":"\([^"]*\)".*/\1/' "$W/r9b.json")
[ -n "$id_a" ] && [ "$id_a" = "$id_b" ] || fail "run_id must not depend on flag order: '$id_a' vs '$id_b'"
"$LHD" compile -q pyrope "$PRP" --workdir "$W/w9c" >/dev/null 2>&1 \
  || fail "language word after intervening flags failed"
"$LHD" --help >/dev/null 2>&1 || fail "lhd --help must print the general help"
"$LHD" --bogus list >/dev/null 2>&1 && fail "an unknown flag before the command must still error"

echo "PASS: lhd list options / describe pass.flag / --set validation / flag-order freedom"
